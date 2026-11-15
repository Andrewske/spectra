#include <QtTest>

// TODO: owned by the capture task.
class StreamsTests : public QObject {
    Q_OBJECT

private slots:
    void placeholder() { QVERIFY(true); }
};

int runStreamsTests(int argc, char *argv[]) {
    StreamsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "streams_tests.moc"
