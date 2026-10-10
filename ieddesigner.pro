TEMPLATE	= app
LANGUAGE	= C++

TARGET = ieddesigner

QT	+= core gui xml

CONFIG	+= qt thread warn_off console

win32:DEFINES	-= UNICODE

INCLUDEPATH	+= src src/core src/ui validate $$PWD/../protocol/libprot61850 $$PWD/../include/libiec61850

win32:LIBPATH	+= F:/WLMQ-Line2/NK8500_Wlmq/lib F:/libiec61850-1.5.3/build_static_x64/hal/Release
win32:LIBS	+= iec61850.lib hal.lib ws2_32.lib

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
