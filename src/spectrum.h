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
    // Per-channel levels for the stereo layout, same bands and range as levels.
    Q_PROPERTY(QList<qreal> levelsLeft READ levelsLeft NOTIFY updated)
    Q_PROPERTY(QList<qreal> levelsRight READ levelsRight NOTIFY updated)
    Q_PROPERTY(QList<qreal> peaksLeft READ peaksLeft NOTIFY updated)
    Q_PROPERTY(QList<qreal> peaksRight READ peaksRight NOTIFY updated)
    // The latest audio as -1..1 points, oldest first, for the waveform layout.
    Q_PROPERTY(QList<qreal> waveform READ waveform NOTIFY updated)
    Q_PROPERTY(int bandCount READ bandCount NOTIFY tuningChanged)
    Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY frozenChanged)

public:
    // Returns the most recent `frames` interleaved stereo frames, oldest
    // first (Capture::latestFrames). Injectable so tests can feed sines.
    using Source = std::function<QVector<float>(int frames)>;

    explicit Spectrum(Source source, int sampleRate = 48000, QObject *parent = nullptr);

    QList<qreal> levels() const;
    QList<qreal> peaks() const;
    QList<qreal> levelsLeft() const;
    QList<qreal> levelsRight() const;
    QList<qreal> peaksLeft() const;
    QList<qreal> peaksRight() const;
    QList<qreal> waveform() const;

    // The layout being drawn (Settings::Layouts), so tick only computes what
    // that layout reads: levels and peaks for bars, mirrored and radial; the
    // per-channel lists for stereo; waveform for waveform. Lists a layout
    // doesn't read are empty, except levels and peaks, which read as zeros.
    void setLayout(const QString &layout);
    int bandCount() const;
    bool frozen() const;
    void setFrozen(bool frozen);

    dsp::Tuning tuning() const;
    void setTuning(const dsp::Tuning &tuning);

    // dtSeconds is the time since the last frame; values above 0.1 s (after
    // a hidden window or a stall) are clamped so bars don't jump.
    Q_INVOKABLE void tick(double dtSeconds);

    // Drops every level, peak and waveform point to zero at once. Capture
    // calls for this when it goes to sleep: the window stops ticking then, so
    // smoothing would otherwise leave the last loud frame on screen.
    Q_INVOKABLE void clear();

signals:
    void updated();
    void tuningChanged();
    void frozenChanged();

private:
    // What tick computes, picked from the layout name.
    enum class Output { Mono, Stereo, Waveform };
    static Output outputFor(const QString &layout);

    // Rebuilds what depends on fftSize, band layout and sample rate.
    void rebuildAnalysis();
    // Empties the outputs the current layout doesn't read and zeroes the rest.
    void resetOutputs();
    void analyze(float dt);
    int framesWanted(int delayFrames) const;
    // One per Output: analyzes the fftSize (or waveform) frames before the delay.
    void tickMono(const QVector<float> &frames, float dt);
    void tickStereo(const QVector<float> &frames, float dt);
    void tickWaveform(const QVector<float> &frames, float dt);
    // Bands, dB and normalize: a power spectrum to the 0..1 levels a bar targets.
    QVector<float> targetLevels(const QVector<float> &power) const;

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

    Output m_output = Output::Mono;
    QVector<float> m_levelsLeft;
    QVector<float> m_levelsRight;
    QVector<float> m_peaksLeft;
    QVector<float> m_peaksRight;
    QList<qreal> m_levelsLeftOut;
    QList<qreal> m_levelsRightOut;
    QList<qreal> m_peaksLeftOut;
    QList<qreal> m_peaksRightOut;
    float m_waveEnvelope = 0.0f;
    QList<qreal> m_waveformOut;
};
