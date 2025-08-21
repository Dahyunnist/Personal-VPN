QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    client.cpp

HEADERS += \
    mainwindow.h \
    client.h

FORMS += \
    mainwindow.ui

LIBS += -lws2_32      # 网络核心（Winsock API：htonl, socket, send 等）
LIBS += -liphlpapi    # IP 配置
LIBS += -lole32       # COM 组件
LIBS += -lcrypt32     # 证书存储
LIBS += -lboost_thread-mt       
LIBS += -lssl
LIBS += -lcrypto

qnx: target.path = /tmp/$${TARGET}/bin else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

QMAKE_PROJECT_DEPTH = 0

win32 {
    RC_FILE += app.rc  # 管理员权限等资源配置
}