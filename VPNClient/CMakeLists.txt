cmake_minimum_required(VERSION 3.21)

# 预定义Qt路径和构建系统
set(CMAKE_PREFIX_PATH "C:/Qt/6.5.3/mingw_64" CACHE PATH "Qt安装路径")

project(VPNClient VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt模块，自动添加头文件路径
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Network Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Network Widgets)

# 源码文件
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    client.cpp
    mainwindow.h
    client.h
    app.rc # 设置管理员权限
)

# .ui文件（绝对路径）
set(UI_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.ui  # 确保CMake能定位到.ui文件
)

# 添加可执行文件
add_executable(VPNClient
    # MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    ${UI_FILES}  # 显式传入.ui文件
)

# 手动生成moc_mainwindow.cpp
set(HEADER_FILES mainwindow.h)  # 包含Q_OBJECT的头文件
qt_wrap_cpp(MOC_SOURCES ${HEADER_FILES})  # 生成moc_mainwindow.cpp
target_sources(VPNClient PRIVATE ${MOC_SOURCES})  # 添加到目标

# 手动强制生成ui_mainwindow.h
set_target_properties(VPNClient PROPERTIES
    AUTOUIC ON  # 启用.ui自动编译
    AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}  # 搜索.ui文件的目录
    WIN32_EXECUTABLE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# 使include包含ui_mainwindow.h所在的build目录
target_include_directories(VPNClient PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# 链接库
target_link_libraries(VPNClient PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Widgets
    ws2_32 iphlpapi ole32 crypt32 advapi32
    boost_thread-mt ssl crypto
)

# 复制wintun.dll至可执行文件同级目录
set(WINTUN_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/wintun.dll")
set(DEST_DLL_PATH "${CMAKE_CURRENT_BINARY_DIR}/wintun.dll")
add_custom_command(
    TARGET VPNClient
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WINTUN_DLL_PATH}" "${DEST_DLL_PATH}"
    COMMENT "Copying wintun.dll to build directory..."
)

qt_finalize_executable(VPNClient)

# 安装配置
if(UNIX AND NOT ANDROID)
    install(TARGETS VPNClient
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()