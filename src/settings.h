#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "dsp.h"

// Config sections beyond look and analysis, grouped so they travel together.
namespace config {

struct Capture {
    QString target;          // an app's name to show only its sound; empty = everything
    double sleepAfter = 5.0; // seconds of silence before capture pauses; 0 = never
    bool autoDelay = true;   // delay=auto: follow the output's reported latency
};

struct Wallpaper {
    double height = 0.35;    // fraction of the screen the bars fill, from the bottom
    double opacity = 0.8;
    bool transparent = true; // show the desktop wallpaper behind the bars
    QString layer = QStringLiteral("bottom"); // "bottom" (above the wallpaper) or "background"
    QString monitor;         // output name like DP-1; empty = the compositor's choice
};

// "section/key" to its raw value, with comments stripped: the file the way
// Settings reads it. When a key appears twice the last one wins.
QHash<QString, QString> parseIni(const QString &text);

} // namespace config

// The user's look and tuning, from ~/.config/spectra/spectra.conf (INI).
// The file is written with defaults on first run so there is something to
// edit, and watched so edits show up live without a restart.
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    // Gradient stops from low to high: across the bands in "spectrum" mode,
    // bottom to top of each bar in "vertical" mode.
    Q_PROPERTY(QStringList colors READ colors NOTIFY changed)
    Q_PROPERTY(QString colorMode READ colorMode WRITE setColorMode NOTIFY changed)
    Q_PROPERTY(bool showPeaks READ showPeaks WRITE setShowPeaks NOTIFY changed)
    Q_PROPERTY(bool glow READ glow WRITE setGlow NOTIFY changed)
    // bars, mirrored, radial, stereo or waveform.
    Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY changed)
    Q_PROPERTY(QString captureTarget READ captureTarget NOTIFY changed)
    Q_PROPERTY(double sleepAfter READ sleepAfter NOTIFY changed)
    Q_PROPERTY(bool autoDelay READ autoDelay NOTIFY changed)
    Q_PROPERTY(double wallpaperHeight READ wallpaperHeight NOTIFY changed)
    Q_PROPERTY(double wallpaperOpacity READ wallpaperOpacity NOTIFY changed)
    Q_PROPERTY(bool wallpaperTransparent READ wallpaperTransparent NOTIFY changed)
    Q_PROPERTY(double barGap READ barGap NOTIFY changed) // fraction of bar width, 0..0.9
    Q_PROPERTY(QString error READ error NOTIFY changed)  // empty, or what's wrong with the file

public:
    // An empty path means ~/.config/spectra/spectra.conf.
    explicit Settings(const QString &path = {}, QObject *parent = nullptr);

    QString path() const;
    QColor background() const;
    QStringList colors() const;
    QString colorMode() const;
    void setColorMode(const QString &mode); // "spectrum" or "vertical"; saved to the file
    bool showPeaks() const;
    void setShowPeaks(bool show);           // saved to the file
    bool glow() const;
    void setGlow(bool glow);                // saved to the file
    QString layout() const;
    void setLayout(const QString &layout);  // one of Layouts; saved to the file
    static const QStringList Layouts;

    config::Capture capture() const;
    QString captureTarget() const { return capture().target; }
    double sleepAfter() const { return capture().sleepAfter; }
    bool autoDelay() const { return capture().autoDelay; }

    config::Wallpaper wallpaper() const;
    double wallpaperHeight() const { return wallpaper().height; }
    double wallpaperOpacity() const { return wallpaper().opacity; }
    bool wallpaperTransparent() const { return wallpaper().transparent; }
    double barGap() const;
    QString error() const;
    dsp::Tuning tuning() const;

    // The gradient color at t in 0..1, interpolated between `colors`.
    Q_INVOKABLE QColor colorAt(double t) const;

    // Sets several "section/key" values in one atomic write, keeping every
    // other line and comment, then reads the result back and emits changed.
    // False, with the reason in error, when the file can't be read or saved.
    bool setValues(const QMap<QString, QString> &values);

signals:
    void changed();

private:
    void ensureFile();
    void reload();
    void apply(const QString &text);
    void watch();
    void save(const QString &section, const QString &key, const QString &value);

    QString m_path;
    QColor m_background;
    QStringList m_colors;
    QList<QColor> m_stops; // m_colors parsed once, so colorAt stays cheap
    QString m_mode;
    bool m_peaks = true;
    bool m_glow = false;
    QString m_layout = QStringLiteral("bars");
    config::Capture m_capture;
    config::Wallpaper m_wallpaper;
    double m_gap = 0.25;
    dsp::Tuning m_tuning;
    QString m_error;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
