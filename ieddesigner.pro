TEMPLATE	= app
LANGUAGE	= C++

TARGET = ieddesigner

QT	+= core gui widgets xml concurrent svg

RESOURCES += src/resources/icons.qrc

# 界面翻译：源码为英文，中文经 translate/ieddesigner_zh_CN.ts -> .qm 加载
# .qm 同时打包进 qrc 资源（部署免拷贝），运行时可被 exe 目录 translate/ 下的外部 .qm 覆盖
TRANSLATIONS += translate/ieddesigner_zh_CN.ts

# 应用图标：嵌入 exe 资源（资源管理器/任务栏显示）
RC_ICONS = src/resources/app.ico

CONFIG	+= c++17 warn_off console

# 源码统一为 UTF-8；MSVC 默认按系统 ANSI 代码页解释窄字面量，需显式指定
# （Qt5/Qt6 的 MSVC 套件均适用）
win32-msvc:QMAKE_CXXFLAGS += /utf-8

INCLUDEPATH	+= src src/core src/ui validate $$PWD/../protocol/libprot61850 $$PWD/../include/libiec61850

# libiec61850 静态库：MinGW 与本机重建的 MSVC 库（Debug/Release 分目录）分别配置
win32-g++ {
	LIBPATH += $$PWD/../libiec61850-mingw/src
	LIBS += -liec61850 -lws2_32
} else:win32 {
	CONFIG(debug, debug|release): LIBPATH += $$PWD/../libiec61850-msvc/Debug
	else: LIBPATH += $$PWD/../libiec61850-msvc/Release
	LIBS += iec61850.lib hal.lib ws2_32.lib
}

HEADERS	+= src/core/sclmodel.h \
	src/core/scltemplates.h \
	src/core/sclwriter.h \
	src/core/sclreader.h \
	src/core/browsemodel.h \
	src/core/jsonmodel.h \
	src/core/onlineclient.h \
	src/core/csvwriter.h \
	src/core/modelvalidate.h \
	src/ui/mainwindow.h \
	../protocol/libprot61850/iec61850_model_loader.h \
	../protocol/libprot61850/iec61850_model_types.h

SOURCES	+= src/main.cpp \
	src/core/sclmodel.cpp \
	src/core/scltemplates.cpp \
	src/core/sclwriter.cpp \
	src/core/sclreader.cpp \
	src/core/browsemodel.cpp \
	src/core/jsonmodel.cpp \
	src/core/onlineclient.cpp \
	src/core/csvwriter.cpp \
	src/core/modelvalidate.cpp \
	src/ui/mainwindow.cpp \
	../protocol/libprot61850/iec61850_model_loader.cpp

win32 {
  DESTDIR = bin
  # Debug/Release 中间目录分开：两套配置共用同一份 obj/moc 时，
  # nmake 会因时间戳误判“无需更新”，导致另一套配置静默不编译
  CONFIG(debug, debug|release) {
    OBJECTS_DIR = build/obj/debug
    MOC_DIR = build/moc/debug
    UI_DIR = build/ui/debug
    RCC_DIR = build/rcc/debug
  } else {
    OBJECTS_DIR = build/obj/release
    MOC_DIR = build/moc/release
    UI_DIR = build/ui/release
    RCC_DIR = build/rcc/release
  }
}
