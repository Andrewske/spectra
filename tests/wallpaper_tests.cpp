#include <QtTest>

#include <QLockFile>
#include <QTemporaryDir>

#include "wallpaper.h"

namespace {

// Stands in for kill(2): records who would have been stopped.
struct FakeStop {
    QList<qint64> pids;
    bool result = true;
    QLockFile *exitsFirst = nullptr; // the owner lets go just before the signal lands
    std::function<bool(qint64)> fn() {
        return [this](qint64 pid) {
            pids << pid;
            if (exitsFirst)
                exitsFirst->unlock();
            return result;
        };
    }
};

// A PID that existed and has exited, so a lock naming it is stale.
qint64 deadPid() {
    QProcess process;
    process.start(QStringLiteral("true"), {});
    process.waitForStarted();
    const qint64 pid = process.processId();
    process.waitForFinished();
    return pid;
}

void writeLockFile(const QString &path, qint64 pid) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray::number(pid) + "\nspectra\n" + QSysInfo::machineHostName().toUtf8() + "\n");
}

} // namespace

class WallpaperTests : public QObject {
    Q_OBJECT

private slots:
    void detectsTheFlag() {
        char prog[] = "spectra", flag[] = "--wallpaper", other[] = "--other";
        char *with[] = {prog, other, flag};
        char *without[] = {prog, other};
        char *flagAsProgram[] = {flag};
        QVERIFY(wallpaper::requested(3, with));
        QVERIFY(!wallpaper::requested(2, without));
        QVERIFY(!wallpaper::requested(1, flagAsProgram));
    }

    void lockLivesInTheRuntimeDir() {
        QCOMPARE(wallpaper::lockPath("/run/user/1000"), QString("/run/user/1000/spectra-wallpaper.lock"));
        QVERIFY(wallpaper::lockPath({}).startsWith(QDir::tempPath()));
    }

    void firstLaunchOwnsTheLock() {
        QTemporaryDir dir;
        QLockFile lock(wallpaper::lockPath(dir.path()));
        FakeStop stop;
        QCOMPARE(wallpaper::claimOrStop(lock, stop.fn()), wallpaper::Claim::Owner);
        QVERIFY(lock.isLocked());
        QVERIFY(stop.pids.isEmpty());
        QCOMPARE(lock.staleLockTime(), 0); // a days-old wallpaper still counts
    }

    void secondLaunchStopsTheFirst() {
        QTemporaryDir dir;
        const QString path = wallpaper::lockPath(dir.path());
        QLockFile running(path);
        QVERIFY(running.tryLock(0));
        QLockFile second(path);
        FakeStop stop;
        QCOMPARE(wallpaper::claimOrStop(second, stop.fn()), wallpaper::Claim::StoppedOther);
        QCOMPARE(stop.pids, QList<qint64>{QCoreApplication::applicationPid()});
        QVERIFY(!second.isLocked());
    }

    void lockIsFreeAgainAfterTheFirstExits() {
        QTemporaryDir dir;
        const QString path = wallpaper::lockPath(dir.path());
        QLockFile running(path);
        QVERIFY(running.tryLock(0));
        running.unlock();
        QLockFile next(path);
        FakeStop stop;
        QCOMPARE(wallpaper::claimOrStop(next, stop.fn()), wallpaper::Claim::Owner);
        QVERIFY(stop.pids.isEmpty());
    }

    void crashedWallpaperDoesNotBlock() {
        QTemporaryDir dir;
        const QString path = wallpaper::lockPath(dir.path());
        writeLockFile(path, deadPid());
        QLockFile lock(path);
        FakeStop stop;
        QCOMPARE(wallpaper::claimOrStop(lock, stop.fn()), wallpaper::Claim::Owner);
        QVERIFY(stop.pids.isEmpty());
    }

    void failedStopRefusesToRun() {
        QTemporaryDir dir;
        const QString path = wallpaper::lockPath(dir.path());
        QLockFile running(path);
        QVERIFY(running.tryLock(0));
        QLockFile second(path);
        FakeStop stop;
        stop.result = false;
        QCOMPARE(wallpaper::claimOrStop(second, stop.fn()), wallpaper::Claim::Failed);
        QVERIFY(!second.isLocked());
    }

    void ownerExitingBeforeTheSignalHandsOverTheLock() {
        QTemporaryDir dir;
        const QString path = wallpaper::lockPath(dir.path());
        QLockFile running(path);
        QVERIFY(running.tryLock(0));
        QLockFile next(path);
        FakeStop stop;
        stop.result = false; // kill() found no such process
        stop.exitsFirst = &running;
        QCOMPARE(wallpaper::claimOrStop(next, stop.fn()), wallpaper::Claim::Owner);
        QVERIFY(next.isLocked());
    }

    void unusableLockRefusesToRun() {
        QLockFile lock(QStringLiteral("/nonexistent-dir/spectra-wallpaper.lock"));
        FakeStop stop;
        QCOMPARE(wallpaper::claimOrStop(lock, stop.fn()), wallpaper::Claim::Failed);
        QVERIFY(stop.pids.isEmpty());
    }

    void matchesMonitorByName() {
        const QStringList names = {"HDMI-A-1", "DP-1", "DP-2"};
        QCOMPARE(wallpaper::screenIndex(names, "DP-1"), 1);
        QCOMPARE(wallpaper::screenIndex(names, "dp-2"), 2);
        QCOMPARE(wallpaper::screenIndex(names, "DP-3"), -1);
        QCOMPARE(wallpaper::screenIndex(names, ""), -1);
        QCOMPARE(wallpaper::screenIndex({}, "DP-1"), -1);
    }
};

int runWallpaperTests(int argc, char *argv[]) {
    WallpaperTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "wallpaper_tests.moc"
