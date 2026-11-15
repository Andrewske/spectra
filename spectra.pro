QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = spectra
TEMPLATE = app

LIBS += -lfftw3f -lLayerShellQtInterface

HEADERS += \
    src/capture.h \
    src/dsp.h \
    src/spectrum.h \
    src/settings.h \
    src/presets.h \
    src/streams.h \
    src/wallpaper.h

SOURCES += \
    src/main.cpp \
    src/capture.cpp \
    src/dsp.cpp \
    src/spectrum.cpp \
    src/settings.cpp \
    src/presets.cpp \
    src/streams.cpp \
    src/wallpaper.cpp

RESOURCES += src/resources.qrc
