#include <QtTest>

// TODO: owned by the presets task.
class PresetsTests : public QObject {
    Q_OBJECT

private slots:
    void placeholder() { QVERIFY(true); }
};

int runPresetsTests(int argc, char *argv[]) {
    PresetsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "presets_tests.moc"
