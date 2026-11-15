#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "streams.h"

// Records the default output's monitor (what you hear, after EasyEffects) by
// running pw-record, and keeps the last few seconds of interleaved stereo
// float samples in a ring buffer that the spectrum reads every frame. With a
// target it records one app's playback stream instead (before EasyEffects).
class Capture : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    // True after sleepAfter seconds of silence, with the bars cleared; the
    // first sound clears it. The device suspends whether or not this is set,
    // because the recorder's link is passive and never holds the output awake.
    Q_PROPERTY(bool sleeping READ sleeping NOTIFY statusChanged)
    // Seconds of latency the default output adds (Bluetooth is ~0.15-0.25).
    Q_PROPERTY(double outputLatency READ outputLatency NOTIFY outputLatencyChanged)

public:
    static constexpr int SampleRate = 48000;
    static constexpr int Channels = 2;
    // Long enough for the largest FFT plus the largest A/V delay setting.
    static constexpr int BufferSeconds = 3;

    // The recorder is injectable so tests can feed a fake one that writes
    // known samples to stdout.
    explicit Capture(QObject *parent = nullptr, const QString &program = QStringLiteral("pw-record"),
                     const QStringList &arguments = defaultArguments());
    ~Capture() override;

    static QStringList defaultArguments();

    void start();
    void stop();

    bool running() const;
    // Empty while healthy; otherwise a sentence the window can show as is.
    QString error() const;

    // The most recent `frames` frames, interleaved L/R, oldest first. When
    // less audio than that has arrived, the front is padded with zeros so the
    // size is always frames * Channels.
    QVector<float> latestFrames(int frames) const;

    // Frames received since start(), so a consumer can tell new audio came in.
    qint64 framesReceived() const;

    // An app's name (matched against its PipeWire application name or
    // binary, case-insensitively) to capture only that app; empty captures
    // the default output's monitor. Called on every settings change, so an
    // unchanged value must do nothing.
    void setTarget(const QString &app);
    // Seconds of silence before capture pauses; 0 never pauses. Same rule:
    // an unchanged value does nothing.
    void setSleepAfter(double seconds);
    bool sleeping() const;
    double outputLatency() const;

    // The tools that watch PipeWire (default pactl and pw-dump). Tests point
    // them at fakes; call before start().
    void setWatcherPrograms(const QString &pactl, const QString &pwDump);

signals:
    void statusChanged();
    void outputLatencyChanged();

private:
    void launch();
    void readOutput();
    void consume(qint64 bytes);
    void readErrors();
    void handleProcessError(QProcess::ProcessError processError);
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void fail(const QString &message);
    void setError(const QString &message);
    void endProcess();
    void relaunch();
    QStringList recorderArguments() const;
    void readStreams(const QByteArray &json);
    void readLatency(const QByteArray &json);
    void checkSilence();
    void resetSilence();
    void setSleeping(bool sleeping);
    void clearRing();

    QString program_;
    QStringList arguments_;
    QProcess process_;
    QTimer retryTimer_;
    int retryDelayMs_ = 1000;
    // True between start() and stop(); a recorder exit only triggers a retry
    // while capture is wanted.
    bool wanted_ = false;
    QString error_;
    QString lastErrorLine_;
    QVector<float> ring_;
    qint64 samplesWritten_ = 0;
    // Reads land here; bytes after the last whole frame stay at the front
    // until the next read completes the frame.
    QByteArray readBuffer_;
    int carryBytes_ = 0;

    StreamWatcher watcher_;
    // Set while the recorder is being replaced on purpose, so its exit isn't
    // mistaken for a failure.
    bool restarting_ = false;
    QString target_;
    // The playback stream being recorded for target_, or -1 when the app
    // isn't playing anything.
    int targetSerial_ = -1;
    QVector<streams::SinkInput> streams_;
    double latency_ = 0.0;

    double sleepAfter_ = 0.0;
    bool sleeping_ = false;
    QTimer silenceTimer_;
    QElapsedTimer silenceClock_;
    qint64 samplesAtCheck_ = 0;
    qint64 quietMs_ = 0;
    qint64 stalledMs_ = 0;
    // Set by consume() when a sample is above the silence level, cleared by
    // the next silence check.
    bool heard_ = false;
    bool ringClean_ = true;
};
