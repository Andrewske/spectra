#include "wallpaper.h"

#include <QQuickWindow>
#include <cstring>

namespace wallpaper {

bool requested(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--wallpaper") == 0)
            return true;
    return false;
}

// TODO: stubs from the phase 2 scaffold; owned by the wallpaper task.
void prepareProcess() {}

bool attach(QQuickWindow *window, const config::Wallpaper &) {
    window->show();
    return true;
}

} // namespace wallpaper
