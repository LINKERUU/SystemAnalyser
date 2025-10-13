QT += core gui widgets svg

CONFIG += c++17

TARGET = SystemAnalyser
TEMPLATE = app

SOURCES += \
    envirconfigpci.cpp \
    main.cpp \
    mainwindow.cpp \
    powermonitor.cpp \
    webcamera.cpp

HEADERS += \
    envirconfigpci.h \
    mainwindow.h \
    powermonitor.h \
    webcamera.h

win32 {

    LIBS += -L"C:/Program Files (x86)/Windows Kits/10/Lib/10.0.19041.0/um/x64" -lsetupapi -lpowrprof -lkernel32 -lwbemuuid -lole32 -loleaut32 -luuid
}
