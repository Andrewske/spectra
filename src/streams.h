#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <optional>

// Watching PipeWire from the outside (what's playing, which app, how much
// latency the output adds), so capture.cpp stays about recording. The
// parsing is free functions over pactl/pw-dump JSON so it can be tested on
// output captured from a real machine; StreamWatcher only runs the tools.
namespace streams {

// One playback stream as pipewire-pulse lists it (`pactl -f json list
// sink-inputs`). The serial is what `pw-record --target` accepts.
struct SinkInput {
    int serial = -1;
    QString appName; // application.name, e.g. "Helium"
    QString binary;  // application.process.binary, e.g. "helium"
    bool corked = false;
};

// What a `pactl subscribe` line asks the watcher to re-read.
enum class Event { None, Streams, Outputs };

QVector<SinkInput> parseSinkInputs(const QByteArray &json);

// True when `app` names the stream's application or binary, ignoring case.
bool matches(const SinkInput &stream, const QString &app);

// The serial of the stream to record for `app`, or -1 when it has none.
// Playing streams win over paused ones, and the newest wins among equals,
// because a browser leaves old, paused streams around after a new tab
// starts playing.
int pickTarget(const QVector<SinkInput> &streams, const QString &app);

Event classifyEvent(const QByteArray &line);

// The default output's node name from `pw-dump` JSON; empty when none.
QString defaultSinkName(const QByteArray &pwDump);

// Seconds between a sample entering the default output and it being heard,
// from the sink's Latency param in `pw-dump` JSON. `quantumFrames` is the
// graph's period, which pw-dump doesn't show; ALSA sinks count one of them
// plus their headroom, Bluetooth sinks report codec and transport delay in
// nanoseconds. 0 when there is no default sink; nullopt when the JSON
// doesn't parse.
std::optional<double> outputLatency(const QByteArray &pwDump, int quantumFrames);

// The recorder arguments for capturing one playback stream instead of the
// default output's monitor. Without dont-reconnect and dont-fallback,
// WirePlumber links the recorder to the default source (the microphone)
// when the app's stream goes away; with them pw-record exits instead.
QStringList targetArguments(const QStringList &monitorArguments, int serial);

// pw-record's last stderr line when its target stream disappeared.
bool isTargetGone(const QString &recorderError);

// Makes a child process die with spectra even if spectra is killed, so a
// crash doesn't leave pw-record or pactl running.
void dieWithParent(QProcess &process);

} // namespace streams

// Runs `pactl subscribe` and, a moment after anything relevant changes,
// re-reads the playback streams (pactl) or the outputs (pw-dump), handing
// the raw JSON on. Every event re-reads at most once per short window, since
// a browser starting a stream produces a burst of them.
class StreamWatcher : public QObject {
    Q_OBJECT

public:
    explicit StreamWatcher(QObject *parent = nullptr);
    ~StreamWatcher() override;

    // Tests point these at fake scripts; call before start().
    void setPrograms(const QString &pactl, const QString &pwDump);

    // Starts watching and reads both lists once right away.
    void start();
    void stop();
    // Asks for a fresh stream list soon, e.g. after the recorder lost its
    // target.
    void refreshStreams();

signals:
    void sinkInputsRead(const QByteArray &json);
    void pipewireDumped(const QByteArray &json);

private:
    void connectProcesses();
    void readEvents();
    void handleSubscribeFinished();
    void runOnce(QProcess &process, const QString &program, const QStringList &arguments);

    QString pactl_ = QStringLiteral("pactl");
    QString pwDump_ = QStringLiteral("pw-dump");
    bool wanted_ = false;
    QProcess subscribe_;
    QProcess listStreams_;
    QProcess dump_;
    QTimer streamsTimer_;
    QTimer outputsTimer_;
    QTimer resubscribeTimer_;
};
