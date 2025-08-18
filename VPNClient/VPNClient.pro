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

# INCLUDEPATH += $$PWD   
 
# LIBS += -L$$PWD/lib 
LIBS += -lws2_32      # 网络核心（Winsock API：htonl, socket, send 等）
# LIBS += -ladvapi32    # 高级 API（服务控制、注册表，OpenSSL 依赖）
LIBS += -liphlpapi    # IP 配置（已添加）
LIBS += -lole32       # COM 组件（已添加）
LIBS += -lcrypt32     # 证书存储（已添加，解决 Cert* 函数）
# LIBS += -lwsock32     # 兼容旧 Winsock（部分 OpenSSL 版本依赖）
LIBS += -lboost_thread-mt       
LIBS += -lssl
LIBS += -lcrypto



qnx: target.path = /tmp/$${TARGET}/bin else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

QMAKE_PROJECT_DEPTH = 0

win32 {
    RC_FILE += app.rc  # 管理员权限等资源配置
}