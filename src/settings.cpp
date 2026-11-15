#include "settings.h"

// TODO: stub; owned by the ui task.

Settings::Settings(const QString &, QObject *parent) : QObject(parent) {}
QString Settings::path() const { return {}; }
QColor Settings::background() const { return Qt::black; }
QStringList Settings::colors() const { return {}; }
QString Settings::colorMode() const { return "spectrum"; }
void Settings::setColorMode(const QString &) {}
bool Settings::showPeaks() const { return true; }
void Settings::setShowPeaks(bool) {}
double Settings::barGap() const { return 0.2; }
QString Settings::error() const { return {}; }
dsp::Tuning Settings::tuning() const { return {}; }
QColor Settings::colorAt(double) const { return Qt::white; }
