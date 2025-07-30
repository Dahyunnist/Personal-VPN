QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

FORMS += \
    mainwindow.ui

# INCLUDEPATH += C:/msys64/mingw64/include

# LIBS += -LC:/msys64/mingw64/lib

# LIBS += -lzip

# 部署规则（保持默认）
qnx: target.path = /tmp/$${TARGET}/bin else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

QMAKE_PROJECT_DEPTH = 0

win32 {
    # 核心：通过 .rc 资源文件嵌入 manifest（彻底绕过 Qt 链接选项）
    RC_FILE += app.rc  # 指定资源文件，Qt 会自动调用 rc.exe 编译
}