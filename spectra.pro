QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = spectra
TEMPLATE = app

LIBS += -lfftw3f

HEADERS += \
    src/capture.h \
    src/dsp.h \
    src/spectrum.h \
    src/settings.h

SOURCES += \
    src/main.cpp \
    src/capture.cpp \
    src/dsp.cpp \
    src/spectrum.cpp \
    src/settings.cpp

RESOURCES += src/resources.qrc
