#include "streams.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <csignal>
#include <sys/prctl.h>

namespace {

constexpr int StreamsDelayMs = 120;
// pw-dump lists the whole graph, so outputs are re-read less eagerly. Sinks
// also change on every suspend and resume, which is when a Bluetooth sink's
// latency becomes known.
constexpr int OutputsDelayMs = 300;
constexpr int ResubscribeMs = 3000;
constexpr int StopWaitMs = 1000;
constexpr int DefaultRate = 48000;

QJsonArray parseArray(const QByteArray &json, bool *ok = nullptr) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (ok)
        *ok = error.error == QJsonParseError::NoError && doc.isArray();
    return doc.array();
}

streams::SinkInput toSinkInput(const QJsonObject &entry) {
    const QJsonObject props = entry.value("properties").toObject();
    streams::SinkInput stream;
    bool ok = false;
    stream.serial = props.value("object.serial").toString().toInt(&ok);
    if (!ok)
        stream.serial = entry.value("index").toInt(-1);
    stream.appName = props.value("application.name").toString();
    stream.binary = props.value("application.process.binary").toString();
    stream.corked = entry.value("corked").toBool();
    return stream;
}

// pw-dump prints a Spa:String:JSON value already parsed, but older versions
// print it as a string holding JSON, so accept both.
QJsonObject jsonValueObject(const QJsonValue &value) {
    if (value.isObject())
        return value.toObject();
    return QJsonDocument::fromJson(value.toString().toUtf8()).object();
}

// The value of `key` (subject 0) in the metadata object called `name`.
QJsonValue metadataValue(const QJsonArray &dump, const QString &name, const QString &key) {
    for (const QJsonValue &object : dump) {
        const QJsonObject o = object.toObject();
        if (o.value("type").toString() != "PipeWire:Interface:Metadata"
            || o.value("props").toObject().value("metadata.name").toString() != name)
            continue;
        for (const QJsonValue &entry : o.value("metadata").toArray()) {
            const QJsonObject e = entry.toObject();
            if (e.value("subject").toInt() == 0 && e.value("key").toString() == key)
                return e.value("value");
        }
    }
    return {};
}

QJsonObject findSinkNode(const QJsonArray &dump, const QString &name) {
    for (const QJsonValue &object : dump) {
        const QJsonObject o = object.toObject();
        const QJsonObject props = o.value("info").toObject().value("props").toObject();
        if (o.value("type").toString() == "PipeWire:Interface:Node"
            && props.value("node.name").toString() == name)
            return o;
    }
    return {};
}

// The Latency entry describing what lies downstream of the sink's input.
std::optional<QJsonObject> inputLatency(const QJsonObject &info) {
    for (const QJsonValue &entry : info.value("params").toObject().value("Latency").toArray()) {
        const QJsonObject e = entry.toObject();
        if (e.value("direction").toString() == "Input")
            return e;
    }
    return std::nullopt;
}

// Adapter-wrapped sinks (ALSA, Bluetooth) copy their port's Latency onto the
// node; plain ones only have it on the ports, so look there second.
std::optional<QJsonObject> sinkLatency(const QJsonArray &dump, const QJsonObject &node) {
    if (auto own = inputLatency(node.value("info").toObject()))
        return own;
    const int id = node.value("id").toInt(-1);
    for (const QJsonValue &object : dump) {
        const QJsonObject info = object.toObject().value("info").toObject();
        if (object.toObject().value("type").toString() == "PipeWire:Interface:Port"
            && info.value("props").toObject().value("node.id").toInt(-2) == id
            && info.value("direction").toString() == "input")
            return inputLatency(info);
    }
    return std::nullopt;
}

double latencySeconds(const QJsonObject &latency, int quantumFrames, int rate) {
    const double frames = latency.value("minQuantum").toDouble() * quantumFrames
                          + latency.value("minRate").toDouble();
    return frames / rate + latency.value("minNs").toDouble() / 1e9;
}

void endProcess(QProcess &process) {
    if (process.state() == QProcess::NotRunning)
        return;
    process.terminate();
    if (!process.waitForFinished(StopWaitMs)) {
        process.kill();
        process.waitForFinished(StopWaitMs);
    }
}

} // namespace

namespace streams {

QVector<SinkInput> parseSinkInputs(const QByteArray &json) {
    QVector<SinkInput> result;
    for (const QJsonValue &entry : parseArray(json)) {
        const SinkInput stream = toSinkInput(entry.toObject());
        if (stream.serial >= 0)
            result.append(stream);
    }
    return result;
}

bool matches(const SinkInput &stream, const QString &app) {
    const QString wanted = app.trimmed();
    if (wanted.isEmpty())
        return false;
    return stream.appName.compare(wanted, Qt::CaseInsensitive) == 0
           || stream.binary.compare(wanted, Qt::CaseInsensitive) == 0;
}

int pickTarget(const QVector<SinkInput> &streams, const QString &app) {
    const SinkInput *best = nullptr;
    for (const SinkInput &stream : streams) {
        if (!matches(stream, app))
            continue;
        // Compare (playing, serial) so a playing stream beats any paused one.
        const bool better = !best || std::make_pair(!stream.corked, stream.serial)
                                         > std::make_pair(!best->corked, best->serial);
        if (better)
            best = &stream;
    }
    return best ? best->serial : -1;
}

Event classifyEvent(const QByteArray &line) {
    if (line.contains("on sink-input #"))
        return Event::Streams;
    // A new default output shows up as a change on the server.
    if (line.contains("on sink #") || line.contains("on server"))
        return Event::Outputs;
    return Event::None;
}

QString defaultSinkName(const QByteArray &pwDump) {
    const QJsonValue value = metadataValue(parseArray(pwDump), "default", "default.audio.sink");
    return jsonValueObject(value).value("name").toString();
}

std::optional<double> outputLatency(const QByteArray &pwDump, int quantumFrames) {
    bool ok = false;
    const QJsonArray dump = parseArray(pwDump, &ok);
    if (!ok)
        return std::nullopt;
    const QString sink = jsonValueObject(metadataValue(dump, "default", "default.audio.sink"))
                             .value("name").toString();
    const QJsonObject node = findSinkNode(dump, sink);
    const auto latency = node.isEmpty() ? std::nullopt : sinkLatency(dump, node);
    if (!latency)
        return 0.0;
    const int rate = metadataValue(dump, "settings", "clock.rate").toInt(DefaultRate);
    return latencySeconds(*latency, quantumFrames, rate > 0 ? rate : DefaultRate);
}

QStringList targetArguments(const QStringList &monitorArguments, int serial) {
    QStringList args;
    for (int i = 0; i < monitorArguments.size(); ++i) {
        // A sink monitor is exactly what we no longer want.
        if (monitorArguments[i] == "-P" && monitorArguments.value(i + 1) == "stream.capture.sink=true") {
            ++i;
            continue;
        }
        args << monitorArguments[i];
    }
    const QStringList extra = {"--target", QString::number(serial), "-P", "node.dont-reconnect=true",
                               "-P", "node.dont-fallback=true"};
    // pw-record wants the output file last.
    const int at = !args.isEmpty() && args.last() == "-" ? int(args.size()) - 1 : int(args.size());
    for (int i = 0; i < extra.size(); ++i)
        args.insert(at + i, extra[i]);
    return args;
}

bool isTargetGone(const QString &recorderError) {
    return recorderError.contains("target not found", Qt::CaseInsensitive);
}

void dieWithParent(QProcess &process) {
    process.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
}

} // namespace streams

StreamWatcher::StreamWatcher(QObject *parent) : QObject(parent) {
    // Nobody reads their stderr, so don't let it pile up in memory.
    for (QProcess *p : {&subscribe_, &listStreams_, &dump_}) {
        streams::dieWithParent(*p);
        p->setStandardErrorFile(QProcess::nullDevice());
    }
    for (QTimer *t : {&streamsTimer_, &outputsTimer_, &resubscribeTimer_})
        t->setSingleShot(true);
    connect(&streamsTimer_, &QTimer::timeout, this, [this] {
        runOnce(listStreams_, pactl_, {"-f", "json", "list", "sink-inputs"});
    });
    connect(&outputsTimer_, &QTimer::timeout, this, [this] { runOnce(dump_, pwDump_, {}); });
    connect(&resubscribeTimer_, &QTimer::timeout, this, [this] { subscribe_.start(pactl_, {"subscribe"}); });
    connectProcesses();
}

void StreamWatcher::connectProcesses() {
    connect(&subscribe_, &QProcess::readyReadStandardOutput, this, &StreamWatcher::readEvents);
    connect(&subscribe_, &QProcess::finished, this, &StreamWatcher::handleSubscribeFinished);
    connect(&subscribe_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            handleSubscribeFinished();
    });
    connect(&listStreams_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (code == 0 && status == QProcess::NormalExit)
            emit sinkInputsRead(listStreams_.readAllStandardOutput());
    });
    connect(&dump_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (code == 0 && status == QProcess::NormalExit)
            emit pipewireDumped(dump_.readAllStandardOutput());
    });
}

StreamWatcher::~StreamWatcher() {
    for (QProcess *p : {&subscribe_, &listStreams_, &dump_})
        p->disconnect(this);
    stop();
}

void StreamWatcher::setPrograms(const QString &pactl, const QString &pwDump) {
    pactl_ = pactl;
    pwDump_ = pwDump;
}

void StreamWatcher::start() {
    if (wanted_)
        return;
    wanted_ = true;
    subscribe_.start(pactl_, {"subscribe"});
    streamsTimer_.start(0);
    outputsTimer_.start(0);
}

void StreamWatcher::stop() {
    wanted_ = false;
    for (QTimer *t : {&streamsTimer_, &outputsTimer_, &resubscribeTimer_})
        t->stop();
    for (QProcess *p : {&subscribe_, &listStreams_, &dump_})
        endProcess(*p);
}

void StreamWatcher::refreshStreams() {
    if (wanted_ && !streamsTimer_.isActive())
        streamsTimer_.start(StreamsDelayMs);
}

void StreamWatcher::readEvents() {
    while (subscribe_.canReadLine()) {
        const streams::Event event = streams::classifyEvent(subscribe_.readLine());
        // Throttle rather than debounce: a steady stream of events must not
        // postpone the re-read forever.
        QTimer &timer = event == streams::Event::Streams ? streamsTimer_ : outputsTimer_;
        if (event != streams::Event::None && !timer.isActive())
            timer.start(event == streams::Event::Streams ? StreamsDelayMs : OutputsDelayMs);
    }
}

void StreamWatcher::handleSubscribeFinished() {
    if (!wanted_)
        return;
    resubscribeTimer_.start(ResubscribeMs);
    // Whatever changed while pactl was gone would otherwise go unnoticed.
    refreshStreams();
}

// A re-read already in flight would answer with stale data, so try again a
// little later instead of piling up processes.
void StreamWatcher::runOnce(QProcess &process, const QString &program, const QStringList &arguments) {
    if (!wanted_)
        return;
    if (process.state() != QProcess::NotRunning) {
        const bool isList = &process == &listStreams_;
        (isList ? streamsTimer_ : outputsTimer_).start(isList ? StreamsDelayMs : OutputsDelayMs);
        return;
    }
    process.start(program, arguments);
}
