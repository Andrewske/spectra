#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class Settings;

// Named looks: colors, mode, layout, glow, peaks and tuning in one bundle.
// Built-in presets are compiled in; the user's own live in
// ~/.config/spectra/presets/<name>.conf as partial INI files. Applying one
// writes its keys into spectra.conf, so the config always shows what's on
// screen and edits to it still apply live.
class Presets : public QObject {
    Q_OBJECT
    // [{name: string, colors: [string], builtIn: bool}], built-ins first.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    // The preset last applied, or empty once the config has been edited away from it.
    Q_PROPERTY(QString current READ current NOTIFY currentChanged)

public:
    // An empty directory means ~/.config/spectra/presets.
    explicit Presets(Settings *settings, const QString &directory = {}, QObject *parent = nullptr);

    QVariantList list() const;
    QString current() const;

    Q_INVOKABLE void apply(const QString &name);
    Q_INVOKABLE void applyIndex(int index); // 0-based position in list, for the number keys
    // Saves the current look and tuning as a user preset; false (and nothing
    // written) for an empty name or a built-in's name.
    Q_INVOKABLE bool saveCurrent(const QString &name);
    Q_INVOKABLE bool remove(const QString &name); // user presets only

signals:
    void listChanged();
    void currentChanged();

private:
    // TODO: owned by the presets task.
};
