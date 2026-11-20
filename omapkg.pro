QT += core gui qml quick quickcontrols2 svg

CONFIG += c++17 release
TARGET = omapkg
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/iconprovider.h \
    src/packagemodel.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/iconprovider.cpp \
    src/packagemodel.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
