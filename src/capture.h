#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

// Records the default output's monitor (what you hear, after EasyEffects) by
// running pw-record, and keeps the last few seconds of interleaved stereo
// float samples in a ring buffer that the spectrum reads every frame.
class Capture : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)

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

signals:
    void statusChanged();

private:
    // TODO: owned by the capture task.
};
