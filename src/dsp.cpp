#include "dsp.h"

// TODO: stub; owned by the dsp task.

namespace dsp {
QVector<float> hann(int size) { return QVector<float>(size, 1.0f); }
QVector<float> stereoPower(const QVector<float> &, const QVector<float> &window) { return QVector<float>(window.size() / 2 + 1); }
QVector<Band> logBands(int count, float, float, int, int) { return QVector<Band>(count); }
QVector<float> bandDb(const QVector<float> &, const QVector<Band> &bands, int) { return QVector<float>(bands.size(), -120.0f); }
QVector<float> normalize(const QVector<float> &db, float, float) { return QVector<float>(db.size()); }
QVector<float> smooth(const QVector<float> &, const QVector<float> &target, float, float, float) { return target; }
QVector<float> peaks(const QVector<float> &, const QVector<float> &levels, float, float) { return levels; }
} // namespace dsp
