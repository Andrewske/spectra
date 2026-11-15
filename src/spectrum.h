#pragma once

#include <QList>
#include <QObject>
#include <QVector>
#include <functional>

#include "dsp.h"

// The bars QML draws. QML calls tick() once per rendered frame (from a
// FrameAnimation, so it follows vsync); each tick pulls the latest audio,
// runs it through dsp, and publishes new levels and peaks.
class Spectrum : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<qreal> levels READ levels NOTIFY updated)
    Q_PROPERTY(QList<qreal> peaks READ peaks NOTIFY updated)
    Q_PROPERTY(int bandCount READ bandCount NOTIFY tuningChanged)
    Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY frozenChanged)

public:
    // Returns the most recent `frames` interleaved stereo frames, oldest
    // first (Capture::latestFrames). Injectable so tests can feed sines.
    using Source = std::function<QVector<float>(int frames)>;

    explicit Spectrum(Source source, int sampleRate = 48000, QObject *parent = nullptr);

    QList<qreal> levels() const;
    QList<qreal> peaks() const;
    int bandCount() const;
    bool frozen() const;
    void setFrozen(bool frozen);

    dsp::Tuning tuning() const;
    void setTuning(const dsp::Tuning &tuning);

    // dtSeconds is the time since the last frame; values above 0.1 s (after
    // a hidden window or a stall) are clamped so bars don't jump.
    Q_INVOKABLE void tick(double dtSeconds);

signals:
    void updated();
    void tuningChanged();
    void frozenChanged();

private:
    // Rebuilds what depends on fftSize, band layout and sample rate.
    void rebuildAnalysis();

    Source m_source;
    int m_sampleRate;
    bool m_frozen = false;
    dsp::Tuning m_tuning;
    QVector<float> m_window;
    QVector<dsp::Band> m_bands;
    // Kept as floats for dsp, and as qreal lists so QML reads don't convert.
    QVector<float> m_levels;
    QVector<float> m_peaks;
    QList<qreal> m_levelsOut;
    QList<qreal> m_peaksOut;
};
