#include <QtTest>

// TODO: owned by the wallpaper task.
class WallpaperTests : public QObject {
    Q_OBJECT

private slots:
    void placeholder() { QVERIFY(true); }
};

int runWallpaperTests(int argc, char *argv[]) {
    WallpaperTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "wallpaper_tests.moc"
