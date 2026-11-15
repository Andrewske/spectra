#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "presets.h"
#include "settings.h"

namespace {

void writeFile(const QString &path, const QByteArray &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
    QVERIFY(file.commit());
}

QString readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QStringList names(const Presets &presets) {
    QStringList list;
    for (const QVariant &item : presets.list())
        list << item.toMap().value("name").toString();
    return list;
}

const QByteArray MyConfig = R"(; my settings
[look]
colors=#000000, #ffffff ; black to white
mode=spectrum
peaks=true
glow=false
layout=radial
gap=0.5

[analysis]
bands=64
delay=0.3
)";

// A temp dir holding spectra.conf and a presets folder beside it.
struct Fixture {
    QTemporaryDir dir;
    QString config() const { return dir.filePath("spectra.conf"); }
    QString presets() const { return dir.filePath("presets"); }
};

} // namespace

class PresetsTests : public QObject {
    Q_OBJECT

private slots:
    void builtInsListedFirstInOrder();
    void defaultConfigMatchesRainbow();
    void applyWritesKeysAndKeepsOthers();
    void applyUserPresetWithAnalysisKeys();
    void userPresetNeverSetsLayout();
    void saveCurrentRoundTrip();
    void rejectedNames();
    void removeDeletesUserPresetsOnly();
    void applyIndexOutOfRangeDoesNothing();
    void unknownNameDoesNothing();
    void currentClearedByExternalEdit();
    void currentClearedBySetter();
    void handAddedPresetShowsUp();
    void setValuesWritesOnce();
    void folderCreatedAfterStartShowsUp();
    void presetEditedInPlaceIsReread();
};

void PresetsTests::builtInsListedFirstInOrder() {
    Fixture f;
    writeFile(f.presets() + "/aaa.conf", "[look]\ncolors=#123456\n");
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    const QStringList expected = {"Rainbow", "Neon", "Sunset", "Ocean", "Fire", "Mono", "aaa"};
    QCOMPARE(names(presets), expected);
    const QVariantMap neon = presets.list().at(1).toMap();
    QVERIFY(neon.value("builtIn").toBool());
    QCOMPARE(neon.value("colors").toStringList().size(), 3);
    const QVariantMap user = presets.list().last().toMap();
    QVERIFY(!user.value("builtIn").toBool());
    QCOMPARE(user.value("colors").toStringList(), QStringList({"#123456"}));
}

void PresetsTests::defaultConfigMatchesRainbow() {
    Fixture f;
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    QCOMPARE(presets.current(), QString("Rainbow"));
}

void PresetsTests::applyWritesKeysAndKeepsOthers() {
    Fixture f;
    writeFile(f.config(), MyConfig);
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    QSignalSpy current(&presets, &Presets::currentChanged);
    presets.apply("Fire");
    QCOMPARE(presets.current(), QString("Fire"));
    QCOMPARE(current.count(), 1);
    QCOMPARE(settings.colorMode(), QString("vertical"));
    QVERIFY(settings.glow());
    const QString text = readFile(f.config());
    QVERIFY(text.contains("; my settings"));
    QVERIFY2(text.contains("colors=#6b0000, #ff6a00, #ffe14d ; black to white"), qPrintable(text));
    QVERIFY(text.contains("layout=radial"));
    QVERIFY(text.contains("gap=0.5"));
    QVERIFY(text.contains("bands=64"));
    QVERIFY(text.contains("delay=0.3"));
    QCOMPARE(Settings(f.config()).colorMode(), QString("vertical"));
}

void PresetsTests::applyUserPresetWithAnalysisKeys() {
    Fixture f;
    writeFile(f.config(), MyConfig);
    writeFile(f.presets() + "/Slow.conf", "[look]\ncolors=#ff0000, #0000ff\n[analysis]\nbands=128\ndecay=1.5\n");
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    presets.applyIndex(6);
    QCOMPARE(presets.current(), QString("Slow"));
    QCOMPARE(settings.tuning().bands, 128);
    QCOMPARE(settings.tuning().decaySeconds, 1.5f);
    QCOMPARE(settings.colors(), QStringList({"#ff0000", "#0000ff"}));
    QCOMPARE(settings.tuning().delaySeconds, 0.3f); // not in the preset, so kept
    QCOMPARE(settings.layout(), QString("radial"));
}

void PresetsTests::userPresetNeverSetsLayout() {
    Fixture f;
    writeFile(f.config(), MyConfig);
    writeFile(f.presets() + "/Odd.conf", "[look]\nlayout=waveform\nglow=true\n[capture]\ntarget=spotify\n");
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    presets.apply("Odd");
    QVERIFY(settings.glow());
    QCOMPARE(settings.layout(), QString("radial"));
    QCOMPARE(settings.captureTarget(), QString());
}

void PresetsTests::saveCurrentRoundTrip() {
    Fixture f;
    writeFile(f.config(), MyConfig);
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    QSignalSpy list(&presets, &Presets::listChanged);
    QVERIFY(presets.saveCurrent("  Mine  "));
    QVERIFY(list.count() >= 1);
    QCOMPARE(presets.current(), QString("Mine"));
    QCOMPARE(names(presets).last(), QString("Mine"));
    const QString text = readFile(f.presets() + "/Mine.conf");
    QVERIFY2(text.contains("colors=#000000, #ffffff\n"), qPrintable(text));
    QVERIFY(text.contains("bands=64\n"));
    QVERIFY(text.contains("delay=0.3\n"));
    QVERIFY(text.contains("attack=0.02\n"));
    QVERIFY(!text.contains("layout"));

    presets.apply("Neon");
    QCOMPARE(settings.colors().size(), 3);
    settings.setValues({{"analysis/bands", "200"}});
    presets.apply("Mine");
    QCOMPARE(settings.colors(), QStringList({"#000000", "#ffffff"}));
    QCOMPARE(settings.tuning().bands, 64);
    QVERIFY(!settings.glow());
    QCOMPARE(presets.current(), QString("Mine"));
}

void PresetsTests::rejectedNames() {
    Fixture f;
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    for (const char *name : {"", "   ", "a/b", "../up", ".hidden", "Neon", "neon", "MONO"})
        QVERIFY2(!presets.saveCurrent(name), name);
    QCOMPARE(names(presets).size(), 6);
    QVERIFY(!QFileInfo::exists(f.presets()) || QDir(f.presets()).isEmpty());
}

void PresetsTests::removeDeletesUserPresetsOnly() {
    Fixture f;
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    QVERIFY(presets.saveCurrent("Gone"));
    QVERIFY(!presets.remove("Rainbow"));
    QVERIFY(!presets.remove("nothing"));
    QVERIFY(presets.remove("Gone"));
    QVERIFY(!QFileInfo::exists(f.presets() + "/Gone.conf"));
    QCOMPARE(names(presets).size(), 6);
    QCOMPARE(presets.current(), QString()); // the removed preset was current
}

void PresetsTests::applyIndexOutOfRangeDoesNothing() {
    Fixture f;
    writeFile(f.config(), MyConfig);
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    const QString before = readFile(f.config());
    presets.applyIndex(-1);
    presets.applyIndex(6);
    presets.applyIndex(8);
    QCOMPARE(readFile(f.config()), before);
    QCOMPARE(presets.current(), QString());
}

void PresetsTests::unknownNameDoesNothing() {
    Fixture f;
    writeFile(f.config(), MyConfig);
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    const QString before = readFile(f.config());
    presets.apply("Nope");
    QCOMPARE(readFile(f.config()), before);
}

void PresetsTests::currentClearedByExternalEdit() {
    Fixture f;
    writeFile(f.config(), MyConfig);
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    presets.apply("Ocean");
    QCOMPARE(presets.current(), QString("Ocean"));
    // An edit to a key the preset doesn't set keeps it current.
    writeFile(f.config(), readFile(f.config()).replace("bands=64", "bands=80").toUtf8());
    QTRY_COMPARE(settings.tuning().bands, 80);
    QCOMPARE(presets.current(), QString("Ocean"));
    writeFile(f.config(), readFile(f.config()).replace("#0a1f5c", "#0A1F5D").toUtf8());
    QTRY_COMPARE(presets.current(), QString());
}

void PresetsTests::currentClearedBySetter() {
    Fixture f;
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    presets.apply("Neon");
    settings.setLayout("mirrored"); // layout isn't part of a look
    QCOMPARE(presets.current(), QString("Neon"));
    settings.setGlow(false);
    QCOMPARE(presets.current(), QString());
}

void PresetsTests::handAddedPresetShowsUp() {
    Fixture f;
    QDir().mkpath(f.presets());
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    writeFile(f.presets() + "/Later.conf", "[look]\nglow=true\n");
    writeFile(f.presets() + "/Neon.conf", "[look]\nglow=false\n"); // can't shadow a built-in
    QTRY_COMPARE(names(presets).size(), 7);
    QCOMPARE(names(presets).last(), QString("Later"));
}

// A fresh install has no presets folder; making it by hand later must work.
void PresetsTests::folderCreatedAfterStartShowsUp() {
    Fixture f;
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    QVERIFY(!QFileInfo::exists(f.presets()));
    writeFile(f.presets() + "/Later.conf", "[look]\nglow=true\n");
    QTRY_COMPARE(names(presets).size(), 7);
    writeFile(f.presets() + "/Again.conf", "[look]\nglow=true\n"); // and the folder is watched now
    QTRY_COMPARE(names(presets).size(), 8);
}

// Writing into the file without replacing it, the way some editors save.
void PresetsTests::presetEditedInPlaceIsReread() {
    Fixture f;
    writeFile(f.presets() + "/Mine.conf", "[look]\ncolors=#ff0000, #00ff00\n");
    Settings settings(f.config());
    Presets presets(&settings, f.presets());
    presets.apply("Mine");
    QCOMPARE(presets.current(), QString("Mine"));
    QFile file(f.presets() + "/Mine.conf");
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("[look]\ncolors=#123456\n");
    file.close();
    QTRY_COMPARE(presets.list().last().toMap().value("colors").toStringList(), QStringList{"#123456"});
    QCOMPARE(presets.current(), QString()); // the config no longer matches the edited preset
    presets.apply("Mine");
    QVERIFY(readFile(f.config()).contains("colors=#123456"));
}

void PresetsTests::setValuesWritesOnce() {
    Fixture f;
    writeFile(f.config(), MyConfig);
    Settings settings(f.config());
    QSignalSpy changed(&settings, &Settings::changed);
    QVERIFY(settings.setValues({{"look/mode", "vertical"}, {"look/peaks", "false"}, {"wallpaper/height", "0.5"}}));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(settings.colorMode(), QString("vertical"));
    QVERIFY(!settings.showPeaks());
    QCOMPARE(settings.wallpaperHeight(), 0.5);
    const QString text = readFile(f.config());
    QVERIFY(text.contains("; my settings"));
    QVERIFY(text.contains("[wallpaper]\nheight=0.5"));
    QVERIFY(text.contains("layout=radial"));
}

int runPresetsTests(int argc, char *argv[]) {
    PresetsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "presets_tests.moc"
