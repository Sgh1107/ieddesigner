TEMPLATE	= app
LANGUAGE	= C++

TARGET = ieddesigner

QT	+= core gui widgets xml concurrent svg

RESOURCES += src/resources/icons.qrc schemas/schemas.qrc src/ui/theme.qrc

# 主题 QSS 同步复制到构建目录：运行时优先加载 exe 旁的 theme.qss，改样式免重编译
theme.files = $$PWD/src/ui/theme.qss
theme.path = $$OUT_PWD
COPIES += theme

# 界面翻译：源码为英文，中文经 translate/ieddesigner_zh_CN.ts -> .qm 加载
# .qm 同时打包进 qrc 资源（部署免拷贝），运行时可被 exe 目录 translate/ 下的外部 .qm 覆盖
TRANSLATIONS += translate/ieddesigner_zh_CN.ts

# 应用图标：嵌入 exe 资源（资源管理器/任务栏显示）
RC_ICONS = src/resources/app.ico

CONFIG	+= c++17 warn_off console

# Qt6 源码统一为 UTF-8；MSVC 默认按系统 ANSI 代码页解释窄字面量，需显式指定
win32-msvc:QMAKE_CXXFLAGS += /utf-8

# Debug 增量链接（.ilk）状态偶发损坏会导致 exe 启动即崩（崩在 __scrt_initialize_type_info，
# 与业务代码无关，全量重建可恢复）；本项目规模小，直接关闭增量链接规避该类问题
win32-msvc:QMAKE_LFLAGS_DEBUG += /INCREMENTAL:NO

INCLUDEPATH	+= src src/core src/ui validate $$PWD/../protocol/libprot61850 $$PWD/../include/libiec61850

# libxml2 静态库（XSD 校验，第三期任务 4）：MinGW 与 MSVC 分别构建，含头文件
INCLUDEPATH	+= $$PWD/../libxml2-2.12.9-install-mingw/include/libxml2
INCLUDEPATH	+= $$PWD/../libxml2-2.12.9-install-msvc/include/libxml2
win32-g++ {
	LIBPATH += $$PWD/../libiec61850-mingw/src
	LIBS += -liec61850 -lws2_32
	LIBPATH += $$PWD/../libxml2-2.12.9-install-mingw/lib
	LIBS += -lxml2
	DEFINES += LIBXML_STATIC
} else:win32 {
	CONFIG(debug, debug|release): LIBPATH += $$PWD/../libiec61850-msvc/Debug
	else: LIBPATH += $$PWD/../libiec61850-msvc/Release
	LIBS += iec61850.lib hal.lib ws2_32.lib
	# libxml2 静态库按 CRT 配套选择：Debug(/MDd → libxml2sd) / Release(/MD → libxml2s)，
	# 否则 CRT 运行库混用触发 LNK4098 警告
	CONFIG(debug, debug|release): LIBPATH += $$PWD/../libxml2-2.12.9-install-msvc-debug/lib
	else: LIBPATH += $$PWD/../libxml2-2.12.9-install-msvc/lib
	CONFIG(debug, debug|release): LIBS += libxml2sd.lib
	else: LIBS += libxml2s.lib
	DEFINES += LIBXML_STATIC
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
	src/core/xsdvalidator.h \
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
	src/core/xsdvalidator.cpp \
	src/ui/mainwindow.cpp \
	../protocol/libprot61850/iec61850_model_loader.cpp

win32 {
  DESTDIR = bin
  OBJECTS_DIR = build/obj
  MOC_DIR = build/moc
  UI_DIR = build/ui
  RCC_DIR = build/rcc
}
