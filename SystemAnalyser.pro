QT += core gui widgets svg  # svg для анимаций

CONFIG += c++17

TARGET = SystemAnalyser
TEMPLATE = app

SOURCES += \
    envirconfigpci.cpp \
    main.cpp \
    mainwindow.cpp \
    powermonitor.cpp

HEADERS += \
    envirconfigpci.h \
    mainwindow.h \
    powermonitor.h

win32 {
    # Укажите путь к библиотекам, если они не в стандартном месте MinGW
    LIBS += -L"C:/Program Files (x86)/Windows Kits/10/Lib/10.0.19041.0/um/x64" -lpowrprof -lkernel32 -lwbemuuid
    # Или используйте стандартный путь MinGW, если библиотеки там
    # LIBS += -lpowrprof -lkernel32
}
