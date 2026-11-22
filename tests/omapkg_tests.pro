QT += core gui qml quick quickcontrols2 svg network testlib

CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = omapkg_tests
TEMPLATE = app

INCLUDEPATH += $$PWD/../src

HEADERS += \
    $$PWD/../src/backend.h \
    $$PWD/../src/packagemodel.h \
    $$PWD/../src/security.h \
    $$PWD/../src/theme.h \
    $$PWD/../src/translator.h

SOURCES += \
    omapkg_tests.cpp \
    $$PWD/../src/backend.cpp \
    $$PWD/../src/packagemodel.cpp \
    $$PWD/../src/security.cpp \
    $$PWD/../src/theme.cpp \
    $$PWD/../src/translator.cpp

RESOURCES += $$PWD/../src/resources.qrc
