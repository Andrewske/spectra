#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>

#include "capture.h"

namespace {

// Interleaved samples whose value is their own index, so any reordering,
// dropped byte or misaligned float shows up as a wrong number.
QByteArray countingSamples(int samples) {
    QByteArray bytes(samples * int(sizeof(float)), '\0');
    for (int i = 0; i < samples; ++i) {
        const float value = float(i);
        std::memcpy(bytes.data() + i * int(sizeof(float)), &value, sizeof(float));
    }
    return bytes;
}

QVector<float> range(int first, int count) {
    QVector<float> values(count);
    for (int i = 0; i < count; ++i)
        values[i] = float(first + i);
    return values;
}

bool writeFile(const QString &path, const QByteArray &contents, bool executable = false) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size())
        return false;
    file.close();
    if (executable)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    return true;
}

int lineCount(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? int(file.readAll().count('\n')) : 0;
}

// Byte counts chosen to end mid-float and mid-frame (frames are 8 bytes).
const char *const OddChunks = "1 2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71 73 79 83 89 97";

} // namespace

class CaptureTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

    QString path(const QString &name) const { return dir_.filePath(name); }

    // A fake pw-record: emits data.raw in the given byte-sized chunks with a
    // pause between them (so the reads really split there), then stays
    // alive like a recorder waiting for more audio.
    QString chunkedRecorder(const QString &name, const QByteArray &data, const char *chunks) {
        const QString dataPath = path(name + ".raw");
        const QByteArray script = QStringLiteral(
            "#!/bin/sh\n"
            "off=0\n"
            "for n in %1; do\n"
            "  dd if='%2' bs=4096 iflag=skip_bytes,count_bytes skip=$off count=$n status=none\n"
            "  off=$((off + n))\n"
            "  sleep 0.01\n"
            "done\n"
            "dd if='%2' bs=4093 iflag=skip_bytes skip=$off status=none\n"
            "exec sleep 60\n").arg(QString::fromLatin1(chunks), dataPath).toUtf8();
        const QString scriptPath = path(name + ".sh");
        if (!writeFile(dataPath, data) || !writeFile(scriptPath, script, true))
            return {};
        return scriptPath;
    }

    // A fake that counts its launches in a file. The first run writes one
    // frame, complains on stderr and exits; later runs keep streaming.
    QString flakyRecorder(const QString &counterPath) {
        const QByteArray script = QStringLiteral(
            "#!/bin/sh\n"
            "echo run >> '%1'\n"
            "if [ \"$(wc -l < '%1')\" -eq 1 ]; then\n"
            "  head -c 8 /dev/zero\n"
            "  echo 'target node vanished' >&2\n"
            "  exit 3\n"
            "fi\n"
            "head -c 800 /dev/zero\n"
            "exec sleep 60\n").arg(counterPath).toUtf8();
        const QString scriptPath = path("flaky.sh");
        return writeFile(scriptPath, script, true) ? scriptPath : QString();
    }

private slots:
    void initTestCase() { QVERIFY(dir_.isValid()); }

    void defaultArgumentsAskForRawFloatSinkMonitor() {
        const QStringList args = Capture::defaultArguments();
        QVERIFY(args.contains("--raw"));
        QCOMPARE(args.at(args.indexOf("--format") + 1), QString("f32"));
        QCOMPARE(args.at(args.indexOf("--rate") + 1), QString("48000"));
        QCOMPARE(args.at(args.indexOf("--channels") + 1), QString("2"));
        QVERIFY(args.contains("stream.capture.sink=true"));
        QCOMPARE(args.last(), QString("-"));
    }

    void emptyBufferIsZeros() {
        Capture capture(nullptr, path("never-started"), {});
        QCOMPARE(capture.latestFrames(4), QVector<float>(8, 0.0f));
        QCOMPARE(capture.latestFrames(0).size(), 0);
        QCOMPARE(capture.framesReceived(), qint64(0));
    }

    void oddChunksStayAlignedAndOrdered() {
        // 251 frames plus 5 stray bytes: the trailing partial frame must not
        // be counted or shift anything.
        QByteArray data = countingSamples(502) + QByteArray(5, '\x7f');
        Capture capture(nullptr, chunkedRecorder("odd", data, OddChunks), {});
        capture.start();
        QTRY_COMPARE(capture.framesReceived(), qint64(251));
        QVERIFY(capture.running());
        QCOMPARE(capture.latestFrames(251), range(0, 502));
        QCOMPARE(capture.latestFrames(3), range(496, 6));
        QVERIFY(capture.error().isEmpty());
        capture.stop();
        QVERIFY(!capture.running());
    }

    void shortBufferIsZeroPaddedAtFront() {
        Capture capture(nullptr, chunkedRecorder("pad", countingSamples(20), "3 9"), {});
        capture.start();
        QTRY_COMPARE(capture.framesReceived(), qint64(10));
        const QVector<float> frames = capture.latestFrames(16);
        QCOMPARE(frames.size(), 32);
        QCOMPARE(frames.mid(0, 12), QVector<float>(12, 0.0f));
        QCOMPARE(frames.mid(12), range(0, 20));
    }

    void ringWrapsAndKeepsNewest() {
        // 1.4 buffers' worth, written in odd 4093-byte blocks so the ring
        // seam lands mid-block.
        const int capacityFrames = Capture::BufferSeconds * Capture::SampleRate;
        const int totalFrames = capacityFrames * 7 / 5;
        const QByteArray data = countingSamples(totalFrames * Capture::Channels);
        Capture capture(nullptr, chunkedRecorder("wrap", data, "4093 4093 4093"), {});
        capture.start();
        QTRY_COMPARE_WITH_TIMEOUT(capture.framesReceived(), qint64(totalFrames), 10000);
        const int oldest = (totalFrames - capacityFrames) * Capture::Channels;
        QCOMPARE(capture.latestFrames(capacityFrames), range(oldest, capacityFrames * Capture::Channels));
        // Asking for more than the ring holds pads the front instead.
        const QVector<float> over = capture.latestFrames(capacityFrames + 10);
        QCOMPARE(over.mid(0, 20), QVector<float>(20, 0.0f));
        QCOMPARE(over.mid(20, 4), range(oldest, 4));
    }

    void missingProgramReportsError() {
        Capture capture(nullptr, path("no-such-recorder"), {});
        QSignalSpy spy(&capture, &Capture::statusChanged);
        capture.start();
        QTRY_COMPARE(capture.error(), QString("no-such-recorder isn't installed"));
        QVERIFY(spy.count() >= 1);
        QVERIFY(!capture.running());
        capture.stop();
        QVERIFY(capture.error().isEmpty());
    }

    void exitShowsErrorThenRetriesAndRecovers() {
        const QString counter = path("flaky-runs");
        Capture capture(nullptr, flakyRecorder(counter), {});
        QStringList errors;
        connect(&capture, &Capture::statusChanged, this, [&] { errors << capture.error(); });
        capture.start();
        QTRY_VERIFY(errors.contains("Audio capture stopped: target node vanished"));
        // First retry comes after about a second.
        QTRY_COMPARE_WITH_TIMEOUT(lineCount(counter), 2, 3000);
        QTRY_COMPARE(capture.framesReceived(), qint64(101));
        QVERIFY(capture.error().isEmpty());
        QVERIFY(capture.running());
    }

    void stopEndsProcessWithoutRetry() {
        const QString counter = path("stop-runs");
        const QByteArray script = QStringLiteral("#!/bin/sh\necho run >> '%1'\nexec sleep 60\n")
                                      .arg(counter).toUtf8();
        QVERIFY(writeFile(path("stop.sh"), script, true));
        Capture capture(nullptr, path("stop.sh"), {});
        capture.start();
        QTRY_VERIFY(capture.running());
        QTRY_COMPARE(lineCount(counter), 1);
        capture.stop();
        QVERIFY(!capture.running());
        QTest::qWait(1300);
        QCOMPARE(lineCount(counter), 1);
        QVERIFY(capture.error().isEmpty());
    }

    void destructorEndsRunningProcess() {
        auto *capture = new Capture(nullptr, chunkedRecorder("dtor", countingSamples(2), "1"), {});
        capture->start();
        QTRY_VERIFY(capture->running());
        delete capture; // must not hang or warn about a running QProcess
    }
};

int runCaptureTests(int argc, char *argv[]) {
    CaptureTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "capture_tests.moc"
