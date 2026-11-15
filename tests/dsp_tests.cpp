#include <QtTest>

// TODO: owned by the dsp task.
class DspTests : public QObject {
    Q_OBJECT

private slots:
    void placeholder() { QVERIFY(true); }
};

int runDspTests(int argc, char *argv[]) {
    DspTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "dsp_tests.moc"
