#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

#include "dsp.h"

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
    double barGap() const;
    QString error() const;
    dsp::Tuning tuning() const;

    // The gradient color at t in 0..1, interpolated between `colors`.
    Q_INVOKABLE QColor colorAt(double t) const;

signals:
    void changed();

private:
    // TODO: owned by the ui task.
};
