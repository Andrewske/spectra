#include <QDir>
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

QByteArray constantSamples(int samples, float value) {
    QByteArray bytes(samples * int(sizeof(float)), '\0');
    for (int i = 0; i < samples; ++i)
        std::memcpy(bytes.data() + i * int(sizeof(float)), &value, sizeof(float));
    return bytes;
}

bool appendLine(const QString &path, const QByteArray &line) {
    QFile file(path);
    return file.open(QIODevice::Append) && file.write(line + '\n') == line.size() + 1;
}

QString readText(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool allZero(const QVector<float> &samples) {
    return std::all_of(samples.cbegin(), samples.cend(), [](float s) { return s == 0.0f; });
}

// Whether any process still has `text` on its command line.
bool processMentions(const QString &text) {
    const QStringList pids = QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (cmdline.open(QIODevice::ReadOnly) && cmdline.readAll().contains(text.toUtf8()))
            return true;
    }
    return false;
}

QByteArray sinkInputsJson(const QList<QPair<int, bool>> &heliumStreams) {
    QStringList entries;
    for (const auto &[serial, corked] : heliumStreams)
        entries << QStringLiteral("{\"index\":%1,\"corked\":%2,\"properties\":{\"application.name\":"
                                  "\"Helium\",\"application.process.binary\":\"helium\","
                                  "\"object.serial\":\"%1\"}}").arg(serial).arg(corked ? "true" : "false");
    return "[" + entries.join(',').toUtf8() + "]";
}

QByteArray dumpJson(const char *sink, const char *latency) {
    return QStringLiteral(
        "[{\"id\":1,\"type\":\"PipeWire:Interface:Metadata\",\"props\":{\"metadata.name\":\"default\"},"
        "\"metadata\":[{\"subject\":0,\"key\":\"default.audio.sink\",\"value\":{\"name\":\"%1\"}}]},"
        "{\"id\":2,\"type\":\"PipeWire:Interface:Node\",\"info\":{\"props\":{\"node.name\":\"%1\"},"
        "\"params\":{\"Latency\":[{\"direction\":\"Input\",%2}]}}}]").arg(sink, latency).toUtf8();
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

    // Fake pactl and pw-dump: `subscribe` follows events.txt, the stream list
    // and the dump are whatever streams.json and dump.json hold right now.
    void useFakeWatcher(Capture &capture, const QString &prefix) {
        const QString events = path(prefix + "-events.txt");
        const QByteArray pactl = QStringLiteral(
            "#!/bin/sh\n"
            "case \"$1\" in\n"
            "  subscribe) exec tail -n 0 -f '%1' ;;\n"
            "  *) cat '%2' ;;\n"
            "esac\n").arg(events, path(prefix + "-streams.json")).toUtf8();
        const QByteArray dump = QStringLiteral("#!/bin/sh\ncat '%1'\n").arg(path(prefix + "-dump.json")).toUtf8();
        QVERIFY(writeFile(events, {}));
        QVERIFY(writeFile(path(prefix + "-streams.json"), "[]"));
        QVERIFY(writeFile(path(prefix + "-dump.json"), "[]"));
        QVERIFY(writeFile(path(prefix + "-pactl.sh"), pactl, true));
        QVERIFY(writeFile(path(prefix + "-pwdump.sh"), dump, true));
        capture.setWatcherPrograms(path(prefix + "-pactl.sh"), path(prefix + "-pwdump.sh"));
    }

    // A fake recorder that logs its arguments, one launch per line, then
    // runs `body` (a shell snippet) to produce audio.
    QString loggingRecorder(const QString &prefix, const QString &body) {
        const QString loud = path(prefix + "-loud.raw");
        const QString quiet = path(prefix + "-quiet.raw");
        const QByteArray script = QStringLiteral("#!/bin/sh\nLOUD='%1'\nQUIET='%2'\necho \"$*\" >> '%3'\n%4\n")
                                      .arg(loud, quiet, path(prefix + "-launches"), body).toUtf8();
        const QString scriptPath = path(prefix + "-rec.sh");
        if (!writeFile(loud, constantSamples(960, 0.5f)) || !writeFile(quiet, constantSamples(960, 0.0f))
            || !writeFile(scriptPath, script, true))
            return {};
        return scriptPath;
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
        QVERIFY(args.contains("node.passive=true"));
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

    void sleepsAfterSilenceAndWakesOnSound() {
        const QString body = QStringLiteral(
            "i=0; while [ $i -lt 10 ]; do cat \"$LOUD\"; sleep 0.02; i=$((i+1)); done\n"
            "i=0; while [ $i -lt 60 ]; do cat \"$QUIET\"; sleep 0.02; i=$((i+1)); done\n"
            "while :; do cat \"$LOUD\"; sleep 0.02; done");
        Capture capture(nullptr, loggingRecorder("sleep", body), Capture::defaultArguments());
        useFakeWatcher(capture, "sleep");
        capture.setSleepAfter(0.5);
        QList<bool> states;
        connect(&capture, &Capture::statusChanged, this, [&] {
            if (states.isEmpty() || states.last() != capture.sleeping())
                states << capture.sleeping();
        });
        capture.start();
        QTRY_VERIFY_WITH_TIMEOUT(capture.sleeping(), 3000);
        QVERIFY(allZero(capture.latestFrames(4096)));
        QVERIFY(capture.running()); // the passive recorder stays connected
        QTRY_VERIFY_WITH_TIMEOUT(!capture.sleeping(), 3000);
        QVERIFY(!allZero(capture.latestFrames(64)));
        QCOMPARE(states, QList<bool>({false, true, false}));
    }

    void stalledRecorderClearsBarsWithoutSleeping() {
        Capture capture(nullptr, loggingRecorder("stall", "cat \"$LOUD\"\nexec sleep 60"), {});
        useFakeWatcher(capture, "stall");
        capture.start();
        QTRY_COMPARE(capture.framesReceived(), qint64(480));
        QVERIFY(!allZero(capture.latestFrames(480)));
        // An idle output stops a passive recorder's data instead of sending
        // zeros; the bars must fall anyway.
        QTRY_VERIFY_WITH_TIMEOUT(allZero(capture.latestFrames(480)), 2000);
        QVERIFY(!capture.sleeping()); // sleepAfter is 0: never
        QVERIFY(capture.running());
    }

    void unchangedSettingsDoNothing() {
        const QString launches = path("same-launches");
        Capture capture(nullptr, loggingRecorder("same", "exec sleep 60"), {});
        useFakeWatcher(capture, "same");
        capture.setSleepAfter(5.0);
        capture.start();
        QTRY_COMPARE(lineCount(launches), 1);
        QTRY_VERIFY(capture.running());
        QSignalSpy spy(&capture, &Capture::statusChanged);
        for (int i = 0; i < 3; ++i) {
            capture.setTarget(" ");
            capture.setSleepAfter(5.0);
        }
        QTest::qWait(300);
        QCOMPARE(lineCount(launches), 1);
        QCOMPARE(spy.count(), 0);
    }

    void targetFollowsTheAppsStreams() {
        const QString launches = path("app-launches");
        Capture capture(nullptr, loggingRecorder("app", "exec sleep 60"), Capture::defaultArguments());
        useFakeWatcher(capture, "app");
        QVERIFY(writeFile(path("app-streams.json"), sinkInputsJson({{544, false}, {600, true}})));
        capture.setTarget("HELIUM");
        capture.start();
        QTRY_VERIFY(readText(launches).contains("--target 544 "));
        const QString first = readText(launches);
        QVERIFY(first.contains("node.dont-fallback=true"));
        QVERIFY(!first.contains("stream.capture.sink=true"));

        // A new tab replaces the stream: follow it.
        QVERIFY(writeFile(path("app-streams.json"), sinkInputsJson({{900, false}})));
        QVERIFY(appendLine(path("app-events.txt"), "Event 'new' on sink-input #900"));
        QTRY_VERIFY(readText(launches).contains("--target 900 "));

        // Nothing playing from the app is silence, not an error.
        QVERIFY(writeFile(path("app-streams.json"), "[]"));
        QVERIFY(appendLine(path("app-events.txt"), "Event 'remove' on sink-input #900"));
        QTRY_VERIFY(!capture.running());
        QVERIFY(capture.error().isEmpty());
        QVERIFY(allZero(capture.latestFrames(64)));

        capture.setTarget("");
        QTRY_COMPARE(lineCount(launches), 3);
        QVERIFY(readText(launches).split('\n').at(2).contains("stream.capture.sink=true"));
    }

    void vanishedTargetIsNotAnError() {
        const QString body = QStringLiteral(
            "case \"$*\" in *'--target 544 '*)\n"
            "  echo 'remote error: id=2 seq:45 res:-2 (No such file or directory): defined target not found' >&2\n"
            "  exit 1 ;;\n"
            "esac\n"
            "exec sleep 60");
        const QString launches = path("gone-launches");
        Capture capture(nullptr, loggingRecorder("gone", body), Capture::defaultArguments());
        useFakeWatcher(capture, "gone");
        QVERIFY(writeFile(path("gone-streams.json"), sinkInputsJson({{544, false}})));
        QStringList errors;
        connect(&capture, &Capture::statusChanged, this, [&] { errors << capture.error(); });
        capture.setTarget("helium");
        capture.start();
        QTRY_VERIFY(readText(launches).contains("--target 544 "));
        // The recorder's exit alone must prompt a fresh look at the streams.
        QVERIFY(writeFile(path("gone-streams.json"), sinkInputsJson({{545, false}})));
        QTRY_VERIFY(readText(launches).contains("--target 545 "));
        QTRY_VERIFY(capture.running());
        QVERIFY(std::all_of(errors.cbegin(), errors.cend(), [](const QString &e) { return e.isEmpty(); }));
    }

    void latencyFollowsTheDefaultOutput() {
        Capture capture(nullptr, loggingRecorder("lat", "exec sleep 60"), {});
        useFakeWatcher(capture, "lat");
        QVERIFY(writeFile(path("lat-dump.json"), dumpJson("alsa_out", "\"minQuantum\":1.0,\"minRate\":512,\"minNs\":0")));
        QSignalSpy spy(&capture, &Capture::outputLatencyChanged);
        capture.start();
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(qAbs(capture.outputLatency() - 768.0 / 48000.0) < 1e-6);

        QVERIFY(writeFile(path("lat-dump.json"), dumpJson("bluez_out", "\"minQuantum\":0.0,\"minRate\":0,\"minNs\":187000000")));
        QVERIFY(appendLine(path("lat-events.txt"), "Event 'change' on server #-1"));
        QTRY_COMPARE(spy.count(), 2);
        QVERIFY(qAbs(capture.outputLatency() - 0.187) < 1e-6);

        // The same latency again is not a change.
        QVERIFY(appendLine(path("lat-events.txt"), "Event 'change' on sink #201"));
        QTest::qWait(800);
        QCOMPARE(spy.count(), 2);
    }

    void destructorEndsWatcherProcesses() {
        auto *capture = new Capture(nullptr, loggingRecorder("kill", "exec sleep 60"), {});
        useFakeWatcher(*capture, "kill");
        capture->start();
        const QString events = path("kill-events.txt");
        QTRY_VERIFY(processMentions(events));
        delete capture;
        QVERIFY(!processMentions(events));
    }
};

int runCaptureTests(int argc, char *argv[]) {
    CaptureTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "capture_tests.moc"
