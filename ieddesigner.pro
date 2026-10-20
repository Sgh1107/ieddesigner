# ieddesigner.pro —— Qt 4.8.7 / VS2013 x64（Qt4 分支，源码为 GBK 编码）
#
# 与 Qt6 分支的工程差异：
#   * Qt4 没有 QtWidgets 子模块（控件都在 QtGui 内），也没有 concurrent 子模块（QtConcurrent 属于 QtCore）；
#   * 本分支源码是 GBK，所以不能加 MSVC 的 /utf-8；
#   * Qt4.8 的 qmake 不支持 COPIES，主题 qss 改用 QMAKE_POST_LINK 复制。

TEMPLATE	= app
LANGUAGE	= C++

TARGET = ieddesigner

QT	+= core gui xml svg

RESOURCES += src/resources/icons.qrc schemas/schemas.qrc src/ui/theme.qrc

# 界面翻译：源码为英文，中文经 translate/ieddesigner_zh_CN.ts -> .qm 加载
# .qm 同时打包进 qrc 资源（部署免拷贝），运行时可用 exe 目录 translate/ 下的外部 .qm 覆盖
TRANSLATIONS += translate/ieddesigner_zh_CN.ts

# 应用图标：嵌入 exe 资源（资源管理器/任务栏显示）。Qt4.8 的 qmake 没有 RC_ICONS，
# 改成标准做法：RC_FILE 指向手写的 .rc（app.rc 与 app.ico 同目录）
win32:RC_FILE = src/resources/app.rc

CONFIG	+= qt thread warn_off console

win32:DEFINES	-= UNICODE

INCLUDEPATH	+= src src/core src/ui validate $$PWD/../protocol/libprot61850 $$PWD/../include/libiec61850

# ---- libxml2：SCL XSD 校验（第三期任务 4）------------------------------------
# xsdvalidator.cpp 用 libxml2 的 xmlSchema 校验 SCL：SCL2003 的 schema 大量使用
# xs:key / xs:unique / xs:keyref 身份约束，QtXmlPatterns 不支持，故沿用 Qt6 分支的 libxml2 实现。
# 这里按仓库旁的 libxml2 安装（Qt6 分支用的那套）自动探测；路径不同时改下面的 LIBXML2_ROOT /
# LIBXML2_LIBDIR / LIBXML2_LIB 即可。若确实找不到，工程会改编译 xsdvalidator_stub.cpp：
# 能正常编译，运行时 XSD 校验会给出“未编译”的明确提示，而不是编译/链接失败。
LIBXML2_ROOT = $$PWD/../libxml2-2.12.9-install-msvc
CONFIG(debug, debug|release) {
	LIBXML2_LIBDIR = $$PWD/../libxml2-2.12.9-install-msvc-debug/lib
	LIBXML2_LIB = libxml2sd.lib
} else {
	LIBXML2_LIBDIR = $$LIBXML2_ROOT/lib
	LIBXML2_LIB = libxml2s.lib
}

exists($$LIBXML2_ROOT/include/libxml2/libxml/xmlschemas.h):exists($$LIBXML2_LIBDIR/$$LIBXML2_LIB) {
	DEFINES += LIBXML_STATIC
	INCLUDEPATH += $$LIBXML2_ROOT/include/libxml2
	win32:LIBPATH += $$LIBXML2_LIBDIR
	win32:LIBS += $$LIBXML2_LIB
	SOURCES += src/core/xsdvalidator.cpp
} else {
	message("ieddesigner: libxml2 not found ($$LIBXML2_LIBDIR/$$LIBXML2_LIB) -> building XSD validator stub, SCL XSD validation will be unavailable")
	SOURCES += src/core/xsdvalidator_stub.cpp
}

# iec61850 / hal 静态库：沿用本分支原有构建机路径。
# 若你在本机（仓库旁有 libiec61850-msvc/Release）构建，可自行加一行：
#   win32:LIBPATH += $$PWD/../libiec61850-msvc/Release
# 注意要与 Qt4 工具链（VS2013/x64、/MD 与 /MDd）一致，否则会出现 CRT 不匹配（LNK4098 等）。
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
	src/ui/mainwindow.cpp \
	../protocol/libprot61850/iec61850_model_loader.cpp

win32 {
  DESTDIR = bin
  OBJECTS_DIR = build/obj
  MOC_DIR = build/moc
  UI_DIR = build/ui
  RCC_DIR = build/rcc

  # 主题 QSS 同步复制到构建目录：运行时优先加载 exe 旁的 theme.qss，改样式免重编译。
  # Qt4.8 的 qmake 没有 COPIES，这里用 QMAKE_POST_LINK + $$replace 把正斜杠换成反斜杠
  # （cmd 的 copy 不认正斜杠，实测）。取不到文件时运行时会退回 qrc 里的 :/ui/theme.qss。
  THEME_SRC = $$PWD/src/ui/theme.qss
  THEME_DST = $$OUT_PWD/$$DESTDIR
  THEME_SRC_WIN = $$replace(THEME_SRC, /, \\)
  THEME_DST_WIN = $$replace(THEME_DST, /, \\)
  QMAKE_POST_LINK += $$QMAKE_COPY "$$THEME_SRC_WIN" "$$THEME_DST_WIN" >NUL
}
