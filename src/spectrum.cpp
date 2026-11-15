#include "spectrum.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double MaxFrameSeconds = 0.1;

// The waveform shows the newest 20 ms (one cycle of 50 Hz) as 512 points,
// about two samples per point at 48 kHz. Its start may move up to another
// 20 ms back to land on a rising zero crossing.
constexpr double WaveSpanSeconds = 0.02;
constexpr int WaveformPoints = 512;
// Auto-gain: the envelope falls slowly enough that the trace doesn't pump
// with every beat, and the floor (-26 dBFS) keeps hiss from filling the window.
constexpr float WaveReleaseSeconds = 1.5f;
constexpr float WaveFloor = 0.05f;

// The power of two nearest to n (in log terms), within 512..16384.
int nearestFftSize(int n) {
    const double exponent = std::round(std::log2(std::max(1, n)));
    return 1 << std::clamp(int(exponent), 9, 14);
}

dsp::Tuning clampFrequencies(dsp::Tuning t, int sampleRate) {
    const float nyquist = sampleRate / 2.0f;
    t.maxHz = std::clamp(t.maxHz, 2.0f, nyquist);
    t.minHz = std::clamp(t.minHz, 1.0f, t.maxHz);
    // An inverted or empty range can't be drawn; keep maxHz and show the
    // octave below it rather than refusing the whole config.
    if (t.minHz >= t.maxHz)
        t.minHz = t.maxHz / 2.0f;
    return t;
}

// Bends an out-of-range tuning into the nearest usable one, so a typo in the
// config still gives a working display.
dsp::Tuning clampTuning(dsp::Tuning t, int sampleRate) {
    t.fftSize = nearestFftSize(t.fftSize);
    t.bands = std::clamp(t.bands, 8, 256);
    t = clampFrequencies(t, sampleRate);
    if (t.ceilingDb <= t.floorDb)
        t.ceilingDb = t.floorDb + 1.0f;
    t.attackSeconds = std::max(t.attackSeconds, 0.001f);
    t.decaySeconds = std::max(t.decaySeconds, 0.001f);
    t.peakFallPerSecond = std::max(t.peakFallPerSecond, 0.0f);
    t.delaySeconds = std::clamp(t.delaySeconds, 0.0f, 2.0f);
    return t;
}

bool sameTuning(const dsp::Tuning &a, const dsp::Tuning &b) {
    return a.bands == b.bands && a.fftSize == b.fftSize && a.minHz == b.minHz
        && a.maxHz == b.maxHz && a.floorDb == b.floorDb && a.ceilingDb == b.ceilingDb
        && a.attackSeconds == b.attackSeconds && a.decaySeconds == b.decaySeconds
        && a.peakFallPerSecond == b.peakFallPerSecond && a.delaySeconds == b.delaySeconds;
}

// The oldest `count` frames of what the source returned; skipping the newest
// delayFrames is the A/V delay. A short read is padded with silence at the front.
QVector<float> oldestFrames(const QVector<float> &frames, int count, int delayFrames) {
    const int wanted = count * 2;
    const int start = frames.size() - (count + delayFrames) * 2;
    QVector<float> out(wanted, 0.0f);
    for (int i = std::max(0, -start); i < wanted && start + i < frames.size(); ++i)
        out[i] = frames[start + i];
    return out;
}

QList<qreal> toQrealList(const QVector<float> &values) {
    return QList<qreal>(values.cbegin(), values.cend());
}

int waveSpanFrames(int sampleRate) {
    return std::max(2, int(std::lround(WaveSpanSeconds * sampleRate)));
}

// Moves bar levels toward their targets and lets the peak caps follow.
void advance(QVector<float> &levels, QVector<float> &peaks, const QVector<float> &target,
             const dsp::Tuning &t, float dt) {
    levels = dsp::smooth(levels, target, t.attackSeconds, t.decaySeconds, dt);
    peaks = dsp::peaks(peaks, levels, t.peakFallPerSecond, dt);
}

} // namespace

Spectrum::Spectrum(Source source, int sampleRate, QObject *parent)
    : QObject(parent), m_source(std::move(source)), m_sampleRate(sampleRate),
      m_tuning(clampTuning(dsp::Tuning{}, sampleRate)) {
    rebuildAnalysis();
}

QList<qreal> Spectrum::levels() const { return m_levelsOut; }
QList<qreal> Spectrum::peaks() const { return m_peaksOut; }
int Spectrum::bandCount() const { return m_bands.size(); }
bool Spectrum::frozen() const { return m_frozen; }
dsp::Tuning Spectrum::tuning() const { return m_tuning; }

void Spectrum::setFrozen(bool frozen) {
    if (frozen == m_frozen)
        return;
    m_frozen = frozen;
    emit frozenChanged();
}

void Spectrum::setTuning(const dsp::Tuning &tuning) {
    const dsp::Tuning clamped = clampTuning(tuning, m_sampleRate);
    // Settings::changed also fires for color edits; skip the rebuild then.
    if (sameTuning(clamped, m_tuning))
        return;
    m_tuning = clamped;
    rebuildAnalysis();
    emit tuningChanged();
    emit updated();
}

void Spectrum::rebuildAnalysis() {
    m_window = dsp::hann(m_tuning.fftSize);
    m_bands = dsp::logBands(m_tuning.bands, m_tuning.minHz, m_tuning.maxHz, m_sampleRate,
                            m_tuning.fftSize);
    // Old levels belong to a different band layout; start the bars from zero.
    resetOutputs();
}

void Spectrum::resetOutputs() {
    const QVector<float> zeros(m_bands.size(), 0.0f);
    const bool stereo = m_output == Output::Stereo;
    m_levels = zeros;
    m_peaks = zeros;
    m_levelsLeft = m_levelsRight = m_peaksLeft = m_peaksRight = stereo ? zeros : QVector<float>{};
    m_levelsOut = m_peaksOut = toQrealList(zeros);
    m_levelsLeftOut = m_levelsRightOut = toQrealList(m_levelsLeft);
    m_peaksLeftOut = m_peaksRightOut = m_levelsLeftOut;
    m_waveEnvelope = 0.0f;
    m_waveformOut = m_output == Output::Waveform ? QList<qreal>(WaveformPoints, 0.0) : QList<qreal>{};
}

int Spectrum::framesWanted(int delayFrames) const {
    if (m_output == Output::Waveform)
        return waveSpanFrames(m_sampleRate) * 2 + delayFrames;
    return m_tuning.fftSize + delayFrames;
}

QVector<float> Spectrum::targetLevels(const QVector<float> &power) const {
    const QVector<float> db = dsp::bandDb(power, m_bands, m_tuning.fftSize);
    return dsp::normalize(db, m_tuning.floorDb, m_tuning.ceilingDb);
}

void Spectrum::tick(double dtSeconds) {
    if (m_frozen)
        return;
    const float dt = float(std::clamp(dtSeconds, 0.0, MaxFrameSeconds));
    const int delayFrames = int(std::lround(m_tuning.delaySeconds * m_sampleRate));
    const int wanted = framesWanted(delayFrames);
    const QVector<float> source = m_source ? m_source(wanted) : QVector<float>{};
    // The frames just before the delay: what is audible now.
    const int count = wanted - delayFrames;
    const QVector<float> frames = oldestFrames(source, count, delayFrames);
    if (m_output == Output::Waveform)
        tickWaveform(frames, dt);
    else if (m_output == Output::Stereo)
        tickStereo(frames, dt);
    else
        tickMono(frames, dt);
    emit updated();
}

void Spectrum::tickMono(const QVector<float> &frames, float dt) {
    advance(m_levels, m_peaks, targetLevels(dsp::stereoPower(frames, m_window)), m_tuning, dt);
    m_levelsOut = toQrealList(m_levels);
    m_peaksOut = toQrealList(m_peaks);
}

void Spectrum::tickStereo(const QVector<float> &frames, float dt) {
    const QVector<float> left = dsp::channelPower(frames, m_window, 0);
    const QVector<float> right = dsp::channelPower(frames, m_window, 1);
    advance(m_levelsLeft, m_peaksLeft, targetLevels(left), m_tuning, dt);
    advance(m_levelsRight, m_peaksRight, targetLevels(right), m_tuning, dt);
    m_levelsLeftOut = toQrealList(m_levelsLeft);
    m_levelsRightOut = toQrealList(m_levelsRight);
    m_peaksLeftOut = toQrealList(m_peaksLeft);
    m_peaksRightOut = toQrealList(m_peaksRight);
}

void Spectrum::tickWaveform(const QVector<float> &frames, float dt) {
    const int span = waveSpanFrames(m_sampleRate);
    const QVector<float> samples = dsp::mono(frames);
    // The newest rising crossing that still leaves a whole span after it;
    // without one (silence, or a tone below 50 Hz) the trace just ends now.
    double start = dsp::risingZeroCrossing(samples, 1, span);
    if (start < 0.0)
        start = span;
    const QVector<float> wave = dsp::resample(samples, start, span - 1, WaveformPoints);
    m_waveEnvelope = dsp::followPeak(m_waveEnvelope, dsp::peakOf(wave), WaveReleaseSeconds, dt);
    m_waveformOut = toQrealList(dsp::scaleWave(wave, m_waveEnvelope, WaveFloor));
}

QList<qreal> Spectrum::levelsLeft() const { return m_levelsLeftOut; }
QList<qreal> Spectrum::levelsRight() const { return m_levelsRightOut; }
QList<qreal> Spectrum::peaksLeft() const { return m_peaksLeftOut; }
QList<qreal> Spectrum::peaksRight() const { return m_peaksRightOut; }
QList<qreal> Spectrum::waveform() const { return m_waveformOut; }

Spectrum::Output Spectrum::outputFor(const QString &layout) {
    if (layout == QLatin1String("stereo"))
        return Output::Stereo;
    if (layout == QLatin1String("waveform"))
        return Output::Waveform;
    return Output::Mono;
}

void Spectrum::setLayout(const QString &layout) {
    const Output output = outputFor(layout);
    if (output == m_output)
        return;
    m_output = output;
    resetOutputs();
    emit updated();
}
