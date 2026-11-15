QT += core gui quick testlib
CONFIG += c++17 testcase
TARGET = spectra_tests
TEMPLATE = app

LIBS += -lfftw3f
INCLUDEPATH += ../src

HEADERS += ../src/capture.h ../src/dsp.h ../src/spectrum.h ../src/settings.h
SOURCES += \
    main.cpp \
    capture_tests.cpp \
    dsp_tests.cpp \
    settings_tests.cpp \
    ../src/capture.cpp \
    ../src/dsp.cpp \
    ../src/spectrum.cpp \
    ../src/settings.cpp
