#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <optional>

namespace {

// Written on first run so the user has every key in front of them to edit.
const char DefaultConfig[] = R"(; spectra settings. Saved edits apply live.

[look]
background=#000000
colors=#ff0000, #ffff00, #00ff00, #00ffff, #0000ff, #ff00ff
mode=spectrum        ; spectrum = hue across bands, vertical = bottom to top
peaks=true
glow=false           ; soft light around the bars (G or the corner icon)
layout=bars          ; bars, mirrored, radial, stereo or waveform (L cycles)
gap=0.25             ; space between bars, as a fraction of a bar (0 to 0.9)

[analysis]
bands=96             ; 8 to 256
fft_size=8192        ; 512, 1024, 2048, 4096, 8192 or 16384
min_hz=30
max_hz=16000
floor_db=-70         ; this level and below is an empty bar
ceiling_db=-3        ; this level and above is a full bar
attack=0.02          ; seconds for a bar to rise
decay=0.25           ; seconds for a bar to fall
peak_fall=0.6        ; peak caps fall this much of full height per second
delay=auto           ; seconds to hold the picture back; auto follows the output's latency

[capture]
target=              ; an app's name (like spotify) to show only its sound; empty = everything
sleep_after=5        ; seconds of silence before capture pauses so the device can sleep; 0 = never

[wallpaper]          ; used by spectra --wallpaper
height=0.35          ; fraction of the screen the bars fill, from the bottom
opacity=0.8
transparent=true     ; show the desktop wallpaper behind the bars
layer=bottom         ; bottom = just above the wallpaper, background = replaces it
monitor=             ; output name like DP-1; empty = the compositor's choice
)";

const QStringList DefaultColors = {"#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff"};
constexpr double DefaultGap = 0.25;
constexpr double MaxHz = 24000.0; // Nyquist at the 48 kHz capture rate

// "section/key" to its raw value, with comments stripped.
using Ini = QHash<QString, QString>;

// The look keys parsed from the file, with defaults for anything missing or bad.
struct Look {
    QColor background = Qt::black;
    QStringList colors = DefaultColors;
    QString mode = QStringLiteral("spectrum");
    bool peaks = true;
    bool glow = false;
    double gap = DefaultGap;
};

// An inline comment needs whitespace before the semicolon, so a value that
// happens to contain one is never cut short.
const QRegularExpression &inlineComment() {
    static const QRegularExpression re(QStringLiteral("\\s+;.*$"));
    return re;
}

QString stripComment(const QString &line) {
    QString text = line;
    text.remove(inlineComment());
    return text.trimmed();
}

bool isSkippable(const QString &line) {
    return line.isEmpty() || line.startsWith(';') || line.startsWith('#');
}

bool isSection(const QString &line) {
    return line.startsWith('[') && line.endsWith(']');
}

QString sectionOf(const QString &line) {
    return line.mid(1, line.size() - 2).trimmed().toLower();
}

QString keyOf(const QString &line) {
    return line.left(line.indexOf('=')).trimmed().toLower();
}

Ini parseIni(const QString &text) {
    Ini ini;
    QString section;
    for (const QString &raw : text.split('\n')) {
        const QString line = stripComment(raw);
        if (isSkippable(line))
            continue;
        if (isSection(line))
            section = sectionOf(line);
        else if (line.indexOf('=') > 0)
            ini.insert(section + '/' + keyOf(line), line.mid(line.indexOf('=') + 1).trimmed());
    }
    return ini;
}

// The line index of key in section, or -1. When a key appears twice the
// last one is returned, because that's the one parseIni reads; changing an
// earlier copy would save a value that never takes effect.
int findKeyLine(const QStringList &lines, const QString &section, const QString &key) {
    QString current;
    int found = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = stripComment(lines[i]);
        if (isSection(line))
            current = sectionOf(line);
        else if (current == section && !isSkippable(line) && line.indexOf('=') > 0 && keyOf(line) == key)
            found = i;
    }
    return found;
}

// Swaps the value but keeps the key's spelling and any trailing comment.
QString replaceValue(const QString &line, const QString &value) {
    const QRegularExpressionMatch comment = inlineComment().match(line);
    const QString tail = comment.hasMatch() ? comment.captured(0) : QString();
    return line.left(line.indexOf('=') + 1) + value + tail;
}

QStringList insertKey(QStringList lines, const QString &section, const QString &entry) {
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = stripComment(lines[i]);
        if (isSection(line) && sectionOf(line) == section) {
            lines.insert(i + 1, entry);
            return lines;
        }
    }
    lines << QString() << '[' + section + ']' << entry;
    return lines;
}

// The file text with one value changed and every other line left alone.
QString setIniValue(const QString &text, const QString &section, const QString &key, const QString &value) {
    QStringList lines = text.split('\n');
    const int index = findKeyLine(lines, section, key);
    if (index < 0)
        return insertKey(lines, section, key + '=' + value).join('\n');
    lines[index] = replaceValue(lines[index], value);
    return lines.join('\n');
}

// Writes beside the target and renames over it, so a crash or a full disk
// never leaves a half-written config. Returns an error sentence or nothing.
QString writeAtomically(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(text.toUtf8());
        if (file.commit())
            return {};
    }
    return QStringLiteral("Couldn't save %1: %2").arg(path, file.errorString());
}

// Empty when the file can't be read, which is different from an empty file:
// saving over a file we couldn't read would throw away the user's settings.
std::optional<QString> readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

// The text to edit when saving: the file, or the defaults when it's missing
// or blank, so a save never leaves a config with only one key in it.
std::optional<QString> editableText(const QString &path) {
    const std::optional<QString> text =
        QFileInfo::exists(path) ? readText(path) : QString::fromUtf8(DefaultConfig);
    if (text && text->trimmed().isEmpty())
        return QString::fromUtf8(DefaultConfig);
    return text;
}

// Keys look like "look/colors"; a key without a section is skipped rather
// than written somewhere the reader would never look.
QString setIniValues(QString text, const QMap<QString, QString> &values) {
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const qsizetype slash = it.key().indexOf('/');
        if (slash > 0)
            text = setIniValue(text, it.key().left(slash), it.key().mid(slash + 1), it.value());
    }
    return text;
}

QString unreadable(const QString &path) {
    return QStringLiteral("Can't read %1; keeping the current settings").arg(path);
}

// "look/gap" reads as "[look] gap" in messages, matching the file.
QString where(const QString &key) {
    const qsizetype slash = key.indexOf('/');
    return QStringLiteral("spectra.conf: [%1] %2").arg(key.left(slash), key.mid(slash + 1));
}

double readNumber(const Ini &ini, const QString &key, double fallback, double min, double max,
                  QStringList &errors) {
    if (!ini.contains(key))
        return fallback;
    bool ok = false;
    const double value = ini.value(key).toDouble(&ok);
    if (ok && std::isfinite(value) && value >= min && value <= max)
        return value;
    errors << QStringLiteral("%1 should be a number from %2 to %3").arg(where(key)).arg(min).arg(max);
    return fallback;
}

int readInt(const Ini &ini, const QString &key, int fallback, int min, int max, QStringList &errors) {
    if (!ini.contains(key))
        return fallback;
    bool ok = false;
    const int value = ini.value(key).toInt(&ok);
    if (ok && value >= min && value <= max)
        return value;
    errors << QStringLiteral("%1 should be a whole number from %2 to %3").arg(where(key)).arg(min).arg(max);
    return fallback;
}

int readFftSize(const Ini &ini, int fallback, QStringList &errors) {
    const QString key = QStringLiteral("analysis/fft_size");
    const int value = readInt(ini, key, fallback, 512, 16384, errors);
    if ((value & (value - 1)) == 0)
        return value;
    errors << QStringLiteral("%1 should be a power of two from 512 to 16384").arg(where(key));
    return fallback;
}

bool readBool(const Ini &ini, const QString &key, bool fallback, QStringList &errors) {
    static const QStringList yes = {"true", "yes", "on", "1"};
    static const QStringList no = {"false", "no", "off", "0"};
    const QString value = ini.value(key).toLower();
    if (!ini.contains(key))
        return fallback;
    if (yes.contains(value) || no.contains(value))
        return yes.contains(value);
    errors << QStringLiteral("%1 should be true or false").arg(where(key));
    return fallback;
}

QColor readColor(const Ini &ini, const QString &key, const QColor &fallback, QStringList &errors) {
    if (!ini.contains(key))
        return fallback;
    const QColor color = QColor::fromString(ini.value(key));
    if (color.isValid())
        return color;
    errors << QStringLiteral("%1 should be a color like #000000").arg(where(key));
    return fallback;
}

QStringList splitColors(const QString &value) {
    QStringList colors;
    for (const QString &part : value.split(',', Qt::SkipEmptyParts))
        colors << part.trimmed();
    return colors;
}

QStringList readColors(const Ini &ini, QStringList &errors) {
    const QString key = QStringLiteral("look/colors");
    if (!ini.contains(key))
        return DefaultColors;
    const QStringList colors = splitColors(ini.value(key));
    const bool valid = std::all_of(colors.begin(), colors.end(),
                                   [](const QString &c) { return QColor::fromString(c).isValid(); });
    if (valid && !colors.isEmpty())
        return colors;
    errors << QStringLiteral("%1 should be a comma-separated list of colors like #ff0000").arg(where(key));
    return DefaultColors;
}

// One of a fixed set of words, case-insensitive.
QString readChoice(const Ini &ini, const QString &key, const QStringList &choices, QStringList &errors) {
    const QString value = ini.value(key).trimmed().toLower();
    if (!ini.contains(key) || choices.contains(value))
        return ini.contains(key) ? value : choices.first();
    errors << QStringLiteral("%1 should be %2").arg(where(key), choices.join(QStringLiteral(", ")));
    return choices.first();
}

QString readMode(const Ini &ini, QStringList &errors) {
    return readChoice(ini, QStringLiteral("look/mode"), {"spectrum", "vertical"}, errors);
}

config::Capture readCapture(const Ini &ini, QStringList &errors) {
    config::Capture c;
    c.target = ini.value(QStringLiteral("capture/target")).trimmed();
    c.sleepAfter = readNumber(ini, QStringLiteral("capture/sleep_after"), c.sleepAfter, 0.0, 3600.0, errors);
    c.autoDelay = ini.value(QStringLiteral("analysis/delay"), QStringLiteral("auto")).trimmed().toLower() == "auto";
    return c;
}

config::Wallpaper readWallpaper(const Ini &ini, QStringList &errors) {
    config::Wallpaper w;
    w.height = readNumber(ini, QStringLiteral("wallpaper/height"), w.height, 0.05, 1.0, errors);
    w.opacity = readNumber(ini, QStringLiteral("wallpaper/opacity"), w.opacity, 0.0, 1.0, errors);
    w.transparent = readBool(ini, QStringLiteral("wallpaper/transparent"), w.transparent, errors);
    w.layer = readChoice(ini, QStringLiteral("wallpaper/layer"), {"bottom", "background"}, errors);
    w.monitor = ini.value(QStringLiteral("wallpaper/monitor")).trimmed();
    return w;
}

Look readLook(const Ini &ini, QStringList &errors) {
    Look look;
    look.background = readColor(ini, QStringLiteral("look/background"), look.background, errors);
    look.colors = readColors(ini, errors);
    look.mode = readMode(ini, errors);
    look.peaks = readBool(ini, QStringLiteral("look/peaks"), look.peaks, errors);
    look.glow = readBool(ini, QStringLiteral("look/glow"), look.glow, errors);
    look.gap = readNumber(ini, QStringLiteral("look/gap"), look.gap, 0.0, 0.9, errors);
    return look;
}

// Pairs that only make sense in order fall back together, so a typo in one
// can't leave an empty or inverted range.
dsp::Tuning checkRanges(dsp::Tuning tuning, QStringList &errors) {
    const dsp::Tuning defaults;
    if (tuning.minHz >= tuning.maxHz) {
        errors << QStringLiteral("spectra.conf: [analysis] min_hz should be below max_hz");
        tuning.minHz = defaults.minHz;
        tuning.maxHz = defaults.maxHz;
    }
    if (tuning.floorDb >= tuning.ceilingDb) {
        errors << QStringLiteral("spectra.conf: [analysis] floor_db should be below ceiling_db");
        tuning.floorDb = defaults.floorDb;
        tuning.ceilingDb = defaults.ceilingDb;
    }
    return tuning;
}

dsp::Tuning readTuning(const Ini &ini, QStringList &errors) {
    const auto number = [&](const char *key, float fallback, double min, double max) {
        return float(readNumber(ini, QStringLiteral("analysis/") + key, fallback, min, max, errors));
    };
    dsp::Tuning t;
    t.bands = readInt(ini, QStringLiteral("analysis/bands"), t.bands, 8, 256, errors);
    t.fftSize = readFftSize(ini, t.fftSize, errors);
    t.minHz = number("min_hz", t.minHz, 1.0, MaxHz);
    t.maxHz = number("max_hz", t.maxHz, 1.0, MaxHz);
    // dsp clamps silence at -120 dB, so a floor below that would draw silence
    // as signal.
    t.floorDb = number("floor_db", t.floorDb, -120.0, 20.0);
    t.ceilingDb = number("ceiling_db", t.ceilingDb, -120.0, 20.0);
    t.attackSeconds = number("attack", t.attackSeconds, 0.001, 10.0);
    t.decaySeconds = number("decay", t.decaySeconds, 0.001, 10.0);
    t.peakFallPerSecond = number("peak_fall", t.peakFallPerSecond, 0.01, 100.0);
    // "auto" leaves the delay at 0 here; main.cpp adds the measured latency.
    if (ini.value(QStringLiteral("analysis/delay")).trimmed().toLower() != "auto")
        t.delaySeconds = number("delay", t.delaySeconds, 0.0, 2.0);
    return checkRanges(t, errors);
}

QList<QColor> toStops(const QStringList &colors) {
    QList<QColor> stops;
    for (const QString &c : colors)
        stops << QColor::fromString(c);
    return stops;
}

QColor mix(const QColor &a, const QColor &b, double f) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * f),
                            float(a.greenF() + (b.greenF() - a.greenF()) * f),
                            float(a.blueF() + (b.blueF() - a.blueF()) * f),
                            float(a.alphaF() + (b.alphaF() - a.alphaF()) * f));
}

QString defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/spectra/spectra.conf");
}

} // namespace

QHash<QString, QString> config::parseIni(const QString &text) {
    return ::parseIni(text);
}

Settings::Settings(const QString &path, QObject *parent)
    : QObject(parent), m_path(path.isEmpty() ? defaultPath() : path), m_background(Qt::black),
      m_colors(DefaultColors), m_stops(toStops(DefaultColors)), m_mode(QStringLiteral("spectrum")) {
    // Editors save in bursts (write, rename, chmod), so wait for the burst to
    // end before reading, or we'd parse a half-written file.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(100);
    connect(&m_debounce, &QTimer::timeout, this, &Settings::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    ensureFile();
    reload();
}

void Settings::ensureFile() {
    if (!QFileInfo::exists(m_path))
        m_error = writeAtomically(m_path, QString::fromUtf8(DefaultConfig));
}

void Settings::reload() {
    watch();
    // While an editor swaps the file there's a moment with no file at all;
    // keep the current values and wait for it to come back.
    if (!QFileInfo::exists(m_path))
        return;
    const std::optional<QString> text = readText(m_path);
    if (text)
        apply(*text);
    else
        m_error = unreadable(m_path);
    emit changed();
}

void Settings::apply(const QString &text) {
    QStringList errors;
    const Ini ini = parseIni(text);
    const Look look = readLook(ini, errors);
    m_tuning = readTuning(ini, errors);
    m_background = look.background;
    m_colors = look.colors;
    m_stops = toStops(look.colors);
    m_mode = look.mode;
    m_peaks = look.peaks;
    m_glow = look.glow;
    m_layout = readChoice(ini, QStringLiteral("look/layout"), Layouts, errors);
    m_capture = readCapture(ini, errors);
    m_wallpaper = readWallpaper(ini, errors);
    m_gap = look.gap;
    m_error = errors.join('\n');
}

// Editors and our own saves replace the file, which ends the old inotify
// watch, so the file is re-added every time. The directory watch catches the
// file coming back after it was briefly gone.
void Settings::watch() {
    m_watcher.removePath(m_path);
    if (QFileInfo::exists(m_path))
        m_watcher.addPath(m_path);
    const QString dir = QFileInfo(m_path).absolutePath();
    if (!m_watcher.directories().contains(dir) && QFileInfo::exists(dir))
        m_watcher.addPath(dir);
}

void Settings::save(const QString &section, const QString &key, const QString &value) {
    const std::optional<QString> base = editableText(m_path);
    if (!base) {
        m_error = unreadable(m_path);
        return;
    }
    const QString error = writeAtomically(m_path, setIniValue(*base, section, key, value));
    if (!error.isEmpty())
        m_error = error;
}

bool Settings::setValues(const QMap<QString, QString> &values) {
    const std::optional<QString> base = editableText(m_path);
    const QString text = base ? setIniValues(*base, values) : QString();
    const QString error = base ? writeAtomically(m_path, text) : unreadable(m_path);
    if (error.isEmpty())
        apply(text);
    else
        m_error = error;
    emit changed();
    return error.isEmpty();
}

QString Settings::path() const { return m_path; }
QColor Settings::background() const { return m_background; }
QStringList Settings::colors() const { return m_colors; }
QString Settings::colorMode() const { return m_mode; }

void Settings::setColorMode(const QString &mode) {
    const QString value = mode.trimmed().toLower();
    if ((value != "spectrum" && value != "vertical") || value == m_mode)
        return;
    m_mode = value;
    save(QStringLiteral("look"), QStringLiteral("mode"), value);
    emit changed();
}

bool Settings::showPeaks() const { return m_peaks; }

void Settings::setShowPeaks(bool show) {
    if (show == m_peaks)
        return;
    m_peaks = show;
    save(QStringLiteral("look"), QStringLiteral("peaks"), show ? QStringLiteral("true") : QStringLiteral("false"));
    emit changed();
}

bool Settings::glow() const { return m_glow; }

void Settings::setGlow(bool glow) {
    if (glow == m_glow)
        return;
    m_glow = glow;
    save(QStringLiteral("look"), QStringLiteral("glow"), glow ? QStringLiteral("true") : QStringLiteral("false"));
    emit changed();
}

const QStringList Settings::Layouts = {"bars", "mirrored", "radial", "stereo", "waveform"};

QString Settings::layout() const { return m_layout; }

void Settings::setLayout(const QString &layout) {
    const QString value = layout.trimmed().toLower();
    if (!Layouts.contains(value) || value == m_layout)
        return;
    m_layout = value;
    save(QStringLiteral("look"), QStringLiteral("layout"), value);
    emit changed();
}

config::Capture Settings::capture() const { return m_capture; }
config::Wallpaper Settings::wallpaper() const { return m_wallpaper; }

double Settings::barGap() const { return m_gap; }
QString Settings::error() const { return m_error; }
dsp::Tuning Settings::tuning() const { return m_tuning; }

QColor Settings::colorAt(double t) const {
    if (m_stops.isEmpty())
        return Qt::white;
    if (m_stops.size() == 1 || !std::isfinite(t))
        return m_stops.first();
    const double position = std::clamp(t, 0.0, 1.0) * double(m_stops.size() - 1);
    const int index = std::min(int(position), int(m_stops.size()) - 2);
    return mix(m_stops[index], m_stops[index + 1], position - index);
}
