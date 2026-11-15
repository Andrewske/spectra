#include "presets.h"

#include "settings.h"

// TODO: stub from the phase 2 scaffold; owned by the presets task.

Presets::Presets(Settings *, const QString &, QObject *parent) : QObject(parent) {}
QVariantList Presets::list() const { return {}; }
QString Presets::current() const { return {}; }
void Presets::apply(const QString &) {}
void Presets::applyIndex(int) {}
bool Presets::saveCurrent(const QString &) { return false; }
bool Presets::remove(const QString &) { return false; }
