#include <QtTest>

#include <cmath>
#include <memory>

#include "dsp.h"
#include "spectrum.h"

namespace {

constexpr int Rate = 48000;
constexpr double Pi = 3.14159265358979323846;

// Interleaved stereo sine; rightSign = -1 puts the right channel out of phase.
QVector<float> stereoSine(int frames, double hz, double amplitude = 1.0, float rightSign = 1.0f) {
    QVector<float> out(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const float s = float(amplitude * std::sin(2.0 * Pi * hz * i / Rate));
        out[2 * i] = s;
        out[2 * i + 1] = rightSign * s;
    }
    return out;
}

QVector<float> sineDb(int fftSize, double hz, const QVector<dsp::Band> &bands, float rightSign = 1.0f) {
    const QVector<float> power = dsp::stereoPower(stereoSine(fftSize, hz, 1.0, rightSign), dsp::hann(fftSize));
    return dsp::bandDb(power, bands, fftSize);
}

int bandContaining(const QVector<dsp::Band> &bands, double hz, int fftSize) {
    const double bin = hz * fftSize / Rate;
    for (int i = 0; i < bands.size(); ++i)
        if (bin >= bands[i].firstBin - 0.5 && bin < bands[i].lastBin + 0.5)
            return i;
    return -1;
}

int argMax(const QVector<float> &values) {
    return int(std::max_element(values.cbegin(), values.cend()) - values.cbegin());
}

// A fake capture: sine in the older part of the buffer, silence in the newest
// `silentFrames`, and it records how many frames were asked for.
struct FakeSource {
    int silentFrames = 0;
    int lastRequest = 0;
    double hz = 1000.0;
};

Spectrum::Source sourceFor(const std::shared_ptr<FakeSource> &fake) {
    return [fake](int frames) {
        fake->lastRequest = frames;
        QVector<float> out = stereoSine(frames, fake->hz);
        const int silentFrom = std::max(0, frames - fake->silentFrames);
        std::fill(out.begin() + silentFrom * 2, out.end(), 0.0f);
        return out;
    };
}

void tickFor(Spectrum &spectrum, double seconds) {
    for (double t = 0.0; t < seconds; t += 1.0 / 60.0)
        spectrum.tick(1.0 / 60.0);
}

// Every field out of range or inverted.
dsp::Tuning badTuning() {
    dsp::Tuning bad;
    bad.fftSize = 5000;
    bad.bands = 1000;
    bad.minHz = 30000;
    bad.maxHz = 90000;
    bad.floorDb = -10;
    bad.ceilingDb = -70;
    bad.attackSeconds = -1;
    bad.delaySeconds = 9;
    return bad;
}

double maxOf(const QList<qreal> &values) {
    return *std::max_element(values.cbegin(), values.cend());
}

} // namespace

class DspTests : public QObject {
    Q_OBJECT

private slots:
    void hannIsPeriodic() {
        const QVector<float> w = dsp::hann(8);
        QCOMPARE(w.size(), 8);
        QCOMPARE(w[0], 0.0f);
        QVERIFY(std::abs(w[4] - 1.0f) < 1e-6f);
        QVERIFY(std::abs(w[2] - w[6]) < 1e-6f);
    }

    void sineLandsInItsBand() {
        const QVector<dsp::Band> bands = dsp::logBands(96, 30, 16000, Rate, 4096);
        const QVector<float> db = sineDb(4096, 1000.0, bands);
        const int expected = bandContaining(bands, 1000.0, 4096);
        QVERIFY(expected >= 0);
        QCOMPARE(argMax(db), expected);
    }

    void fullScaleSineReadsZeroDb_data() {
        QTest::addColumn<int>("fftSize");
        QTest::addColumn<double>("hz");
        QTest::newRow("4096, 1 kHz") << 4096 << 1000.0;
        QTest::newRow("4096, 5 kHz") << 4096 << 5000.0;
        QTest::newRow("8192, 440 Hz") << 8192 << 440.0;
        QTest::newRow("1024, 3 kHz") << 1024 << 3000.0;
    }

    void fullScaleSineReadsZeroDb() {
        QFETCH(int, fftSize);
        QFETCH(double, hz);
        // A single wide band holds the sine's whole main lobe wherever it sits.
        const int bin = int(std::lround(hz * fftSize / Rate));
        const QVector<dsp::Band> bands{{bin - 8, bin + 8, float(hz)}};
        const float db = sineDb(fftSize, hz, bands).first();
        QVERIFY2(std::abs(db) < 1.5f, qPrintable(QString("read %1 dB").arg(db)));
    }

    void defaultBandsReadSineNearZeroDb() {
        const QVector<dsp::Band> bands = dsp::logBands(96, 30, 16000, Rate, 4096);
        const QVector<float> db = sineDb(4096, 1000.0, bands);
        QVERIFY2(std::abs(db[argMax(db)]) < 1.5f, qPrintable(QString("read %1 dB").arg(db[argMax(db)])));
    }

    void halfScaleSineReadsMinusSixDb() {
        const QVector<dsp::Band> bands{{80, 100, 1000.0f}};
        const QVector<float> power = dsp::stereoPower(stereoSine(4096, 1000.0, 0.5), dsp::hann(4096));
        QVERIFY(std::abs(dsp::bandDb(power, bands, 4096).first() + 6.02f) < 0.2f);
    }

    void silenceIsFloor() {
        const QVector<dsp::Band> bands = dsp::logBands(32, 30, 16000, Rate, 2048);
        const QVector<float> power = dsp::stereoPower(QVector<float>(4096, 0.0f), dsp::hann(2048));
        for (float db : dsp::bandDb(power, bands, 2048))
            QCOMPARE(db, -120.0f);
    }

    void outOfPhaseStereoDoesNotCancel() {
        const QVector<dsp::Band> bands = dsp::logBands(96, 30, 16000, Rate, 4096);
        const QVector<float> inPhase = sineDb(4096, 1000.0, bands, 1.0f);
        const QVector<float> outOfPhase = sineDb(4096, 1000.0, bands, -1.0f);
        const int band = argMax(inPhase);
        QVERIFY(std::abs(inPhase[band] - outOfPhase[band]) < 0.01f);
    }

    void stereoPowerRejectsMismatchedInput() {
        const QVector<float> power = dsp::stereoPower(QVector<float>(100, 1.0f), dsp::hann(512));
        QCOMPARE(power.size(), 257);
        QCOMPARE(*std::max_element(power.cbegin(), power.cend()), 0.0f);
    }

    void logBandsInvariants_data() {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("fftSize");
        QTest::addColumn<float>("minHz");
        QTest::addColumn<float>("maxHz");
        QTest::newRow("default") << 96 << 4096 << 30.0f << 16000.0f;
        QTest::newRow("512 x 128") << 128 << 512 << 30.0f << 16000.0f;
        QTest::newRow("512 x 256 (every bin)") << 256 << 512 << 30.0f << 16000.0f;
        QTest::newRow("512 x 256, high minHz") << 256 << 512 << 10000.0f << 16000.0f;
        QTest::newRow("1024 x 200") << 200 << 1024 << 20.0f << 20000.0f;
        QTest::newRow("16384 x 8") << 8 << 16384 << 30.0f << 24000.0f;
        QTest::newRow("16384 x 256") << 256 << 16384 << 20.0f << 24000.0f;
    }

    void logBandsInvariants() {
        QFETCH(int, count);
        QFETCH(int, fftSize);
        QFETCH(float, minHz);
        QFETCH(float, maxHz);
        const QVector<dsp::Band> bands = dsp::logBands(count, minHz, maxHz, Rate, fftSize);
        QCOMPARE(bands.size(), count);
        QVERIFY(bands.first().firstBin >= 1);
        QVERIFY(bands.last().lastBin <= fftSize / 2);
        for (int i = 0; i < count; ++i) {
            QVERIFY(bands[i].lastBin >= bands[i].firstBin);
            if (i > 0) {
                QCOMPARE(bands[i].firstBin, bands[i - 1].lastBin + 1);
                QVERIFY(bands[i].centerHz > bands[i - 1].centerHz);
            }
        }
    }

    void logBandsReachMaxHzWhenBinsAllow() {
        const QVector<dsp::Band> bands = dsp::logBands(96, 30, 16000, Rate, 4096);
        const double topHz = (bands.last().lastBin + 0.5) * Rate / 4096.0;
        QVERIFY(std::abs(topHz - 16000.0) < Rate / 4096.0);
        QCOMPARE(bands.first().firstBin, 3); // 35.2 Hz, the first bin center above 30 Hz
    }

    void logBandsWidenUpwardAtTheLowEnd() {
        // 512 points at 48 kHz are 93.75 Hz bins: the low bands take one bin
        // each in turn, and the top band ends up at Nyquist when every bin is used.
        const QVector<dsp::Band> bands = dsp::logBands(256, 30, 16000, Rate, 512);
        QCOMPARE(bands.first().firstBin, 1);
        QCOMPARE(bands.last().lastBin, 256);
        for (const dsp::Band &band : bands)
            QCOMPARE(band.lastBin, band.firstBin);
    }

    void logBandsRejectImpossibleRequests() {
        QVERIFY(dsp::logBands(0, 30, 16000, Rate, 4096).isEmpty());
        QVERIFY(dsp::logBands(257, 30, 16000, Rate, 512).isEmpty());
        QVERIFY(dsp::logBands(16, 1000, 500, Rate, 4096).isEmpty());
    }

    void normalizeMapsAndClamps() {
        const QVector<float> out = dsp::normalize({-80, -70, -40, -10, 0}, -70, -10);
        const QVector<float> expected{0.0f, 0.0f, 0.5f, 1.0f, 1.0f};
        for (int i = 0; i < out.size(); ++i)
            QVERIFY(std::abs(out[i] - expected[i]) < 1e-6f);
    }

    void smoothingAttacksFasterThanItDecays() {
        const QVector<float> rising = dsp::smooth({0.0f}, {1.0f}, 0.02f, 0.25f, 0.01f);
        const QVector<float> falling = dsp::smooth({1.0f}, {0.0f}, 0.02f, 0.25f, 0.01f);
        QVERIFY(rising[0] > 0.0f && rising[0] < 1.0f);
        QVERIFY(falling[0] > 0.0f && falling[0] < 1.0f);
        QVERIFY(rising[0] > 1.0f - falling[0]); // moved further up than down
        QVERIFY(std::abs(rising[0] - (1.0f - std::exp(-0.5f))) < 1e-5f);
    }

    void smoothingConverges() {
        QVector<float> level{0.0f, 1.0f};
        const QVector<float> target{0.7f, 0.2f};
        for (int i = 0; i < 600; ++i)
            level = dsp::smooth(level, target, 0.02f, 0.25f, 1.0f / 60.0f);
        QVERIFY(std::abs(level[0] - 0.7f) < 1e-4f);
        QVERIFY(std::abs(level[1] - 0.2f) < 1e-4f);
    }

    void smoothingStartsFromZeroOnSizeChange() {
        const QVector<float> out = dsp::smooth({0.5f}, {1.0f, 1.0f}, 0.02f, 0.25f, 0.0f);
        QCOMPARE(out, (QVector<float>{0.0f, 0.0f}));
    }

    void peaksJumpUpThenFall() {
        QVector<float> peak = dsp::peaks({}, {0.8f}, 0.6f, 0.01f);
        QCOMPARE(peak[0], 0.8f);
        peak = dsp::peaks(peak, {0.1f}, 0.6f, 0.5f); // falls 0.3, stays above the bar
        QVERIFY(std::abs(peak[0] - 0.5f) < 1e-6f);
        peak = dsp::peaks(peak, {0.1f}, 0.6f, 1.0f); // would fall below; lands on the bar
        QCOMPARE(peak[0], 0.1f);
        peak = dsp::peaks(peak, {0.9f}, 0.6f, 0.01f);
        QCOMPARE(peak[0], 0.9f);
    }

    void spectrumLevelsAreInRange() {
        auto fake = std::make_shared<FakeSource>();
        Spectrum spectrum(sourceFor(fake), Rate);
        QSignalSpy updates(&spectrum, &Spectrum::updated);
        tickFor(spectrum, 0.5);
        QVERIFY(updates.count() >= 30);
        QCOMPARE(spectrum.levels().size(), spectrum.bandCount());
        QCOMPARE(spectrum.bandCount(), spectrum.tuning().bands);
        for (int i = 0; i < spectrum.bandCount(); ++i) {
            QVERIFY(spectrum.levels()[i] >= 0.0 && spectrum.levels()[i] <= 1.0);
            QVERIFY(spectrum.peaks()[i] >= spectrum.levels()[i]);
        }
        QVERIFY(maxOf(spectrum.levels()) > 0.99);
    }

    void spectrumFrozenHoldsLevels() {
        auto fake = std::make_shared<FakeSource>();
        Spectrum spectrum(sourceFor(fake), Rate);
        tickFor(spectrum, 0.2);
        spectrum.setFrozen(true);
        const QList<qreal> held = spectrum.levels();
        fake->silentFrames = 1 << 20;
        tickFor(spectrum, 0.5);
        QCOMPARE(spectrum.levels(), held);
        spectrum.setFrozen(false);
        tickFor(spectrum, 1.0);
        QVERIFY(maxOf(spectrum.levels()) < 0.05);
    }

    void spectrumAnalyzesDelayedAudio() {
        auto fake = std::make_shared<FakeSource>();
        fake->silentFrames = Rate / 2; // the newest half second is silent
        Spectrum spectrum(sourceFor(fake), Rate);
        tickFor(spectrum, 0.3);
        QCOMPARE(fake->lastRequest, dsp::Tuning{}.fftSize);
        QCOMPARE(maxOf(spectrum.levels()), 0.0);
        dsp::Tuning tuning = spectrum.tuning();
        tuning.delaySeconds = 0.5f;
        spectrum.setTuning(tuning);
        tickFor(spectrum, 0.3);
        QCOMPARE(fake->lastRequest, dsp::Tuning{}.fftSize + Rate / 2);
        QVERIFY(maxOf(spectrum.levels()) > 0.99);
    }

    void spectrumClampsTuning() {
        Spectrum spectrum(sourceFor(std::make_shared<FakeSource>()), Rate);
        QSignalSpy changes(&spectrum, &Spectrum::tuningChanged);
        const dsp::Tuning bad = badTuning();
        spectrum.setTuning(bad);
        const dsp::Tuning t = spectrum.tuning();
        QCOMPARE(t.fftSize, 4096);
        QCOMPARE(t.bands, 256);
        QCOMPARE(t.maxHz, 24000.0f);
        QVERIFY(t.minHz > 0.0f && t.minHz < t.maxHz);
        QVERIFY(t.floorDb < t.ceilingDb);
        QVERIFY(t.attackSeconds > 0.0f);
        QCOMPARE(t.delaySeconds, 2.0f);
        QCOMPARE(spectrum.bandCount(), 256);
        QCOMPARE(changes.count(), 1);
        spectrum.setTuning(bad); // same tuning again: no rebuild, no signal
        QCOMPARE(changes.count(), 1);
    }

    void spectrumClampsLongFrames() {
        auto fake = std::make_shared<FakeSource>();
        Spectrum a(sourceFor(fake), Rate);
        Spectrum b(sourceFor(fake), Rate);
        a.tick(5.0);
        b.tick(0.1);
        QCOMPARE(a.levels(), b.levels());
    }
};

int runDspTests(int argc, char *argv[]) {
    DspTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "dsp_tests.moc"
