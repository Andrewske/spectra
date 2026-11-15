#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "settings.h"

namespace {

void writeFile(const QString &path, const QByteArray &text) {
    // QSaveFile renames over the target, the way most editors save.
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
    QVERIFY(file.commit());
}

QString readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

const QByteArray CustomConfig = R"(; my settings
[look]
background=#101010
colors=#000000, #ffffff
mode=vertical        ; bottom to top
peaks=false
gap=0.5

[analysis]
bands=64
fft_size=8192
min_hz=40
max_hz=12000
floor_db=-80
ceiling_db=-5
attack=0.05
decay=0.5
peak_fall=1.2
delay=0.3
)";

} // namespace

class SettingsTests : public QObject {
    Q_OBJECT

private slots:
    void defaultsWrittenOnFirstRun();
    void valuesRead();
    void tuningRead();
    void missingKeysUseDefaults();
    void badValueFallsBackWithError();
    void invertedRangesFallBack();
    void colorAtEndpointsAndMidpoints();
    void settersPersistAndKeepOtherKeys();
    void invalidModeIgnored();
    void externalEditEmitsChanged();
};

void SettingsTests::defaultsWrittenOnFirstRun() {
    QTemporaryDir dir;
    const QString path = dir.filePath("nested/spectra.conf");
    Settings settings(path);
    QVERIFY(QFileInfo::exists(path));
    const QString text = readFile(path);
    QVERIFY(text.contains("[look]"));
    QVERIFY(text.contains("bands=96"));
    QCOMPARE(settings.error(), QString());
    QCOMPARE(settings.background(), QColor(Qt::black));
    QCOMPARE(settings.colors().size(), 6);
    QCOMPARE(settings.colorMode(), QString("spectrum"));
    QVERIFY(settings.showPeaks());
    QCOMPARE(settings.barGap(), 0.25);
    QCOMPARE(settings.tuning().bands, 96);
    QCOMPARE(settings.tuning().fftSize, dsp::Tuning{}.fftSize);
    QCOMPARE(settings.tuning().ceilingDb, dsp::Tuning{}.ceilingDb);
}

void SettingsTests::valuesRead() {
    QTemporaryDir dir;
    const QString path = dir.filePath("spectra.conf");
    writeFile(path, CustomConfig);
    Settings settings(path);
    QCOMPARE(settings.error(), QString());
    QCOMPARE(settings.background(), QColor("#101010"));
    QCOMPARE(settings.colors(), QStringList({"#000000", "#ffffff"}));
    QCOMPARE(settings.colorMode(), QString("vertical"));
    QVERIFY(!settings.showPeaks());
    QCOMPARE(settings.barGap(), 0.5);
}

void SettingsTests::tuningRead() {
    QTemporaryDir dir;
    const QString path = dir.filePath("spectra.conf");
    writeFile(path, CustomConfig);
    const dsp::Tuning t = Settings(path).tuning();
    QCOMPARE(t.bands, 64);
    QCOMPARE(t.fftSize, 8192);
    QCOMPARE(t.minHz, 40.0f);
    QCOMPARE(t.maxHz, 12000.0f);
    QCOMPARE(t.floorDb, -80.0f);
    QCOMPARE(t.ceilingDb, -5.0f);
    QCOMPARE(t.attackSeconds, 0.05f);
    QCOMPARE(t.decaySeconds, 0.5f);
    QCOMPARE(t.peakFallPerSecond, 1.2f);
    QCOMPARE(t.delaySeconds, 0.3f);
}

void SettingsTests::missingKeysUseDefaults() {
    QTemporaryDir dir;
    const QString path = dir.filePath("spectra.conf");
    writeFile(path, "[look]\ngap=0.1\n");
    Settings settings(path);
    QCOMPARE(settings.error(), QString());
    QCOMPARE(settings.barGap(), 0.1);
    QCOMPARE(settings.colorMode(), QString("spectrum"));
    QCOMPARE(settings.tuning().bands, 96);
    // An existing file is the user's; it must not be overwritten with defaults.
    QCOMPARE(readFile(path), QString("[look]\ngap=0.1\n"));
}

void SettingsTests::badValueFallsBackWithError() {
    QTemporaryDir dir;
    const QString path = dir.filePath("spectra.conf");
    writeFile(path, "[look]\ngap=wide\ncolors=#ff0000, nope\nmode=spiral\npeaks=maybe\nbackground=#00ff00\n"
                    "[analysis]\nbands=3\nfft_size=3000\n");
    Settings settings(path);
    QCOMPARE(settings.barGap(), 0.25);
    QCOMPARE(settings.colors().size(), 6);
    QCOMPARE(settings.colorMode(), QString("spectrum"));
    QVERIFY(settings.showPeaks());
    QCOMPARE(settings.tuning().bands, 96);
    QCOMPARE(settings.tuning().fftSize, dsp::Tuning{}.fftSize);
    QCOMPARE(settings.background(), QColor("#00ff00")); // good keys still read
    const QString error = settings.error();
    for (const char *key : {"gap", "colors", "mode", "peaks", "bands", "fft_size"})
        QVERIFY2(error.contains(key), qPrintable(error));
}

void SettingsTests::invertedRangesFallBack() {
    QTemporaryDir dir;
    const QString path = dir.filePath("spectra.conf");
    writeFile(path, "[analysis]\nmin_hz=5000\nmax_hz=100\nfloor_db=-10\nceiling_db=-70\n");
    Settings settings(path);
    QCOMPARE(settings.tuning().minHz, 30.0f);
    QCOMPARE(settings.tuning().maxHz, 16000.0f);
    QCOMPARE(settings.tuning().floorDb, -70.0f);
    QVERIFY(settings.error().contains("min_hz"));
    QVERIFY(settings.error().contains("floor_db"));
}

void SettingsTests::colorAtEndpointsAndMidpoints() {
    QTemporaryDir dir;
    const QString path = dir.filePath("spectra.conf");
    writeFile(path, "[look]\ncolors=#ff0000, #00ff00, #0000ff\n");
    Settings settings(path);
    QCOMPARE(settings.colorAt(0.0), QColor("#ff0000"));
    QCOMPARE(settings.colorAt(0.5), QColor("#00ff00"));
    QCOMPARE(settings.colorAt(1.0), QColor("#0000ff"));
    QCOMPARE(settings.colorAt(-3.0), QColor("#ff0000"));
    QCOMPARE(settings.colorAt(7.0), QColor("#0000ff"));
    const QColor quarter = settings.colorAt(0.25);
    QVERIFY(qAbs(quarter.red() - 128) <= 1);
    QVERIFY(qAbs(quarter.green() - 128) <= 1);
    QCOMPARE(quarter.blue(), 0);
}

void SettingsTests::settersPersistAndKeepOtherKeys() {
    QTemporaryDir dir;
    const QString path = dir.filePath("spectra.conf");
    writeFile(path, CustomConfig);
    Settings settings(path);
    QSignalSpy spy(&settings, &Settings::changed);
    settings.setColorMode("spectrum");
    settings.setShowPeaks(true);
    QCOMPARE(settings.colorMode(), QString("spectrum"));
    QVERIFY(settings.showPeaks());
    QVERIFY(spy.count() >= 2);
    const QString text = readFile(path);
    QVERIFY(text.contains("mode=spectrum        ; bottom to top"));
    QVERIFY(text.contains("peaks=true"));
    QVERIFY(text.contains("; my settings"));
    QVERIFY(text.contains("bands=64"));
    QVERIFY(text.contains("colors=#000000, #ffffff"));
    Settings reread(path);
    QCOMPARE(reread.colorMode(), QString("spectrum"));
    QVERIFY(reread.showPeaks());
    QCOMPARE(reread.barGap(), 0.5);
}

void SettingsTests::invalidModeIgnored() {
    QTemporaryDir dir;
    Settings settings(dir.filePath("spectra.conf"));
    settings.setColorMode("rainbow");
    QCOMPARE(settings.colorMode(), QString("spectrum"));
    QVERIFY(readFile(settings.path()).contains("mode=spectrum"));
}

void SettingsTests::externalEditEmitsChanged() {
    QTemporaryDir dir;
    const QString path = dir.filePath("spectra.conf");
    Settings settings(path);
    QSignalSpy spy(&settings, &Settings::changed);
    writeFile(path, "[look]\ngap=0.5\n");
    QTRY_COMPARE(settings.barGap(), 0.5);
    QVERIFY(spy.count() >= 1);
    // A second edit proves the watch was re-armed after the file was replaced.
    writeFile(path, "[look]\ngap=0.7\n");
    QTRY_COMPARE(settings.barGap(), 0.7);
}

int runSettingsTests(int argc, char *argv[]) {
    SettingsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "settings_tests.moc"
