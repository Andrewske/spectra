#include "dsp.h"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace dsp {

namespace {

constexpr float SilenceDb = -120.0f;
constexpr double Pi = 3.14159265358979323846;

// An fftw plan with the buffers it was planned on. Planning is slow and not
// thread-safe, so each size is planned once and kept for the life of the
// process; everything calls in from the GUI thread.
struct FftPlan {
    float *in = nullptr;
    fftwf_complex *out = nullptr;
    fftwf_plan plan = nullptr;
};

FftPlan makePlan(int size) {
    FftPlan p;
    p.in = fftwf_alloc_real(size);
    p.out = fftwf_alloc_complex(size / 2 + 1);
    // ESTIMATE plans instantly and leaves the buffers alone; MEASURE would
    // stall the first frame after every fft_size change for little gain.
    p.plan = fftwf_plan_dft_r2c_1d(size, p.in, p.out, FFTW_ESTIMATE);
    return p;
}

const FftPlan &planFor(int size) {
    static std::unordered_map<int, FftPlan> cache;
    auto it = cache.find(size);
    if (it == cache.end())
        it = cache.emplace(size, makePlan(size)).first;
    return it->second;
}

// Adds scale * |X|^2 of one channel into `power`; two calls at 0.5 average L and R.
void addChannelPower(QVector<float> &power, const FftPlan &p, const QVector<float> &interleaved,
                     const QVector<float> &window, int channel, float scale) {
    const int size = window.size();
    for (int i = 0; i < size; ++i)
        p.in[i] = interleaved[2 * i + channel] * window[i];
    fftwf_execute(p.plan);
    for (int k = 0; k < power.size(); ++k)
        power[k] += scale * (p.out[k][0] * p.out[k][0] + p.out[k][1] * p.out[k][1]);
}

// Total one-sided power a full-scale sine puts into its main lobe with a
// periodic Hann window. The peak bin has magnitude sum(w)/2 = N/4 and its two
// neighbours half that, so the lobe holds (N/4)^2 * 1.5 = 3N^2/32. That 1.5
// is Hann's noise bandwidth in bins; it doesn't depend on where the sine sits.
double fullScaleSinePower(int fftSize) {
    const double n = fftSize;
    return 3.0 * n * n / 32.0;
}

float bandPower(const QVector<float> &power, const Band &band) {
    double sum = 0.0;
    const int last = std::min<int>(band.lastBin, power.size() - 1);
    for (int k = std::max(0, band.firstBin); k <= last; ++k)
        sum += power[k];
    return float(sum);
}

// The first bin whose center frequency is at or above hz.
int binAtOrAbove(double hz, double binHz) {
    return int(std::ceil(hz / binHz - 1e-9));
}

Band makeBand(int first, int last, double binHz) {
    // Geometric center of the frequencies the bins actually cover, so the
    // label matches what the bar shows even after a band was widened.
    const double lo = std::max(0.5, first - 0.5) * binHz;
    const double hi = (last + 0.5) * binHz;
    return Band{first, last, float(std::sqrt(lo * hi))};
}

} // namespace

QVector<float> hann(int size) {
    // Periodic Hann (divide by N, not N - 1): the right choice for spectral
    // analysis, and it makes sum(w) = N/2 and sum(w^2) = 3N/8 exactly.
    QVector<float> w(std::max(0, size));
    for (int i = 0; i < w.size(); ++i)
        w[i] = float(0.5 - 0.5 * std::cos(2.0 * Pi * i / size));
    return w;
}

QVector<float> stereoPower(const QVector<float> &interleaved, const QVector<float> &window) {
    const int size = window.size();
    QVector<float> power(size / 2 + 1, 0.0f);
    if (size < 2 || interleaved.size() != size * 2)
        return power;
    const FftPlan &p = planFor(size);
    addChannelPower(power, p, interleaved, window, 0, 0.5f);
    addChannelPower(power, p, interleaved, window, 1, 0.5f);
    return power;
}

QVector<float> channelPower(const QVector<float> &interleaved, const QVector<float> &window,
                            int channel) {
    const int size = window.size();
    QVector<float> power(size / 2 + 1, 0.0f);
    if (size < 2 || interleaved.size() != size * 2 || channel < 0 || channel > 1)
        return power;
    addChannelPower(power, planFor(size), interleaved, window, channel, 1.0f);
    return power;
}

QVector<float> average(const QVector<float> &a, const QVector<float> &b) {
    const int size = std::min(a.size(), b.size());
    QVector<float> out(size);
    for (int i = 0; i < size; ++i)
        out[i] = 0.5f * (a[i] + b[i]);
    return out;
}

QVector<Band> logBands(int count, float minHz, float maxHz, int sampleRate, int fftSize) {
    const int nyquistBin = fftSize / 2;
    if (count < 1 || count > nyquistBin || sampleRate <= 0 || minHz <= 0.0f || maxHz <= minHz)
        return {};
    const double binHz = double(sampleRate) / fftSize;
    const double ratio = std::pow(double(maxHz) / minHz, 1.0 / count);
    // Skip DC, and start low enough that every band still fits below Nyquist.
    int first = std::clamp(binAtOrAbove(minHz, binHz), 1, nyquistBin - count + 1);
    QVector<Band> bands;
    bands.reserve(count);
    for (int i = 0; i < count; ++i) {
        const int roomAbove = count - 1 - i;
        const int wanted = binAtOrAbove(minHz * std::pow(ratio, i + 1), binHz) - 1;
        const int last = std::clamp(wanted, first, nyquistBin - roomAbove);
        bands.append(makeBand(first, last, binHz));
        first = last + 1;
    }
    return bands;
}

QVector<float> bandDb(const QVector<float> &power, const QVector<Band> &bands, int fftSize) {
    // Summing the band (rather than averaging it) keeps a sine at 0 dB however
    // wide its band is, and makes pink noise, which is roughly what music
    // looks like, read flat across log-spaced bands.
    const double reference = fullScaleSinePower(fftSize);
    QVector<float> db(bands.size());
    for (int i = 0; i < bands.size(); ++i) {
        const double ratio = bandPower(power, bands[i]) / reference;
        const float value = ratio > 0.0 ? float(10.0 * std::log10(ratio)) : SilenceDb;
        db[i] = std::max(SilenceDb, value);
    }
    return db;
}

QVector<float> normalize(const QVector<float> &db, float floorDb, float ceilingDb) {
    const float range = std::max(1e-6f, ceilingDb - floorDb);
    QVector<float> out(db.size());
    for (int i = 0; i < db.size(); ++i)
        out[i] = std::clamp((db[i] - floorDb) / range, 0.0f, 1.0f);
    return out;
}

QVector<float> smooth(const QVector<float> &previous, const QVector<float> &target,
                      float attackSeconds, float decaySeconds, float dt) {
    const bool fresh = previous.size() != target.size();
    const float riseStep = 1.0f - std::exp(-dt / std::max(1e-6f, attackSeconds));
    const float fallStep = 1.0f - std::exp(-dt / std::max(1e-6f, decaySeconds));
    QVector<float> out(target.size());
    for (int i = 0; i < target.size(); ++i) {
        const float level = fresh ? 0.0f : previous[i];
        const float step = target[i] > level ? riseStep : fallStep;
        out[i] = level + (target[i] - level) * step;
    }
    return out;
}

QVector<float> peaks(const QVector<float> &previous, const QVector<float> &levels,
                     float fallPerSecond, float dt) {
    // A previous of the wrong size starts at the levels: caps sit on the bars.
    if (previous.size() != levels.size())
        return levels;
    QVector<float> out(levels.size());
    for (int i = 0; i < levels.size(); ++i)
        out[i] = std::max(levels[i], previous[i] - fallPerSecond * dt);
    return out;
}

QVector<float> mono(const QVector<float> &interleaved) {
    QVector<float> out(interleaved.size() / 2);
    for (int i = 0; i < out.size(); ++i)
        out[i] = 0.5f * (interleaved[2 * i] + interleaved[2 * i + 1]);
    return out;
}

double risingZeroCrossing(const QVector<float> &samples, int earliest, int latest) {
    // Searching from the newest end keeps the trace as close to now as it can be.
    const int stop = std::max(1, earliest);
    for (int i = std::min<int>(latest, samples.size() - 1); i >= stop; --i) {
        const float before = samples[i - 1];
        const float after = samples[i];
        if (before < 0.0f && after >= 0.0f)
            return (i - 1) + double(before) / (double(before) - after);
    }
    return -1.0;
}

QVector<float> resample(const QVector<float> &samples, double start, double span, int points) {
    QVector<float> out(std::max(0, points), 0.0f);
    const double step = points > 1 ? span / (points - 1) : 0.0;
    const int last = samples.size() - 1;
    for (int i = 0; i < out.size() && last > 0; ++i) {
        const double position = start + i * step;
        if (position < 0.0 || position > last)
            continue;
        // The final sample interpolates from its left neighbour with t = 1.
        const int index = std::min(int(position), last - 1);
        const float t = float(position - index);
        out[i] = samples[index] + (samples[index + 1] - samples[index]) * t;
    }
    return out;
}

float followPeak(float envelope, float peak, float releaseSeconds, float dt) {
    const float decayed = envelope * std::exp(-dt / std::max(1e-6f, releaseSeconds));
    return std::max(peak, decayed);
}

float peakOf(const QVector<float> &values) {
    float peak = 0.0f;
    for (float v : values)
        peak = std::max(peak, std::abs(v));
    return peak;
}

QVector<float> scaleWave(const QVector<float> &wave, float envelope, float floor) {
    const float gain = 1.0f / std::max({envelope, floor, 1e-6f});
    QVector<float> out(wave.size());
    for (int i = 0; i < wave.size(); ++i)
        out[i] = std::clamp(wave[i] * gain, -1.0f, 1.0f);
    return out;
}

} // namespace dsp
