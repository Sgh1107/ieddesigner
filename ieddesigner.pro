TEMPLATE	= app
LANGUAGE	= C++

TARGET = ieddesigner

QT	+= core gui widgets xml concurrent

CONFIG	+= c++17 warn_off console

# Qt6 源码统一为 UTF-8；MSVC 默认按系统 ANSI 代码页解释窄字面量，需显式指定
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
  OBJECTS_DIR = build/obj
  MOC_DIR = build/moc
  UI_DIR = build/ui
  RCC_DIR = build/rcc
}
