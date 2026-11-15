#include "presets.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <optional>

#include "settings.h"

namespace {

// The keys a preset may carry, in the order a saved preset file lists them.
// Layout is left out on purpose: a preset changes how things look, not which
// picture you chose, and capture or wallpaper keys aren't looks at all.
const QStringList LookKeys = {"background", "colors", "mode", "peaks", "glow", "gap"};
const QStringList AnalysisKeys = {"bands",    "fft_size", "min_hz", "max_hz",    "floor_db",
                                  "ceiling_db", "attack", "decay",  "peak_fall", "delay"};

bool isPresetKey(const QString &key) {
    const qsizetype slash = key.indexOf('/');
    const QString section = key.left(slash);
    const QString name = key.mid(slash + 1);
    return (section == "look" && LookKeys.contains(name)) ||
           (section == "analysis" && AnalysisKeys.contains(name));
}

// Built-ins set only look keys, and every look key that differs between
// them, so switching from one to another never leaves a stray glow behind.
// Gap is left to the user because it's about bar density, not color.
Preset look(const QString &name, const QString &colors, const QString &mode, bool peaks, bool glow) {
    const auto flag = [](bool on) { return on ? QStringLiteral("true") : QStringLiteral("false"); };
    return {name,
            true,
            {{"look/background", "#000000"},
             {"look/colors", colors},
             {"look/mode", mode},
             {"look/peaks", flag(peaks)},
             {"look/glow", flag(glow)}}};
}

const QList<Preset> &builtIns() {
    static const QList<Preset> presets = {
        look("Rainbow", "#ff0000, #ffff00, #00ff00, #00ffff, #0000ff, #ff00ff", "spectrum", true, false),
        look("Neon", "#ff00ff, #8a2be2, #00e5ff", "spectrum", true, true),
        look("Sunset", "#3b0a5c, #c2187a, #ff7a1a, #ffd23f", "spectrum", true, false),
        look("Ocean", "#0a1f5c, #0f8b8d, #5ff5ff", "vertical", true, false),
        look("Fire", "#6b0000, #ff6a00, #ffe14d", "vertical", true, true),
        look("Mono", "#3a3a3a, #8a8a8a, #ffffff", "vertical", false, false),
    };
    return presets;
}

bool isBuiltInName(const QString &name) {
    return std::any_of(builtIns().begin(), builtIns().end(),
                       [&](const Preset &p) { return p.name.compare(name, Qt::CaseInsensitive) == 0; });
}

// Names become filenames, so anything that could leave the presets folder
// or hide the file is refused. Built-in names are refused in any case, so
// "neon" can't sit next to "Neon" in the menu.
bool isValidName(const QString &name) {
    return !name.isEmpty() && !name.contains('/') && !name.startsWith('.') && !isBuiltInName(name);
}

QStringList splitColors(const QString &value) {
    QStringList colors;
    for (const QString &part : value.split(',', Qt::SkipEmptyParts))
        colors << part.trimmed();
    return colors;
}

QString boolText(bool on) {
    return on ? QStringLiteral("true") : QStringLiteral("false");
}

QString colorText(const QColor &color) {
    return color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
}

QMap<QString, QString> currentLook(const Settings &settings) {
    return {{"look/background", colorText(settings.background())},
            {"look/colors", settings.colors().join(QStringLiteral(", "))},
            {"look/mode", settings.colorMode()},
            {"look/peaks", boolText(settings.showPeaks())},
            {"look/glow", boolText(settings.glow())},
            {"look/gap", QString::number(settings.barGap())}};
}

QMap<QString, QString> currentTuning(const Settings &settings) {
    const dsp::Tuning t = settings.tuning();
    const auto number = [](float value) { return QString::number(double(value)); };
    return {{"analysis/bands", QString::number(t.bands)},
            {"analysis/fft_size", QString::number(t.fftSize)},
            {"analysis/min_hz", number(t.minHz)},
            {"analysis/max_hz", number(t.maxHz)},
            {"analysis/floor_db", number(t.floorDb)},
            {"analysis/ceiling_db", number(t.ceilingDb)},
            {"analysis/attack", number(t.attackSeconds)},
            {"analysis/decay", number(t.decaySeconds)},
            {"analysis/peak_fall", number(t.peakFallPerSecond)},
            {"analysis/delay", settings.autoDelay() ? QStringLiteral("auto") : number(t.delaySeconds)}};
}

// What's on screen, read back from Settings after it validated the file, so
// a saved preset never carries a value Settings had to throw away.
QMap<QString, QString> currentValues(const Settings &settings) {
    QMap<QString, QString> values = currentLook(settings);
    values.insert(currentTuning(settings));
    return values;
}

bool sameColors(const QString &a, const QString &b) {
    QList<QColor> left;
    QList<QColor> right;
    for (const QString &c : splitColors(a))
        left << QColor::fromString(c);
    for (const QString &c : splitColors(b))
        right << QColor::fromString(c);
    return left == right;
}

QString normalWord(const QString &value) {
    const QString word = value.trimmed().toLower();
    if (word == "yes" || word == "on")
        return QStringLiteral("true");
    if (word == "no" || word == "off")
        return QStringLiteral("false");
    return word;
}

// Compares what a value means rather than how it's spelled: "#FF0000" is
// "#ff0000", and 0.02 survives the round trip through float in dsp::Tuning.
bool sameValue(const QString &key, const QString &a, const QString &b) {
    if (key == "look/colors")
        return sameColors(a, b);
    if (key == "look/background")
        return QColor::fromString(a.trimmed()) == QColor::fromString(b.trimmed());
    bool numberA = false;
    bool numberB = false;
    const double x = a.toDouble(&numberA);
    const double y = b.toDouble(&numberB);
    if (numberA && numberB)
        return std::abs(x - y) <= 1e-4 * std::max(1.0, std::abs(x));
    return normalWord(a) == normalWord(b);
}

bool matches(const Preset &preset, const QMap<QString, QString> &current) {
    for (auto it = preset.values.cbegin(); it != preset.values.cend(); ++it) {
        if (!sameValue(it.key(), it.value(), current.value(it.key())))
            return false;
    }
    return true;
}

// A hand-written preset may hold anything; only preset keys are kept, so
// applying it can't change the layout, the capture target or the wallpaper.
Preset parsePreset(const QString &name, const QString &text) {
    Preset preset{name, false, {}};
    const QHash<QString, QString> ini = config::parseIni(text);
    for (auto it = ini.cbegin(); it != ini.cend(); ++it) {
        if (isPresetKey(it.key()))
            preset.values.insert(it.key(), it.value());
    }
    return preset;
}

QString formatSection(const QString &section, const QStringList &keys, const QMap<QString, QString> &values) {
    QString text = '[' + section + "]\n";
    for (const QString &key : keys)
        text += key + '=' + values.value(section + '/' + key) + '\n';
    return text;
}

QString formatPreset(const QMap<QString, QString> &values) {
    return QStringLiteral("; A spectra preset. Applying it writes these keys into spectra.conf.\n\n") +
           formatSection(QStringLiteral("look"), LookKeys, values) + '\n' +
           formatSection(QStringLiteral("analysis"), AnalysisKeys, values);
}

std::optional<QString> readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

bool writeAtomically(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write(text.toUtf8());
    return file.commit();
}

QList<Preset> readUserPresets(const QString &directory) {
    QList<Preset> presets;
    const QFileInfoList files = QDir(directory).entryInfoList({"*.conf"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files) {
        const QString name = file.fileName().chopped(int(qstrlen(".conf")));
        const std::optional<QString> text = readText(file.filePath());
        if (text && isValidName(name))
            presets << parsePreset(name, *text);
    }
    return presets;
}

QVariantMap toVariant(const Preset &preset) {
    return {{"name", preset.name},
            {"colors", splitColors(preset.values.value(QStringLiteral("look/colors")))},
            {"builtIn", preset.builtIn}};
}

QString defaultDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/spectra/presets");
}

} // namespace

Presets::Presets(Settings *settings, const QString &directory, QObject *parent)
    : QObject(parent), m_settings(settings), m_directory(directory.isEmpty() ? defaultDirectory() : directory) {
    // Presets dropped into the folder by hand show up without a restart.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Presets::loadUserPresets);
    loadUserPresets();
    followSettings();
}

QList<Preset> Presets::all() const {
    return builtIns() + m_user;
}

void Presets::loadUserPresets() {
    watchDirectory();
    m_user = readUserPresets(m_directory);
    const QList<Preset> presets = all();
    const bool currentGone = std::none_of(presets.begin(), presets.end(),
                                          [&](const Preset &p) { return p.name == m_current; });
    if (currentGone)
        setCurrent({});
    emit listChanged();
}

// The folder only exists once a preset has been saved, so this is called
// again after every save.
void Presets::watchDirectory() {
    if (QFileInfo(m_directory).isDir() && !m_watcher.directories().contains(m_directory))
        m_watcher.addPath(m_directory);
}

// At startup the first preset the config already matches counts as current,
// so the menu outlines Rainbow on a fresh install. After that, any change
// that makes the config stop matching clears it.
void Presets::followSettings() {
    const QMap<QString, QString> now = currentValues(*m_settings);
    for (const Preset &preset : all()) {
        if (matches(preset, now)) {
            setCurrent(preset.name);
            break;
        }
    }
    connect(m_settings, &Settings::changed, this, [this] {
        const QList<Preset> presets = all();
        const auto it = std::find_if(presets.begin(), presets.end(),
                                     [&](const Preset &p) { return p.name == m_current; });
        if (it != presets.end() && !matches(*it, currentValues(*m_settings)))
            setCurrent({});
    });
}

void Presets::setCurrent(const QString &name) {
    if (name == m_current)
        return;
    m_current = name;
    emit currentChanged();
}

QVariantList Presets::list() const {
    QVariantList list;
    for (const Preset &preset : all())
        list << toVariant(preset);
    return list;
}

QString Presets::current() const { return m_current; }

// current is set before the write, because the write emits changed and the
// check that clears current has to see this preset, not the previous one.
void Presets::apply(const QString &name) {
    const QList<Preset> presets = all();
    const auto it = std::find_if(presets.begin(), presets.end(), [&](const Preset &p) { return p.name == name; });
    if (it == presets.end())
        return;
    const QString previous = m_current;
    setCurrent(name);
    if (!m_settings->setValues(it->values))
        setCurrent(previous);
}

void Presets::applyIndex(int index) {
    const QList<Preset> presets = all();
    if (index >= 0 && index < presets.size())
        apply(presets[index].name);
}

bool Presets::saveCurrent(const QString &name) {
    const QString clean = name.trimmed();
    if (!isValidName(clean))
        return false;
    const QString path = m_directory + '/' + clean + QStringLiteral(".conf");
    if (!writeAtomically(path, formatPreset(currentValues(*m_settings))))
        return false;
    loadUserPresets();
    setCurrent(clean);
    return true;
}

bool Presets::remove(const QString &name) {
    const bool isUser = std::any_of(m_user.begin(), m_user.end(), [&](const Preset &p) { return p.name == name; });
    if (!isUser || !QFile::remove(m_directory + '/' + name + QStringLiteral(".conf")))
        return false;
    loadUserPresets();
    return true;
}
