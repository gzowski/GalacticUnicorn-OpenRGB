# OpenRGB Galactic Unicorn plugin - qmake project
# Built against the OpenRGB source tree in $OPENRGB_SRC (default: ./build/OpenRGB)

QT += core gui widgets serialport
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = GalacticUnicornPlugin

OPENRGB_SRC = $$(OPENRGB_SRC)
isEmpty(OPENRGB_SRC): OPENRGB_SRC = $$PWD/build/OpenRGB

# Baseline include paths (used on Windows; install.sh adds more on Linux)
INCLUDEPATH += \
    $$OPENRGB_SRC \
    $$OPENRGB_SRC/RGBController \
    $$OPENRGB_SRC/i2c_smbus \
    $$OPENRGB_SRC/net_port \
    $$OPENRGB_SRC/hidapi_wrapper \
    $$OPENRGB_SRC/qt \
    $$OPENRGB_SRC/dependencies/json \
    $$OPENRGB_SRC/dependencies/hidapi \
    $$OPENRGB_SRC/dependencies/hidapi/hidapi

exists($$PWD/extra_includes.pri): include($$PWD/extra_includes.pri)

DISTFILES += plugin.json

HEADERS += \
    GalacticUnicornPlugin.h \
    GUWidget.h

SOURCES += \
    GalacticUnicornPlugin.cpp \
    GUWidget.cpp

win32 {
    DEFINES += _CRT_SECURE_NO_WARNINGS
}
