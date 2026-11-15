#include "capture.h"

// TODO: stub; owned by the capture task.

Capture::Capture(QObject *parent, const QString &, const QStringList &) : QObject(parent) {}
Capture::~Capture() = default;
QStringList Capture::defaultArguments() { return {}; }
void Capture::start() {}
void Capture::stop() {}
bool Capture::running() const { return false; }
QString Capture::error() const { return {}; }
QVector<float> Capture::latestFrames(int frames) const { return QVector<float>(frames * Channels, 0.0f); }
qint64 Capture::framesReceived() const { return 0; }
