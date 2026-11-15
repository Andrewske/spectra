#pragma once

#include <QVector>

// Pure signal processing: no Qt objects, no state beyond fftw's cached plans.
// Every function takes its inputs and returns new values.
namespace dsp {

// How the spectrum is analyzed and animated. Settings fills one from the
// config file; Spectrum applies it.
struct Tuning {
    int bands = 96;
    int fftSize = 4096;          // power of two
    float minHz = 30.0f;
    float maxHz = 16000.0f;
    float floorDb = -70.0f;      // maps to an empty bar
    float ceilingDb = -10.0f;    // maps to a full bar
    float attackSeconds = 0.02f; // time constant while a bar rises
    float decaySeconds = 0.25f;  // time constant while a bar falls
    float peakFallPerSecond = 0.6f; // peak caps fall this much of full height per second
    float delaySeconds = 0.0f;   // shows audio this much older, for Bluetooth latency
};

// One output bar: an inclusive range of FFT bins.
struct Band {
    int firstBin = 0;
    int lastBin = 0;
    float centerHz = 0.0f;
};

QVector<float> hann(int size);

// Power spectrum (|X|^2, fftSize/2 + 1 bins) averaged over the left and right
// channels of interleaved stereo frames. Averaging power rather than samples
// keeps out-of-phase stereo content from cancelling. interleaved.size() must
// be window.size() * 2.
QVector<float> stereoPower(const QVector<float> &interleaved, const QVector<float> &window);

// Exactly `count` log-spaced bands between minHz and maxHz. Bands are
// ascending, never overlap, and each covers at least one bin; where the low
// end has fewer bins than bands, those bands widen rather than share bins.
// A band covers the bins whose centers fall in its log range. DC (bin 0) is
// never used. Where bins are scarcer than bands, the low bands take one bin
// each in order, which pushes the following bands upward until the log
// spacing catches up; if it never does, the top bands run past maxHz, up to
// the Nyquist bin. When minHz is so high that `count` bins don't fit above it,
// the first band starts lower instead. Returns an empty vector when it can't
// work at all: count < 1, count > fftSize / 2, or minHz >= maxHz.
QVector<Band> logBands(int count, float minHz, float maxHz, int sampleRate, int fftSize);

// Power per band in dB (10 * log10), normalized so a full-scale sine reads
// about 0 dB, and clamped at -120 dB for silence. Assumes the power came from
// a Hann window (dsp::hann). The band's bins are summed rather than averaged:
// a sine then reads 0 dB whatever the band's width (as long as the band holds
// its whole main lobe, about 3 bins), and broadband sound rises with band
// width, which offsets music's roughly pink fall-off across log bands.
QVector<float> bandDb(const QVector<float> &power, const QVector<Band> &bands, int fftSize);

// dB to 0..1 between floorDb and ceilingDb, clamped.
QVector<float> normalize(const QVector<float> &db, float floorDb, float ceilingDb);

// Moves each level toward its target with an exponential time constant:
// attack while rising, decay while falling. dt is the frame time in seconds.
// A previous of the wrong size starts from zeros.
QVector<float> smooth(const QVector<float> &previous, const QVector<float> &target,
                      float attackSeconds, float decaySeconds, float dt);

// Peak caps: jump up to the level, otherwise fall at fallPerSecond.
QVector<float> peaks(const QVector<float> &previous, const QVector<float> &levels,
                     float fallPerSecond, float dt);

} // namespace dsp
