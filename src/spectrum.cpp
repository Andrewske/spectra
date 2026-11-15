#include "spectrum.h"

// TODO: stub; owned by the dsp task.

Spectrum::Spectrum(Source, int, QObject *parent) : QObject(parent) {}
QList<qreal> Spectrum::levels() const { return {}; }
QList<qreal> Spectrum::peaks() const { return {}; }
int Spectrum::bandCount() const { return 0; }
bool Spectrum::frozen() const { return false; }
void Spectrum::setFrozen(bool) {}
dsp::Tuning Spectrum::tuning() const { return {}; }
void Spectrum::setTuning(const dsp::Tuning &) {}
void Spectrum::tick(double) {}
