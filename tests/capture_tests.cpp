#include <QtTest>

// TODO: owned by the capture task.
class CaptureTests : public QObject {
    Q_OBJECT

private slots:
    void placeholder() { QVERIFY(true); }
};

int runCaptureTests(int argc, char *argv[]) {
    CaptureTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "capture_tests.moc"
