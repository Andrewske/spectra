#include "spectrum.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double MaxFrameSeconds = 0.1;

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

// The oldest fftSize frames of what the source returned; that offset is the
// A/V delay. A short read is padded with silence at the front.
QVector<float> oldestFrames(const QVector<float> &frames, int fftSize, int delayFrames) {
    const int wanted = fftSize * 2;
    const int start = frames.size() - (fftSize + delayFrames) * 2;
    QVector<float> out(wanted, 0.0f);
    for (int i = std::max(0, -start); i < wanted && start + i < frames.size(); ++i)
        out[i] = frames[start + i];
    return out;
}

QList<qreal> toQrealList(const QVector<float> &values) {
    return QList<qreal>(values.cbegin(), values.cend());
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
    m_levels = QVector<float>(m_bands.size(), 0.0f);
    m_peaks = m_levels;
    m_levelsOut = toQrealList(m_levels);
    m_peaksOut = m_levelsOut;
}

void Spectrum::tick(double dtSeconds) {
    if (m_frozen)
        return;
    const float dt = float(std::clamp(dtSeconds, 0.0, MaxFrameSeconds));
    const int fftSize = m_tuning.fftSize;
    const int delayFrames = int(std::lround(m_tuning.delaySeconds * m_sampleRate));
    const QVector<float> frames = m_source ? m_source(fftSize + delayFrames) : QVector<float>{};
    const QVector<float> power = dsp::stereoPower(oldestFrames(frames, fftSize, delayFrames), m_window);
    const QVector<float> db = dsp::bandDb(power, m_bands, fftSize);
    const QVector<float> target = dsp::normalize(db, m_tuning.floorDb, m_tuning.ceilingDb);
    m_levels = dsp::smooth(m_levels, target, m_tuning.attackSeconds, m_tuning.decaySeconds, dt);
    m_peaks = dsp::peaks(m_peaks, m_levels, m_tuning.peakFallPerSecond, dt);
    m_levelsOut = toQrealList(m_levels);
    m_peaksOut = toQrealList(m_peaks);
    emit updated();
}
