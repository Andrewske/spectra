#include "capture.h"

#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <cstring>

namespace {

constexpr int FrameBytes = Capture::Channels * int(sizeof(float));
constexpr int RingSamples = Capture::BufferSeconds * Capture::SampleRate * Capture::Channels;
// Big enough that one readyRead normally drains in a single read.
constexpr int ReadBufferBytes = 64 * 1024;
constexpr int FirstRetryMs = 1000;
constexpr int MaxRetryMs = 10000;
constexpr int StopWaitMs = 1000;

// Copies `count` floats from raw bytes into the ring, where `at` is the
// absolute index of the first one. memcpy rather than a float* cast because
// the source bytes are not guaranteed to be float-aligned.
void ringWrite(QVector<float> &ring, qint64 at, const char *src, qint64 count) {
    const qint64 size = ring.size();
    if (count > size) {
        // Only the newest `size` samples survive anyway.
        src += (count - size) * qint64(sizeof(float));
        at += count - size;
        count = size;
    }
    const qint64 start = at % size;
    const qint64 first = std::min(count, size - start);
    std::memcpy(ring.data() + start, src, size_t(first) * sizeof(float));
    std::memcpy(ring.data(), src + first * qint64(sizeof(float)), size_t(count - first) * sizeof(float));
}

// The last stderr line is the most useful thing to show when the recorder dies.
QString lastLine(const QByteArray &text) {
    const QList<QByteArray> lines = text.split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QByteArray line = it->trimmed();
        if (!line.isEmpty())
            return QString::fromLocal8Bit(line.left(200));
    }
    return {};
}

QString exitReason(const QString &program, int exitCode, QProcess::ExitStatus status, const QString &stderrLine) {
    if (!stderrLine.isEmpty())
        return stderrLine;
    if (status == QProcess::CrashExit)
        return QStringLiteral("%1 crashed").arg(program);
    return QStringLiteral("%1 exited with code %2").arg(program).arg(exitCode);
}

} // namespace

Capture::Capture(QObject *parent, const QString &program, const QStringList &arguments)
    : QObject(parent), program_(program), arguments_(arguments), ring_(RingSamples, 0.0f),
      readBuffer_(ReadBufferBytes + FrameBytes, '\0') {
    retryTimer_.setSingleShot(true);
    connect(&retryTimer_, &QTimer::timeout, this, &Capture::launch);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &Capture::readOutput);
    connect(&process_, &QProcess::readyReadStandardError, this, &Capture::readErrors);
    connect(&process_, &QProcess::errorOccurred, this, &Capture::handleProcessError);
    connect(&process_, &QProcess::finished, this, &Capture::handleFinished);
    connect(&process_, &QProcess::stateChanged, this, &Capture::statusChanged);
}

Capture::~Capture() {
    // No signals during teardown: QML may already be gone.
    process_.disconnect(this);
    wanted_ = false;
    retryTimer_.stop();
    endProcess();
}

QStringList Capture::defaultArguments() {
    // --raw: without it pw-record writes a .snd header first.
    // --latency 256: the default 100 ms request leaves the graph at 1024
    //   frames (one chunk per ~21 ms, slower than a 60 Hz frame); 256 gives
    //   a chunk every ~10.7 ms. It lowers the graph quantum for everyone
    //   while spectra runs, which is cheap on this machine.
    // stream.capture.sink: record the default sink's monitor, not a mic.
    // node.name / application.name: so it shows up as "spectra" in pw-top
    //   and pavucontrol, and WirePlumber restores its volume separately from
    //   other pw-record uses.
    return {QStringLiteral("--raw"), QStringLiteral("--format"), QStringLiteral("f32"),
            QStringLiteral("--rate"), QString::number(SampleRate),
            QStringLiteral("--channels"), QString::number(Channels),
            QStringLiteral("--latency"), QStringLiteral("256"),
            QStringLiteral("-P"), QStringLiteral("stream.capture.sink=true"),
            QStringLiteral("-P"), QStringLiteral("node.name=spectra"),
            QStringLiteral("-P"), QStringLiteral("application.name=spectra"),
            QStringLiteral("-")};
}

void Capture::start() {
    if (wanted_)
        return;
    wanted_ = true;
    samplesWritten_ = 0;
    retryDelayMs_ = FirstRetryMs;
    setError({});
    launch();
}

void Capture::stop() {
    wanted_ = false;
    retryTimer_.stop();
    endProcess();
    setError({});
}

bool Capture::running() const { return process_.state() == QProcess::Running; }

QString Capture::error() const { return error_; }

QVector<float> Capture::latestFrames(int frames) const {
    const qint64 wanted = qint64(std::max(0, frames)) * Channels;
    QVector<float> out(wanted, 0.0f);
    const qint64 size = ring_.size();
    const qint64 have = std::min({wanted, samplesWritten_, size});
    const qint64 start = (samplesWritten_ - have) % size;
    const qint64 first = std::min(have, size - start);
    // Newest audio goes at the end; whatever is missing stays zero at the front.
    float *dst = out.data() + (wanted - have);
    std::copy_n(ring_.constData() + start, first, dst);
    std::copy_n(ring_.constData(), have - first, dst + first);
    return out;
}

qint64 Capture::framesReceived() const { return samplesWritten_ / Channels; }

void Capture::launch() {
    // A fresh recorder starts on a frame boundary, so drop any half frame
    // left over from the previous one.
    carryBytes_ = 0;
    lastErrorLine_.clear();
    if (QStandardPaths::findExecutable(program_).isEmpty()) {
        fail(QStringLiteral("%1 isn't installed").arg(QFileInfo(program_).fileName()));
        return;
    }
    process_.start(program_, arguments_);
}

void Capture::readOutput() {
    bool gotAudio = false;
    while (process_.bytesAvailable() > 0) {
        char *dst = readBuffer_.data() + carryBytes_;
        const qint64 got = process_.read(dst, readBuffer_.size() - carryBytes_);
        if (got <= 0)
            break;
        consume(carryBytes_ + got);
        gotAudio = true;
    }
    if (gotAudio && !error_.isEmpty()) {
        retryDelayMs_ = FirstRetryMs;
        setError({});
    }
}

// Moves every whole frame in the first `bytes` of readBuffer_ into the ring
// and keeps the partial frame (a read can end mid-float) for next time.
void Capture::consume(qint64 bytes) {
    const qint64 whole = bytes - bytes % FrameBytes;
    const qint64 samples = whole / qint64(sizeof(float));
    ringWrite(ring_, samplesWritten_, readBuffer_.constData(), samples);
    samplesWritten_ += samples;
    carryBytes_ = int(bytes - whole);
    std::memmove(readBuffer_.data(), readBuffer_.constData() + whole, size_t(carryBytes_));
}

void Capture::readErrors() {
    // Keep only the latest line so a chatty recorder can't grow memory.
    const QString line = lastLine(process_.readAllStandardError());
    if (!line.isEmpty())
        lastErrorLine_ = line;
}

void Capture::handleProcessError(QProcess::ProcessError processError) {
    // Crashes also arrive through finished(); only a failed start doesn't.
    if (processError == QProcess::FailedToStart)
        fail(QStringLiteral("Audio capture couldn't start: %1").arg(process_.errorString()));
}

void Capture::handleFinished(int exitCode, QProcess::ExitStatus status) {
    if (!wanted_)
        return;
    readErrors();
    const QString name = QFileInfo(program_).fileName();
    fail(QStringLiteral("Audio capture stopped: %1").arg(exitReason(name, exitCode, status, lastErrorLine_)));
}

// Shows the problem and tries again later, doubling the wait each time so a
// broken setup doesn't spawn a process every second forever.
void Capture::fail(const QString &message) {
    setError(message);
    if (!wanted_)
        return;
    retryTimer_.start(retryDelayMs_);
    retryDelayMs_ = std::min(retryDelayMs_ * 2, MaxRetryMs);
}

void Capture::setError(const QString &message) {
    if (message == error_)
        return;
    error_ = message;
    emit statusChanged();
}

void Capture::endProcess() {
    if (process_.state() == QProcess::NotRunning)
        return;
    // SIGTERM lets pw-record disconnect from PipeWire cleanly.
    process_.terminate();
    if (!process_.waitForFinished(StopWaitMs)) {
        process_.kill();
        process_.waitForFinished(StopWaitMs);
    }
}
