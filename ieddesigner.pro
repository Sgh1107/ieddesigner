TEMPLATE	= app
LANGUAGE	= C++

TARGET = ieddesigner

QT	+= core gui widgets xml concurrent svg

RESOURCES += src/resources/icons.qrc schemas/schemas.qrc src/ui/theme.qrc

# 界面翻译：源码为英文，中文经 translate/ieddesigner_zh_CN.ts -> .qm 加载
# .qm 同时打包进 qrc 资源（部署免拷贝），运行时可被 exe 目录 translate/ 下的外部 .qm 覆盖
TRANSLATIONS += translate/ieddesigner_zh_CN.ts

# 应用图标：嵌入 exe 资源（资源管理器/任务栏显示）
RC_ICONS = src/resources/app.ico

CONFIG	+= c++17 warn_off console

# 源码统一为 UTF-8；MSVC 默认按系统 ANSI 代码页解释窄字面量，需显式指定
# （Qt5/Qt6 的 MSVC 套件均适用；MinGW g++ 默认即按 UTF-8 处理）
win32-msvc:QMAKE_CXXFLAGS += /utf-8

# Debug 增量链接（.ilk）状态偶发损坏会导致 exe 启动即崩（如 __scrt_initialize_type_info）
# 与业务代码无关，全量重建可恢复；本项目规模小，直接关掉增量链接规避该问题
win32-msvc:QMAKE_LFLAGS_DEBUG += /INCREMENTAL:NO

INCLUDEPATH	+= src src/core src/ui validate $$PWD/../protocol/libprot61850 $$PWD/../include/libiec61850

# libxml2 静态库（XSD 校验，第三期任务 4）：MinGW 与 MSVC 分别构建，头文件路径两套都加
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
	# libxml2 静态库按 CRT 区分：Debug(/MDd → libxml2sd) / Release(/MD → libxml2s)，
	# 混用 CRT 会有 LNK4098 等告警
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
  # 主题 QSS 同步到 exe 目录（运行时优先加载 exe 旁的 theme.qss，改样式免重编译；
  # 查找顺序：exe 目录 → 上级目录 → qrc 内置 :/ui/theme.qss）
  theme.files = $$PWD/src/ui/theme.qss
  theme.path = $$OUT_PWD/$$DESTDIR
  COPIES += theme
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
