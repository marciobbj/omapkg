QT += core gui qml quick quickcontrols2 svg testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = omarchy-update-central_tests
TEMPLATE = app

INCLUDEPATH += $$PWD/../src

HEADERS += \
    $$PWD/../src/backend.h \
    $$PWD/../src/packagemodel.h \
    $$PWD/../src/theme.h \
    $$PWD/../src/translator.h

SOURCES += \
    omarchy-update-central_tests.cpp \
    $$PWD/../src/backend.cpp \
    $$PWD/../src/packagemodel.cpp \
    $$PWD/../src/theme.cpp \
    $$PWD/../src/translator.cpp

RESOURCES += $$PWD/../src/resources.qrc
