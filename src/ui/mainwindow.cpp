#include "mainwindow.h"
#include "scltemplates.h"
#include "sclreader.h"
#include "jsonmodel.h"
#include "onlineclient.h"
#include "sclwriter.h"
#include "csvwriter.h"
#include "modelvalidate.h"

// 布尔值转 "true"/"false" 展示文本（树节点摘要与只读属性行用）
static QString boolText(bool v)
{
	return v ? QStringLiteral("true") : QStringLiteral("false");
}
#include "xsdvalidator.h"

#include <QTreeWidget>
#include <QHeaderView>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCloseEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QInputDialog>
#include <QLabel>
#include <QDir>
#include <QTime>
#include <QDate>
#include <QDateTime>
#include <QTimer>
#include <QSettings>
#include <QClipboard>
#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTabWidget>
#include <QToolBar>
#include <QScrollArea>
#include <QGridLayout>
#include <QSet>
#include <QStringList>
#include <QTreeWidget>
#include <QStyledItemDelegate>
#include <QLineEdit>
#include <map>
#include <cctype>

// 模型树：重写 edit() 在进入委托前按列/节点类型过滤，
// 不可编辑单元格直接返回 false（Qt 随后走展开等默认双击行为），
// 避免让 delegate 的 createEditor 返回空编辑器


// 详细列就地编辑委托：仅对"详细列 = 单一值"的节点类型开放编辑
// （通信参数值 / IED 名称 / DAI 值）；复合摘要节点保持只读。
// DAI 的详细文本是 "bType/fc = 值" 复合格式，编辑器只放值，提交也只取值。
class TreeDetailDelegate : public QStyledItemDelegate
{
public:
	explicit TreeDetailDelegate(QObject* parent = 0)
		: QStyledItemDelegate(parent) {}

	static bool editableKind(const QString& kind)
	{
		return kind == "capp" || kind == "ied" || kind == "dai";
	}

	static bool canEdit(const QModelIndex& index)
	{
		if (index.column() != 1)
			return false;   // 仅详细列
		const QString path = index.sibling(index.row(), 0).data(Qt::UserRole).toString();
		return editableKind(path.split(':').value(0));
	}

	// 拦截编辑入口：标题列与复合摘要节点返回空编辑器（视图自动放弃编辑）
	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
		const QModelIndex& index) const override
	{
		if (!canEdit(index))
			return nullptr;
		return QStyledItemDelegate::createEditor(parent, option, index);
	}

	void setEditorData(QWidget* editor, const QModelIndex& index) const override
	{
		QStyledItemDelegate::setEditorData(editor, index);
		const QString path = index.sibling(index.row(), 0).data(Qt::UserRole).toString();
		if (path.split(':').value(0) != "dai")
			return;
		// DAI：编辑器只放当前值（详细列文本为 "bType/fc = 值" 复合格式）
		QString text = index.data(Qt::EditRole).toString();
		if (text.contains(" = "))
		{
			if (QLineEdit* le = qobject_cast<QLineEdit*>(editor))
				le->setText(text.section(" = ", 1));
		}
	}

	void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
	{
		QStyledItemDelegate::setModelData(editor, model, index);
		// 提交后把新文本路由给主窗口（经 itemChanged 信号 → applyDetailEdit）
	}
};

class TreeWidget : public QTreeWidget
{
public:
	explicit TreeWidget(QWidget* parent = 0)
		: QTreeWidget(parent) {}

protected:
	bool edit(const QModelIndex& index, EditTrigger trigger, QEvent* event) override
	{
		if (index.column() != 1)
			return false;   // 仅详细列可编辑
		const QString path = index.sibling(index.row(), 0).data(Qt::UserRole).toString();
		if (!TreeDetailDelegate::editableKind(path.split(':').value(0)))
			return false;
		return QTreeWidget::edit(index, trigger, event);
	}
};

namespace
{
	// 属性面板编辑器类型
	enum PropEditorKind
	{
		EditNone,    // 只读
		EditText,    // 单行文本
		EditBool,    // true/false 下拉
		EditEnum,    // 枚举可选值下拉
		EditInt,     // 整数（带校验器）
		EditFloat    // 浮点（带校验器）
	};

	// 应用级剪贴板（IED/LD/LN 复制粘贴，进程内共享）
	SclLDevice g_clipLd;
	SclLn g_clipLn;
	bool g_hasClipLd = false;
	bool g_hasClipLn = false;

	struct SclIedListResult
	{
		std::vector<std::string> ieds;
		std::string err;
	};

	struct SclLoadResult
	{
		bool ok;
		std::string err;
		SclModel model;

		SclLoadResult() : ok(false) {}
	};

	// 标识符合法性: 1-64 位，字母开头，仅字母数字下划线
	bool validIdent(const std::string& s)
	{
		if (s.empty() || s.size() > 64)
			return false;
		if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')))
			return false;
		for (size_t i = 0; i < s.size(); ++i)
		{
			unsigned char c = (unsigned char)s[i];
			if (!(isalnum(c) || c == '_'))
				return false;
		}
		return true;
	}

	bool allDigits(const std::string& s)
	{
		if (s.empty())
			return false;
		for (size_t i = 0; i < s.size(); ++i) {
			if (s[i] < '0' || s[i] > '9')
				return false;
		}
		return true;
	}

	// IPv4 地址合法性: 4 段十进制，每段 1-3 位、0-255
	bool validIpv4(const std::string& s)
	{
		int parts = 0;
		size_t start = 0;
		for (size_t i = 0; i <= s.size(); ++i)
		{
			if (i == s.size() || s[i] == '.')
			{
				size_t len = i - start;
				if (len == 0 || len > 3)
					return false;
				int v = 0;
				for (size_t j = start; j < i; ++j)
				{
					if (s[j] < '0' || s[j] > '9')
						return false;
					v = v * 10 + (s[j] - '0');
				}
				if (v > 255)
					return false;
				parts++;
				start = i + 1;
			}
		}
		return parts == 4;
	}

	// 按 CDC 推断默认功能约束（引用展示用，精确 FC 需 DA 级信息）
	std::string cdcToDefaultFc(const std::string& cdc)
	{
		if (cdc == "SPS" || cdc == "DPS" || cdc == "INS" || cdc == "ENS" ||
			cdc == "ACT" || cdc == "ACD" || cdc == "SEC" || cdc == "VSS")
			return "ST";
		if (cdc == "MV" || cdc == "SAV" || cdc == "CMV" || cdc == "BCR" || cdc == "HMV")
			return "MX";
		return "CF";
	}

	void updatePParam(SclIed& ied, const char* type, const std::string& value)
	{
		for (size_t i = 0; i < ied.pParams.size(); ++i)
		{
			if (ied.pParams[i].first == type)
			{
				ied.pParams[i].second = value;
				return;
			}
		}
	}

	// IED 主接入点的通信参数同步到 Communication 段同名 ConnectedAP。
	// writer 在 subnets 非空时只从 subnets 输出 P 参数，不同步则 IED 属性页的修改落不了盘。
	void syncMainApPParams(SclModel& model)
	{
		for (size_t s = 0; s < model.subnets.size(); ++s)
		{
			for (size_t c = 0; c < model.subnets[s].aps.size(); ++c)
			{
				if (model.subnets[s].aps[c].iedName == model.ied.name)
				{
					model.subnets[s].aps[c].pParams = model.ied.pParams;
					return;
				}
			}
		}
	}
}

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
	, m_loadingProps(false)
	, m_jsonAct(0)
	, m_onlineAct(0)
	, m_openAct(0)
	, m_recentMenu(0)
	, m_browseThread(0)
{
	setWindowTitle(tr("IED²esigner"));
	setWindowIcon(QIcon(":/icons/app.png"));
	resize(1100, 700);

	m_tree = new TreeWidget(this);
	m_tree->setColumnCount(2);
	m_tree->setHeaderLabels(QStringList() << tr("Title") << tr("Detail"));
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	setCentralWidget(m_tree);

	QDockWidget* propDock = new QDockWidget(tr("Properties"), this);
	propDock->setObjectName("propDock");
	m_propScroll = new QScrollArea(this);
	m_propScroll->setWidgetResizable(true);
	m_propScroll->setObjectName("propScroll");
	m_propScroll->setFrameShape(QFrame::NoFrame);
	QWidget* propPanel = new QWidget(m_propScroll);
	propPanel->setObjectName("propPanel");
	// 关键：普通 QWidget 需开启此属性，QSS 的 background 声明才生效
	propPanel->setAttribute(Qt::WA_StyledBackground, true);
	m_propGrid = new QGridLayout(propPanel);
	m_propGrid->setContentsMargins(16, 16, 16, 16);
	m_propGrid->setHorizontalSpacing(10);
	m_propGrid->setVerticalSpacing(14);
	m_propGrid->setColumnStretch(1, 1);
	m_propScroll->setWidget(propPanel);
	propDock->setWidget(m_propScroll);
	propDock->setMinimumWidth(380);
	addDockWidget(Qt::LeftDockWidgetArea, propDock);

	QDockWidget* logDock = new QDockWidget(tr("Log"), this);
	m_log = new QPlainTextEdit(this);
	m_log->setReadOnly(true);
	logDock->setWidget(m_log);
	addDockWidget(Qt::BottomDockWidgetArea, logDock);

	initLogFile();
	loadUserTemplates();
	setupMenus();
	setupToolbar();

	connect(m_tree, SIGNAL(itemSelectionChanged()), this, SLOT(onTreeSelectionChanged()));
	connect(m_tree, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(onTreeContextMenu(const QPoint&)));
	// 详细列就地编辑（双击）：TreeWidget::edit 按列/类型过滤，
	// 委托控制可编辑类型，itemChanged 统一路由到属性行校验
	m_detailDelegate = new TreeDetailDelegate(m_tree);
	m_tree->setItemDelegate(m_detailDelegate);
	// [排查中] 双击编辑触发器与展开渲染存在交互崩溃，暂禁用，见 README 已知问题
	// m_tree->setEditTriggers(QAbstractItemView::DoubleClicked);
	connect(m_tree, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(onTreeItemChanged(QTreeWidgetItem*, int)));

	// 界面样式由 main.cpp 的 ApplyTheme 以应用级加载 theme.qss（含对话框等所有窗口），
	// 此处不再重复设置，避免两套样式级联冲突。
	m_model = MakeNewModel();
	buildTree();
	// 窗口布局（几何 + 停靠窗格状态）
	{
		QSettings settings("newfert", "ieddesigner");
		QByteArray geo = settings.value("winGeometry").toByteArray();
		if (!geo.isEmpty())
			restoreGeometry(geo);
		QByteArray st = settings.value("winState").toByteArray();
		if (!st.isEmpty())
			restoreState(st);
	}
	m_tree->header()->setStretchLastSection(false);          // 关闭最后一列自动拉伸
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);  // 第0列自适应拉伸
	m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);  // 第1列自适应拉伸
	m_tree->header()->resizeSection(0, 300);                 // 初始宽度，作为比例参考
	m_tree->header()->resizeSection(1, 500);
	log(tr("Ready. New model loaded: IED=%1").arg(QString::fromUtf8(m_model.ied.name.c_str())));
}

MainWindow::~MainWindow()
{
	logFileLine(tr("===== ieddesigner exited ====="));
	if (m_logFile.isOpen())
		m_logFile.close();
}

// 关闭时保存窗口布局（几何 + 停靠窗格状态）
void MainWindow::closeEvent(QCloseEvent* event)
{
	QSettings settings("newfert", "ieddesigner");
	settings.setValue("winGeometry", saveGeometry());
	settings.setValue("winState", saveState());
	QMainWindow::closeEvent(event);
}

void MainWindow::initLogFile()
{
	QString dir = QCoreApplication::applicationDirPath() + "/log";
	if (!QDir().mkpath(dir))
		return;
	m_logFile.setFileName(dir + "/" + QDate::currentDate().toString("yyyy-MM-dd") + ".log");
	if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
		return;
	logFileLine(tr("===== ieddesigner started (Qt %1) =====").arg(QT_VERSION_STR));
}

void MainWindow::logFileLine(const QString& text)
{
	if (!m_logFile.isOpen())
		return;
	QByteArray line = (QTime::currentTime().toString("hh:mm:ss ") + text + "\n").toUtf8();
	m_logFile.write(line);
	m_logFile.flush();
}

void MainWindow::setupToolbar()
{
	QToolBar* tb = addToolBar(tr("Main"));
	tb->setObjectName("mainToolbar");
	tb->setIconSize(QSize(20, 20));
	tb->setMovable(false);
	tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
	tb->addAction(m_actNew);
	tb->addAction(m_openAct);
	tb->addAction(m_actSave);
	tb->addSeparator();
	tb->addAction(m_actExportIcd);
	tb->addSeparator();
	tb->addAction(m_actTemplates);
	tb->addAction(m_actXml);
	addToolBarBreak(Qt::TopToolBarArea);
}

void MainWindow::setupMenus()
{
	QMenu* fileMenu = menuBar()->addMenu(tr("File"));
	m_actNew = new QAction(QIcon(":/icons/new.svg"), tr("New"), this);
	connect(m_actNew, SIGNAL(triggered()), this, SLOT(onNewModel()));
	fileMenu->addAction(m_actNew);
	m_openAct = new QAction(QIcon(":/icons/open.svg"), tr("Open SCL model"), this);
	connect(m_openAct, SIGNAL(triggered()), this, SLOT(onOpenScl()));
	fileMenu->addAction(m_openAct);
	m_recentMenu = fileMenu->addMenu(tr("Open recent"));
	rebuildRecentMenu();
	m_actSave = new QAction(QIcon(":/icons/save.svg"), tr("Save"), this);
	connect(m_actSave, SIGNAL(triggered()), this, SLOT(onSaveCid()));
	fileMenu->addAction(m_actSave);
	fileMenu->addAction(tr("Save as"), this, SLOT(onSaveCidAs()));
	QAction* closeAct = fileMenu->addAction(tr("Close"));
	closeAct->setEnabled(false);
	fileMenu->addSeparator();
	fileMenu->addAction(tr("Exit"), this, SLOT(close()));

	QMenu* toolMenu = menuBar()->addMenu(tr("Tools"));
	QAction* expIcdAct = new QAction(QIcon(":/icons/export.svg"), tr("Export ICD file"), this);
	connect(expIcdAct, SIGNAL(triggered()), this, SLOT(onExportIcd()));
	toolMenu->addAction(expIcdAct);
	m_actExportIcd = expIcdAct;
	toolMenu->addAction(tr("Export CID file"), this, SLOT(onExportCid()));
	toolMenu->addAction(tr("Export CSV point table"), this, SLOT(onExportCsv()));
	toolMenu->addSeparator();
	m_actTemplates = new QAction(QIcon(":/icons/templates.svg"), tr("Template library"), this);
	connect(m_actTemplates, SIGNAL(triggered()), this, SLOT(manageTemplates()));
	toolMenu->addAction(m_actTemplates);
	m_actXml = new QAction(QIcon(":/icons/preview.svg"), tr("XML source preview"), this);
	connect(m_actXml, SIGNAL(triggered()), this, SLOT(onXmlPreview()));
	toolMenu->addAction(m_actXml);
	toolMenu->addSeparator();
	m_jsonAct = toolMenu->addAction(tr("Import online JSON"));
	connect(m_jsonAct, SIGNAL(triggered()), this, SLOT(onImportJson()));
	m_onlineAct = toolMenu->addAction(tr("Read IED online"));
	connect(m_onlineAct, SIGNAL(triggered()), this, SLOT(onOnlineRead()));
	// 界面语言切换（重启生效）
	toolMenu->addSeparator();
	QMenu* langMenu = toolMenu->addMenu(tr("Language"));
	QAction* zhAct = langMenu->addAction(tr("中文 (Chinese)"));
	QAction* enAct = langMenu->addAction(tr("English"));
	zhAct->setCheckable(true);
	enAct->setCheckable(true);
	QSettings langSettings("newfert", "ieddesigner");
	bool useEn = (langSettings.value("language", "zh").toString() == "en");
	zhAct->setChecked(!useEn);
	enAct->setChecked(useEn);
	connect(zhAct, &QAction::triggered, this, [this]() { setUiLanguage("zh"); });
	connect(enAct, &QAction::triggered, this, [this]() { setUiLanguage("en"); });
}

// 切换界面语言：写入偏好，重启后由 SetupTranslation 生效
void MainWindow::setUiLanguage(const QString& lang)
{
	QSettings settings("newfert", "ieddesigner");
	if (settings.value("language", "zh").toString() == lang)
		return;
	settings.setValue("language", lang);
	QMessageBox::information(this, tr("Language"),
		tr("Language changed. Restart the application to take effect."));
}

void MainWindow::rebuildRecentMenu()
{
	if (m_recentMenu == 0)
		return;
	m_recentMenu->clear();
	QSettings settings("newfert", "ieddesigner");
	QStringList files = settings.value("recentFiles").toStringList();
	if (files.isEmpty())
	{
		QAction* emptyAct = m_recentMenu->addAction(tr("(None)"));
		emptyAct->setEnabled(false);
	}
	else
	{
		for (int i = 0; i < files.size(); ++i)
		{
			QString f = files[i];
			QAction* act = m_recentMenu->addAction(f);
			connect(act, &QAction::triggered, this, [this, f]() { openSclPath(f); });
		}
		m_recentMenu->addSeparator();
		QAction* clearAct = m_recentMenu->addAction(tr("Clear history"));
		connect(clearAct, &QAction::triggered, this, [this]() {
			QSettings s("newfert", "ieddesigner");
			s.remove("recentFiles");
			rebuildRecentMenu();
			log(tr("Recent file history cleared"));
		});
	}
}

// 最多保存十个历史
void MainWindow::pushRecentFile(const QString& path)
{
	QSettings settings("newfert", "ieddesigner");
	QStringList files = settings.value("recentFiles").toStringList();
	files.removeAll(path);
	files.prepend(path);
	while (files.size() > 10)
		files.removeLast();
	settings.setValue("recentFiles", files);
	rebuildRecentMenu();
}

void MainWindow::setBusy(bool busy)
{
	m_openAct->setEnabled(!busy);
	m_jsonAct->setEnabled(!busy);
	m_onlineAct->setEnabled(!busy);
}

// qrc
QIcon MainWindow::iconForPath(const QString& path) const
{
	QString kind = path.split(':').first();
	static QIcon ied(":/icons/ied.svg");
	static QIcon historyIc(":/icons/history.svg");
	static QIcon serverIc(":/icons/server.svg");
	static QIcon ldIc(":/icons/ld.svg");
	static QIcon lnIc(":/icons/ln.svg");
	static QIcon doIc(":/icons/do.svg");
	static QIcon daiIc(":/icons/dai.svg");
	static QIcon commIc(":/icons/comm.svg");
	static QIcon gseIc(":/icons/gse.svg");
	static QIcon smvIc(":/icons/smv.svg");
	static QIcon dsIc(":/icons/dataset.svg");
	static QIcon ctrlIc(":/icons/control.svg");
	static QIcon refIc(":/icons/reference.svg");
	if (kind == "ied") return ied;
	if (kind == "history" || kind == "hitem") return historyIc;
	if (kind == "ap" || kind == "cap") return serverIc;
	if (kind == "server") return serverIc;
	if (kind == "ld") return ldIc;
	if (kind == "ln" || kind == "ln0") return lnIc;
	if (kind == "do") return doIc;
	if (kind == "dai") return daiIc;
	if (kind == "comm" || kind == "subnet" || kind == "addr") return commIc;
	if (kind == "capp") return daiIc;
	if (kind == "gse" || kind == "gcb" || kind == "gse-sec") return gseIc;
	if (kind == "smv" || kind == "smcb" || kind == "smv-sec") return smvIc;
	if (kind == "ds-sec" || kind == "ds" || kind == "fcda") return dsIc;
	if (kind == "ext-sec" || kind == "extref") return refIc;
	if (kind == "cb-sec" || kind == "rpt" || kind == "rpt-sec" || kind == "rptsec" || kind == "logsec"
		|| kind == "logcb" || kind == "log-sec" || kind == "setcb" || kind == "set-sec") return ctrlIc;
	return QIcon();
}

QTreeWidgetItem* MainWindow::addNode(QTreeWidgetItem* parent, const QString& text,
	const QString& detail, const QString& path)
{
	QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
	item->setText(0, text);
	if (!detail.isEmpty())
	{
		item->setText(1, detail);
		// Detail 列弱化为次级信息（纯视觉，配合精密仪器主题的层级表达）
		item->setForeground(1, QBrush(QColor(0x7D, 0x8B, 0x9D)));
	}
	item->setData(0, Qt::UserRole, path);
	item->setIcon(0, iconForPath(path));
	applyNodeStyle(item, path);
	return item;
}

// 节点类别配色：浅色主题下的低饱和工程色，按模型层级快速区分；关键层级加粗
void MainWindow::applyNodeStyle(QTreeWidgetItem* item, const QString& path)
{
	QString kind = path.split(':').first();
	QColor main;
	const QColor detail(0x6A, 0x76, 0x86);
	bool bold = false;

	if (kind == "ied") { main = QColor(0x0F, 0x3D, 0x64); bold = true; }
	else if (kind == "ap" || kind == "server") { main = QColor(0x1B, 0x5E, 0x8A); bold = true; }
	else if (kind == "ld") { main = QColor(0x0E, 0x6B, 0x5C); bold = true; }
	else if (kind == "ln0" || kind == "ln") { main = QColor(0x24, 0x56, 0xA6); bold = true; }
	else if (kind == "do") main = QColor(0x0B, 0x6E, 0x4F);
	else if (kind == "dai" || kind == "fcda" || kind == "capp") main = QColor(0x5B, 0x6B, 0x7C);
	else if (kind == "ds-sec") { main = QColor(0x7A, 0x5D, 0x00); bold = true; }
	else if (kind == "ds") main = QColor(0x7A, 0x5D, 0x00);
	else if (kind == "rpt-sec") { main = QColor(0xB2, 0x50, 0x00); bold = true; }
	else if (kind == "rpt") main = QColor(0xB2, 0x50, 0x00);
	else if (kind == "rptsec") main = QColor(0x9A, 0x6A, 0x3C);
	else if (kind == "gse-sec") { main = QColor(0x7D, 0x3C, 0x98); bold = true; }
	else if (kind == "gcb") main = QColor(0x7D, 0x3C, 0x98);
	else if (kind == "smv-sec") { main = QColor(0x0E, 0x74, 0x90); bold = true; }
	else if (kind == "smcb") main = QColor(0x0E, 0x74, 0x90);
	else if (kind == "log-sec") { main = QColor(0x54, 0x5D, 0x6B); bold = true; }
	else if (kind == "logcb" || kind == "logsec") main = QColor(0x54, 0x5D, 0x6B);
	else if (kind == "set-sec") { main = QColor(0x8B, 0x69, 0x14); bold = true; }
	else if (kind == "setcb") main = QColor(0x8B, 0x69, 0x14);
	else if (kind == "ext-sec") { main = QColor(0x9C, 0x3B, 0x3B); bold = true; }
	else if (kind == "extref") main = QColor(0x9C, 0x3B, 0x3B);
	else if (kind == "comm" || kind == "subnet") { main = QColor(0x5B, 0x3E, 0x96); bold = true; }
	else if (kind == "cap" || kind == "addr") main = QColor(0x5B, 0x3E, 0x96);
	else if (kind == "hitem") main = QColor(0x6A, 0x76, 0x86);

	if (!main.isValid())
		return;
	item->setForeground(0, QBrush(main));
	item->setForeground(1, QBrush(detail));
	if (bold)
	{
		QFont f = item->font(0);
		f.setBold(true);
		item->setFont(0, f);
		item->setFont(1, f);
	}
}

void MainWindow::collectExpandedPaths(QTreeWidgetItem* item, QSet<QString>& out)
{
	for (int i = 0; i < item->childCount(); ++i)
	{
		QTreeWidgetItem* c = item->child(i);
		if (c->isExpanded())
			out.insert(c->data(0, Qt::UserRole).toString());
		collectExpandedPaths(c, out);
	}
}

// 恢复展开状态；若遇到选中路径的节点则返回它（供 setCurrentItem）
QTreeWidgetItem* MainWindow::restoreTreeState(QTreeWidgetItem* item, const QSet<QString>& expanded,
	const QString& selectPath) const
{
	for (int i = 0; i < item->childCount(); ++i)
	{
		QTreeWidgetItem* c = item->child(i);
		QString p = c->data(0, Qt::UserRole).toString();
		if (expanded.contains(p))
			c->setExpanded(true);
		if (!selectPath.isEmpty() && p == selectPath)
			return c;
		QTreeWidgetItem* r = restoreTreeState(c, expanded, selectPath);
		if (r)
			return r;
	}
	return 0;
}

void MainWindow::buildTree()
{
	// 重建期间抑制 itemChanged 路由（批量 setText 会触发编辑回调）
	m_loadingProps = true;
	// 保留展开状态与当前选中节点（避免属性修改后整树折叠）
	QSet<QString> expanded;
	collectExpandedPaths(m_tree->invisibleRootItem(), expanded);
	QString selectPath = m_currentPath;

	m_tree->clear();
	const SclIed& ied = m_model.ied;
	QTreeWidgetItem* iedItem = addNode(0, tr("IED ") + QString::fromUtf8(ied.name.c_str()),
		QString::fromUtf8(ied.desc.c_str()), "ied");

	// 修订历史
	if (!m_model.history.empty())
	{
		QTreeWidgetItem* histItem = addNode(iedItem, tr("Revision history"),
			tr("%1 items").arg((int)m_model.history.size()), "history");
		for (size_t i = 0; i < m_model.history.size(); ++i)
		{
			const SclHistoryItem& h = m_model.history[i];
			QString detail = QString::fromUtf8(h.when.c_str());
			if (!h.who.empty())
				detail += " " + QString::fromUtf8(h.who.c_str());
			addNode(histItem, tr("H%1 ").arg((int)i + 1) + QString::fromUtf8(h.what.c_str()),
				detail, QString("hitem:%1").arg((int)i));
		}
	}

	// 主接入点（挂 Server）与附加接入点
	QTreeWidgetItem* apItem = addNode(iedItem, tr("AccessPoint ") + QString::fromUtf8(ied.apName.c_str()),
		tr("Primary access point"), "ap:0");
	QTreeWidgetItem* svrItem = addNode(apItem, tr("Server"), QString(), "server");
	for (size_t a = 0; a < ied.extraAps.size(); ++a)
	{
		QString detail;
		if (ied.extraAps[a].serverAt.empty())
			detail = tr("Standalone access point");
		else
			detail = tr("ServerAt -> %1").arg(QString::fromUtf8(ied.extraAps[a].serverAt.c_str()));
		addNode(iedItem, tr("AccessPoint ") + QString::fromUtf8(ied.extraAps[a].name.c_str()),
			detail, QString("ap:%1").arg((int)(a + 1)));
	}

	// DO 下挂 DAI 子节点（点分路径；specs 已补全文件自定义 Struct 的 BDA 成员，
	// origin/ctlNum/Check 等均会显示；层级化展示因渲染期堆损坏暂缓，见 README 已知问题）
	auto addDaiNodes = [this](QTreeWidgetItem* doItem, int li, int ni, int di, const SclDo& dobj) {
		std::vector<SclDaiSpec> specs = daiSpecsFor(dobj);
		for (size_t k = 0; k < specs.size(); ++k)
		{
			if (specs[k].path == "d")
				continue;   // 描述节点走 DOI 描述字段，不在树上展开
			QString detail;
			if (specs[k].bType == "Struct")
				detail = tr("Struct");
			else
			{
				detail = QString::fromUtf8(specs[k].bType.c_str()) + "/" + QString::fromUtf8(specs[k].fc.c_str());
				const std::string* val = dobj.FindDaiVal(specs[k].path);
				if (val)
					detail += " = " + QString::fromUtf8(val->c_str());
			}
			addNode(doItem, tr("DAI ") + QString::fromUtf8(specs[k].path.c_str()), detail,
				QString("dai:%1:%2:%3:%4").arg(li).arg(ni).arg(di).arg((int)k));
		}
	};

	// LN 下数据集与控制块分组（仅在非空时显示）
	auto addCbSections = [&](QTreeWidgetItem* lnItem, int li, int ni, const SclLn& ln) {
		// LN 下按类型固定分组（对齐 yx：每类一组标签节点，空组也显示）
		auto makeSec = [&](const QString& title, int count, const QString& path) {
			return addNode(lnItem, title, tr("%1 item(s)").arg(count), path);
		};

		// 1. 数据集
		{
			QTreeWidgetItem* sec = makeSec(tr("DataSets"), (int)ln.dataSets.size(),
				QString("ds-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.dataSets.size(); ++s)
			{
				QTreeWidgetItem* dsItem = addNode(sec, tr("DataSet ") + QString::fromUtf8(ln.dataSets[s].name.c_str()),
					tr("%1 FCDA").arg((int)ln.dataSets[s].fcdas.size()),
					QString("ds:%1:%2:%3").arg(li).arg(ni).arg((int)s));
				// FCDA 成员挂为子节点，可展开浏览（属性面板可编辑，右键可删除）
				for (size_t k = 0; k < ln.dataSets[s].fcdas.size(); ++k)
				{
					const SclFcda& f = ln.dataSets[s].fcdas[k];
					QString detail = QString::fromUtf8(f.fc.c_str());
					if (!f.daName.empty())
						detail += tr(", DA-level");
					addNode(dsItem, tr("FCDA ") + QString::fromUtf8(f.RefText().c_str()), detail,
						QString("fcda:%1:%2:%3:%4").arg(li).arg(ni).arg((int)s).arg((int)k));
				}
			}
		}

		// 2. 报告控制块
		{
			QTreeWidgetItem* sec = makeSec(tr("Report controls"), (int)ln.reports.size(),
				QString("rpt-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.reports.size(); ++s)
			{
				const SclReportControl& rc = ln.reports[s];
				QTreeWidgetItem* rptItem = addNode(sec, tr("Report ") + QString::fromUtf8(rc.name.c_str()),
					(rc.buffered ? tr("BRCB") : tr("URCB")) + QString(" -> ") +
					QString::fromUtf8(rc.dataSet.c_str()),
					QString("rpt:%1:%2:%3").arg(li).arg(ni).arg((int)s));
				// 报告控制块的 schema 子结构挂为子节点（只读浏览，编辑走父节点属性页）
				if (rc.trgOps.present)
					addNode(rptItem, tr("TrgOps"),
						tr("dchg=%1, qchg=%2, dupd=%3, period=%4").arg(
							boolText(rc.trgOps.dchg)).arg(
							boolText(rc.trgOps.qchg)).arg(
							boolText(rc.trgOps.dupd)).arg(
							boolText(rc.trgOps.period)),
						QString("rptsec:%1:%2:%3:0").arg(li).arg(ni).arg((int)s));
				if (rc.optFields.present)
				{
					QStringList on;
					if (rc.optFields.seqNum) on << "seqNum";
					if (rc.optFields.timeStamp) on << "timeStamp";
					if (rc.optFields.dataSet) on << "dataSet";
					if (rc.optFields.reasonCode) on << "reasonCode";
					if (rc.optFields.dataRef) on << "dataRef";
					if (rc.optFields.entryID) on << "entryID";
					if (rc.optFields.bufOvfl) on << "bufOvfl";
					if (rc.optFields.config) on << "config";
					addNode(rptItem, tr("OptFields"),
						on.isEmpty() ? QString("-") : on.join(", "),
						QString("rptsec:%1:%2:%3:1").arg(li).arg(ni).arg((int)s));
				}
				if (rc.maxClients >= 0)
					addNode(rptItem, tr("RptEnabled"),
						tr("max=%1").arg(rc.maxClients),
						QString("rptsec:%1:%2:%3:2").arg(li).arg(ni).arg((int)s));
			}
		}

		// 3. GOOSE 控制块
		{
			QTreeWidgetItem* sec = makeSec(tr("GOOSE controls"), (int)ln.gses.size(),
				QString("gse-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.gses.size(); ++s)
				addNode(sec, tr("GOOSE ") + QString::fromUtf8(ln.gses[s].name.c_str()),
					QString("appID=") + QString::fromUtf8(ln.gses[s].appId.c_str()),
					QString("gcb:%1:%2:%3").arg(li).arg(ni).arg((int)s));
		}

		// 4. 采样值控制块
		{
			QTreeWidgetItem* sec = makeSec(tr("Sampled value controls"), (int)ln.smvs.size(),
				QString("smv-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.smvs.size(); ++s)
				addNode(sec, tr("SampledValue ") + QString::fromUtf8(ln.smvs[s].name.c_str()),
					QString("smvID=") + QString::fromUtf8(ln.smvs[s].smvId.c_str()),
					QString("smcb:%1:%2:%3").arg(li).arg(ni).arg((int)s));
		}

		// 5. 日志控制块
		{
			QTreeWidgetItem* sec = makeSec(tr("Log controls"), (int)ln.logs.size(),
				QString("log-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.logs.size(); ++s)
			{
				const SclLogControl& lc = ln.logs[s];
				QTreeWidgetItem* logItem = addNode(sec, tr("Log ") + QString::fromUtf8(lc.name.c_str()),
					QString::fromUtf8(lc.dataSet.c_str()),
					QString("logcb:%1:%2:%3").arg(li).arg(ni).arg((int)s));
				if (lc.trgOps.present)
					addNode(logItem, tr("TrgOps"),
						tr("dchg=%1, qchg=%2, dupd=%3, period=%4").arg(
							boolText(lc.trgOps.dchg)).arg(
							boolText(lc.trgOps.qchg)).arg(
							boolText(lc.trgOps.dupd)).arg(
							boolText(lc.trgOps.period)),
						QString("logsec:%1:%2:%3:0").arg(li).arg(ni).arg((int)s));
			}
		}

		// 6. 定值控制块
		{
			QTreeWidgetItem* sec = makeSec(tr("Setting controls"), (int)ln.settings.size(),
				QString("set-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.settings.size(); ++s)
				addNode(sec, tr("Setting ") + QString::fromUtf8(ln.settings[s].name.c_str()),
					QString::fromUtf8(ln.settings[s].dataSet.c_str()),
					QString("setcb:%1:%2:%3").arg(li).arg(ni).arg((int)s));
		}

		// 7. 外部引用（Inputs，GOOSE 订阅声明）
		{
			QTreeWidgetItem* sec = makeSec(tr("External references"), (int)ln.extRefs.size(),
				QString("ext-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.extRefs.size(); ++s)
				addNode(sec, tr("ExtRef ") + QString::fromUtf8(ln.extRefs[s].doName.c_str()),
					QString::fromUtf8(ln.extRefs[s].RefText().c_str()),
					QString("extref:%1:%2:%3").arg(li).arg(ni).arg((int)s));
		}
	};

	for (size_t li = 0; li < ied.lds.size(); ++li)
	{
		const SclLDevice& ld = ied.lds[li];
		QString ldDetail = QString::fromUtf8(ld.ldName.c_str());
		if (!ld.desc.empty())
			ldDetail += " " + QString::fromUtf8(ld.desc.c_str());
		if (!ld.comment.empty())
			ldDetail += " " + tr("[comment]");
		QTreeWidgetItem* ldItem = addNode(svrItem, tr("LDevice ") + QString::fromUtf8(ld.inst.c_str()),
			ldDetail, QString("ld:%1").arg((int)li));
		QTreeWidgetItem* ln0Item = addNode(ldItem, tr("LN0 ") + QString::fromUtf8(ld.ln0.lnClass.c_str()),
			tr("Logical node zero"), QString("ln0:%1").arg((int)li));
		for (size_t d = 0; d < ld.ln0.dos.size(); ++d)
		{
			const SclDo& dobj = ld.ln0.dos[d];
			QTreeWidgetItem* doItem = addNode(ln0Item, tr("DO ") + QString::fromUtf8(dobj.name.c_str()),
				QString::fromUtf8(dobj.cdc.c_str()) + " " + QString::fromUtf8(dobj.desc.c_str()),
				QString("do:%1:-1:%2").arg((int)li).arg((int)d));
			addDaiNodes(doItem, (int)li, -1, (int)d, dobj);
		}
		addCbSections(ln0Item, (int)li, -1, ld.ln0);
		for (size_t ni = 0; ni < ld.lns.size(); ++ni)
		{
			const SclLn& ln = ld.lns[ni];
			QString lnDetail = QString::fromUtf8(ln.desc.c_str());
			if (!ln.comment.empty())
				lnDetail += (lnDetail.isEmpty() ? QString() : QString(" ")) + tr("[comment]");
			QTreeWidgetItem* lnItem = addNode(ldItem, tr("LN ") + QString::fromUtf8(ln.Name().c_str()),
				lnDetail, QString("ln:%1:%2").arg((int)li).arg((int)ni));
			for (size_t d = 0; d < ln.dos.size(); ++d)
			{
				const SclDo& dobj = ln.dos[d];
				QTreeWidgetItem* doItem = addNode(lnItem, tr("DO ") + QString::fromUtf8(dobj.name.c_str()),
					QString::fromUtf8(dobj.cdc.c_str()) + " " + QString::fromUtf8(dobj.desc.c_str()),
					QString("do:%1:%2:%3").arg((int)li).arg((int)ni).arg((int)d));
				addDaiNodes(doItem, (int)li, (int)ni, (int)d, dobj);
			}
			addCbSections(lnItem, (int)li, (int)ni, ln);
		}
	}

	// 通信
	QTreeWidgetItem* commItem = addNode(0, tr("Communication"), tr("%1 subnets").arg((int)m_model.subnets.size()), "comm");
	for (size_t s = 0; s < m_model.subnets.size(); ++s)
	{
		const SclSubNetwork& sn = m_model.subnets[s];
		QTreeWidgetItem* snItem = addNode(commItem, tr("SubNetwork ") + QString::fromUtf8(sn.name.c_str()),
			QString::fromUtf8(sn.type.c_str()), QString("subnet:%1").arg((int)s));
		for (size_t c = 0; c < sn.aps.size(); ++c)
		{
			const SclConnectedAP& cap = sn.aps[c];
			QTreeWidgetItem* capItem = addNode(snItem, tr("AccessPoint ") + QString::fromUtf8(cap.apName.c_str()),
				QString::fromUtf8(cap.iedName.c_str()) + tr(", %1 P parameters").arg((int)cap.pParams.size()),
				QString("cap:%1:%2").arg((int)s).arg((int)c));
			// 通信参数组（Address 的 P 参数，值可在属性面板编辑）
			{
				QTreeWidgetItem* addrItem = addNode(capItem, tr("Address parameters"),
					tr("%1 P parameters").arg((int)cap.pParams.size()),
					QString("addr:%1:%2").arg((int)s).arg((int)c));
				for (size_t p = 0; p < cap.pParams.size(); ++p)
					addNode(addrItem, QString::fromUtf8(cap.pParams[p].first.c_str()),
						QString::fromUtf8(cap.pParams[p].second.c_str()),
						QString("capp:%1:%2:%3").arg((int)s).arg((int)c).arg((int)p));
			}
			for (size_t g = 0; g < cap.gses.size(); ++g)
				addNode(capItem, tr("GSE ") + QString::fromUtf8(cap.gses[g].ldInst.c_str()) +
					QString("/") + QString::fromUtf8(cap.gses[g].cbName.c_str()),
					tr("%1 address entries").arg((int)cap.gses[g].pParams.size()),
					QString("gse:%1:%2:%3").arg((int)s).arg((int)c).arg((int)g));
			for (size_t v = 0; v < cap.smvs.size(); ++v)
				addNode(capItem, tr("SMV ") + QString::fromUtf8(cap.smvs[v].ldInst.c_str()) +
					QString("/") + QString::fromUtf8(cap.smvs[v].cbName.c_str()),
					tr("%1 address entries").arg((int)cap.smvs[v].pParams.size()),
					QString("smv:%1:%2:%3").arg((int)s).arg((int)c).arg((int)v));
		}
	}
	m_tree->expandToDepth(3);
	if (!expanded.isEmpty() || !selectPath.isEmpty())
	{
		QTreeWidgetItem* sel = restoreTreeState(m_tree->invisibleRootItem(), expanded, selectPath);
		if (sel)
			m_tree->setCurrentItem(sel);
		else
			m_currentPath.clear();
	}
	m_loadingProps = false;
	if (!m_currentPath.isEmpty())
		populateProps(m_currentPath);   // 复位保护后重灌选中节点的属性面板
}

// 详细列就地编辑提交：把新文本路由到对应属性页行，复用 applyProp 的校验与写回
void MainWindow::applyDetailEdit(const QString& path, const QString& text)
{
	QStringList parts = path.split(':');
	const QString kind = parts.value(0);
	if (kind == "capp" && parts.size() == 5)
		applyProp(path, 1, text);       // 通信参数值
	else if (kind == "ied")
		applyProp(path, 0, text);       // IED 名称
	else if (kind == "dai" && parts.size() == 4)
		applyProp(path, 3, text);       // DAI 值（属性行 4）
}

// 树编辑提交（委托放行后的统一入口）：路由编辑并重建树恢复规范文本
void MainWindow::onTreeItemChanged(QTreeWidgetItem* item, int column)
{
	if (m_loadingProps || column != 1 || item == 0)
		return;
	QString path = item->data(0, Qt::UserRole).toString();
	if (path.isEmpty())
		return;
	applyDetailEdit(path, item->text(1));
	// 延迟重建：恢复树上的规范显示文本（校验被拒时自动回退原值）；
	// 不能在 itemChanged 信号栈内直接清空树（正在发信号的 item 会被销毁）
	QTimer::singleShot(0, this, [this]() { buildTree(); });
}

SclLDevice* MainWindow::ldAt(int li)
{
	if (li < 0 || li >= (int)m_model.ied.lds.size())
		return 0;
	return &m_model.ied.lds[li];
}

SclLn* MainWindow::lnAt(int li, int ni)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0)
		return 0;
	if (ni < 0)
		return &ld->ln0;
	if (ni >= (int)ld->lns.size())
		return 0;
	return &ld->lns[ni];
}

SclDo* MainWindow::doAt(int li, int ni, int di)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0 || di < 0 || di >= (int)ln->dos.size())
		return 0;
	return &ln->dos[di];
}

bool MainWindow::apNameExists(const std::string& name, int skipExtraIdx) const
{
	if (m_model.ied.apName == name)
		return true;
	for (size_t i = 0; i < m_model.ied.extraAps.size(); ++i)
	{
		if ((int)i == skipExtraIdx)
			continue;
		if (m_model.ied.extraAps[i].name == name)
			return true;
	}
	return false;
}

bool MainWindow::ldInstExists(const std::string& inst, int skipLi) const
{
	for (size_t i = 0; i < m_model.ied.lds.size(); ++i)
	{
		if ((int)i == skipLi)
			continue;
		if (m_model.ied.lds[i].inst == inst)
			return true;
	}
	return false;
}

bool MainWindow::lnExistsInLd(const SclLDevice& ld, const std::string& prefix,
	const std::string& lnClass, const std::string& inst, int skipNi) const
{
	if (ld.ln0.prefix == prefix && ld.ln0.lnClass == lnClass && ld.ln0.inst == inst)
		return true;
	for (size_t i = 0; i < ld.lns.size(); ++i)
	{
		if ((int)i == skipNi)
			continue;
		if (ld.lns[i].prefix == prefix && ld.lns[i].lnClass == lnClass && ld.lns[i].inst == inst)
			return true;
	}
	return false;
}

void MainWindow::onTreeSelectionChanged()
{
	QList<QTreeWidgetItem*> sel = m_tree->selectedItems();
	if (sel.isEmpty())
		return;
	m_currentPath = sel.first()->data(0, Qt::UserRole).toString();
	populateProps(m_currentPath);
}

void MainWindow::populateProps(const QString& path)
{
	m_loadingProps = true;

	// 清空上次的属性行；注意 QGridLayout 的行拉伸是布局属性、不会随控件删除复位，
	// 必须连同布局一起重建（否则上一次的行拉伸会作用到本次的对应行，出现大块空白）
	QWidget* panel = m_propScroll->widget();
	while (m_propGrid->count() > 0)
	{
		QLayoutItem* item = m_propGrid->takeAt(0);
		if (item->widget())
		{
			item->widget()->hide();
			item->widget()->setParent(0);
			item->widget()->deleteLater();
		}
		delete item;
	}
	delete m_propGrid;
	m_propGrid = new QGridLayout(panel);
	m_propGrid->setContentsMargins(16, 16, 16, 16);
	m_propGrid->setHorizontalSpacing(10);
	m_propGrid->setVerticalSpacing(14);

	QStringList keys;
	QStringList values;
	QList<bool> editable;
	QMap<int, int> kindMap;          // 行号 -> 编辑器类型（缺省 EditText）
	QMap<int, QStringList> optMap;   // 行号 -> 下拉选项

	QStringList parts = path.split(':');
	if (path == "ied")
	{
		const SclIed& ied = m_model.ied;
		keys << tr("IED name") << tr("Description") << tr("Manufacturer") << tr("Type") << tr("AccessPoint")
			 << tr("IP") << tr("Subnet mask") << tr("Gateway") << tr("Port");
		values << QString::fromUtf8(ied.name.c_str()) << QString::fromUtf8(ied.desc.c_str())
			<< QString::fromUtf8(ied.manufacturer.c_str()) << QString::fromUtf8(ied.type.c_str())
			<< QString::fromUtf8(ied.apName.c_str()) << QString::fromUtf8(ied.ip.c_str())
			<< QString::fromUtf8(ied.ipSubnet.c_str()) << QString::fromUtf8(ied.ipGateway.c_str())
			<< QString::number(ied.port);
		for (int i = 0; i < 9; ++i) editable << true;
		kindMap.insert(8, EditInt);   // 端口
	}
	else if (path == "history")
	{
		keys << tr("Note");
		values << tr("Right-click the IED node to add revision history; edit entries here");
		editable << false;
	}
	else if (parts.size() == 2 && parts[0] == "hitem")
	{
		int hi = parts[1].toInt();
		if (hi >= 0 && hi < (int)m_model.history.size())
		{
			const SclHistoryItem& h = m_model.history[hi];
			keys << tr("Version") << tr("What") << tr("Who") << tr("When") << tr("Why");
			values << QString::fromUtf8(h.version.c_str()) << QString::fromUtf8(h.what.c_str())
				<< QString::fromUtf8(h.who.c_str()) << QString::fromUtf8(h.when.c_str())
				<< QString::fromUtf8(h.why.c_str());
			editable << true << true << true << true << true;
		}
	}
	else if (parts.size() == 2 && parts[0] == "ap")
	{
		int ai = parts[1].toInt();
		if (ai == 0)
		{
			keys << tr("Name") << tr("Type");
			values << QString::fromUtf8(m_model.ied.apName.c_str()) << tr("Primary access point (with Server)");
			editable << true << false;
		}
		else if (ai - 1 < (int)m_model.ied.extraAps.size())
		{
			const SclAccessPoint& ap = m_model.ied.extraAps[ai - 1];
			keys << tr("Name") << tr("ServerAt") << tr("Type");
			values << QString::fromUtf8(ap.name.c_str()) << QString::fromUtf8(ap.serverAt.c_str())
				<< (ap.serverAt.empty() ? tr("Standalone access point") : tr("ServerAt reference"));
			editable << true << true << false;
		}
	}
	else if (path == "server")
	{
		keys << tr("timeout") << tr("Authentication");
		values << (m_model.ied.serverTimeout >= 0 ? QString::number(m_model.ied.serverTimeout) : QString())
			<< (m_model.ied.authentication ? "true" : "false");
		editable << true << true;
		kindMap.insert(0, EditInt);
		kindMap.insert(1, EditBool);
	}
	else if (parts.size() == 2 && parts[0] == "ld")
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		if (ld)
		{
			keys << tr("Instance") << tr("LDName") << tr("Description") << tr("Comment");
			values << QString::fromUtf8(ld->inst.c_str()) << QString::fromUtf8(ld->ldName.c_str())
				<< QString::fromUtf8(ld->desc.c_str()) << QString::fromUtf8(ld->comment.c_str());
			// LDName 无对应 SCL 属性（完整 LD 名 = IED 名 + inst），只读展示，随 Instance 联动
			editable << true << false << true << true;
		}
	}
	else if (parts[0] == "ln0" || (parts[0] == "ln" && parts.size() == 3))
	{
		bool isLn0 = (parts[0] == "ln0");
		SclLn* ln = 0;
		if (isLn0)
			ln = lnAt(parts[1].toInt(), -1);
		else
			ln = lnAt(parts[1].toInt(), parts[2].toInt());
		if (ln)
		{
			keys << tr("Prefix") << tr("LnClass") << tr("Inst") << tr("Description") << tr("Comment");
			values << QString::fromUtf8(ln->prefix.c_str()) << QString::fromUtf8(ln->lnClass.c_str())
				<< QString::fromUtf8(ln->inst.c_str()) << QString::fromUtf8(ln->desc.c_str())
				<< QString::fromUtf8(ln->comment.c_str());
			editable << true << false << !isLn0 << true << true;
		}
	}
	else if (parts.size() == 4 && parts[0] == "extref")
	{
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int idx = parts[3].toInt();
		if (ln && idx >= 0 && idx < (int)ln->extRefs.size())
		{
			const SclExtRef& er = ln->extRefs[idx];
			keys << tr("iedName (source IED)") << tr("ldInst") << tr("prefix") << tr("lnClass")
				<< tr("lnInst") << tr("doName") << tr("daName (empty = DO level)")
				<< tr("Description") << tr("intAddr");
			values << QString::fromUtf8(er.iedName.c_str()) << QString::fromUtf8(er.ldInst.c_str())
				<< QString::fromUtf8(er.prefix.c_str()) << QString::fromUtf8(er.lnClass.c_str())
				<< QString::fromUtf8(er.lnInst.c_str()) << QString::fromUtf8(er.doName.c_str())
				<< QString::fromUtf8(er.daName.c_str()) << QString::fromUtf8(er.desc.c_str())
				<< QString::fromUtf8(er.intAddr.c_str());
			editable << true << true << true << true << true << true << true << true << true;
		}
	}
	else if (parts.size() == 5 && parts[0] == "fcda")
	{
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int s = parts[3].toInt();
		int k = parts[4].toInt();
		if (ln && s >= 0 && s < (int)ln->dataSets.size() && k >= 0 && k < (int)ln->dataSets[s].fcdas.size())
		{
			const SclFcda& f = ln->dataSets[s].fcdas[k];
			keys << tr("iedName (local only, not exported)") << tr("ldInst") << tr("prefix") << tr("lnClass")
				<< tr("lnInst") << tr("doName") << tr("daName (empty = DO level)") << tr("fc");
			values << QString::fromUtf8(f.iedName.c_str()) << QString::fromUtf8(f.ldInst.c_str())
				<< QString::fromUtf8(f.prefix.c_str()) << QString::fromUtf8(f.lnClass.c_str())
				<< QString::fromUtf8(f.lnInst.c_str()) << QString::fromUtf8(f.doName.c_str())
				<< QString::fromUtf8(f.daName.c_str()) << QString::fromUtf8(f.fc.c_str());
			editable << true << true << true << true << true << true << true << true;
		}
	}
	else if (parts.size() == 5 && parts[0] == "rptsec")
	{
		// 报告控制块的 schema 子结构（只读浏览；编辑在父节点属性页）
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int s = parts[3].toInt();
		int sec = parts[4].toInt();
		if (ln && s >= 0 && s < (int)ln->reports.size())
		{
			const SclReportControl& rc = ln->reports[s];
			if (sec == 0 && rc.trgOps.present)
			{
				keys << tr("dchg (value change)") << tr("qchg (quality change)")
					<< tr("dupd (data update)") << tr("period (integrity)");
				values << boolText(rc.trgOps.dchg) << boolText(rc.trgOps.qchg)
					<< boolText(rc.trgOps.dupd) << boolText(rc.trgOps.period);
				editable << false << false << false << false;
			}
			else if (sec == 1 && rc.optFields.present)
			{
				keys << tr("seqNum") << tr("timeStamp") << tr("dataSet") << tr("reasonCode")
					<< tr("dataRef") << tr("entryID") << tr("bufOvfl (Ed2 only)") << tr("config (Ed2 only)");
				values << boolText(rc.optFields.seqNum) << boolText(rc.optFields.timeStamp)
					<< boolText(rc.optFields.dataSet) << boolText(rc.optFields.reasonCode)
					<< boolText(rc.optFields.dataRef) << boolText(rc.optFields.entryID)
					<< boolText(rc.optFields.bufOvfl) << boolText(rc.optFields.config);
				editable << false << false << false << false << false << false << false << false;
			}
			else if (sec == 2 && rc.maxClients >= 0)
			{
				keys << tr("max (client connections)");
				values << QString::number(rc.maxClients);
				editable << false;
			}
		}
	}
	else if (parts.size() == 5 && parts[0] == "logsec")
	{
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int s = parts[3].toInt();
		if (ln && s >= 0 && s < (int)ln->logs.size() && ln->logs[s].trgOps.present)
		{
			const SclLogControl& lc = ln->logs[s];
			keys << tr("dchg (value change)") << tr("qchg (quality change)")
				<< tr("dupd (data update)") << tr("period (integrity)");
			values << boolText(lc.trgOps.dchg) << boolText(lc.trgOps.qchg)
				<< boolText(lc.trgOps.dupd) << boolText(lc.trgOps.period);
			editable << false << false << false << false;
		}
	}
	else if (parts.size() == 5 && parts[0] == "capp")
	{
		// 通信参数 P 项：type 只读，值可编辑
		int sIdx = parts[1].toInt();
		int cIdx = parts[2].toInt();
		int k = parts[3].toInt();
		if (sIdx >= 0 && sIdx < (int)m_model.subnets.size()
			&& cIdx >= 0 && cIdx < (int)m_model.subnets[sIdx].aps.size()
			&& k >= 0 && k < (int)m_model.subnets[sIdx].aps[cIdx].pParams.size())
		{
			keys << tr("P type") << tr("Value");
			values << QString::fromUtf8(m_model.subnets[sIdx].aps[cIdx].pParams[k].first.c_str())
				<< QString::fromUtf8(m_model.subnets[sIdx].aps[cIdx].pParams[k].second.c_str());
			editable << false << true;
		}
	}
	else if (parts.size() == 4 && parts[0] == "do")
	{
		SclDo* dobj = doAt(parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
		if (dobj)
		{
			keys << tr("Name") << tr("CDC") << tr("Description");
			values << QString::fromUtf8(dobj->name.c_str()) << QString::fromUtf8(dobj->cdc.c_str())
				<< QString::fromUtf8(dobj->desc.c_str());
			editable << true << false << true;
		}
	}
	else if (parts.size() == 5 && parts[0] == "dai")
	{
		SclDo* dobj = doAt(parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
		if (dobj)
		{
			std::vector<SclDaiSpec> specs = daiSpecsFor(*dobj);
			int ki = parts[4].toInt();
			if (ki >= 0 && ki < (int)specs.size())
			{
				const SclDaiSpec& spec = specs[ki];
				const std::string* val = dobj->FindDaiVal(spec.path);
				keys << tr("DAI path") << tr("Basic type") << tr("FC") << tr("Value");
				values << QString::fromUtf8(spec.path.c_str())
					   << QString::fromUtf8(spec.bType.c_str())
					   << QString::fromUtf8(spec.fc.c_str())
					   << (val ? QString::fromUtf8(val->c_str()) : QString());
				editable << false << false << false << true;

				// 值控件按 bType 选择：布尔/枚举下拉，数值带校验器
				QStringList enumOpts;
				if (spec.bType == "Enum")
				{
					std::vector<std::pair<int, std::string> > evals = enumValsFor(spec.enumId);
					for (size_t e = 0; e < evals.size(); ++e)
						enumOpts << QString::fromUtf8(evals[e].second.c_str());
				}
				if (spec.bType == "BOOLEAN")
					kindMap.insert(3, EditBool);
				else if (spec.bType == "Enum" && !enumOpts.isEmpty())
				{
					kindMap.insert(3, EditEnum);
					optMap.insert(3, enumOpts);
				}
				else if (spec.bType == "FLOAT32" || spec.bType == "FLOAT64")
					kindMap.insert(3, EditFloat);
				else if (spec.bType.compare(0, 3, "INT") == 0 || spec.bType.compare(0, 4, "UINT") == 0)
					kindMap.insert(3, EditInt);
				else if (daiTypeEditable(spec.bType))
					kindMap.insert(3, EditText);
				else
				{
					kindMap.insert(3, EditNone);
					editable[3] = false;
				}
			}
		}
	}

	else if (path == "comm")
	{
		keys << tr("Note");
		values << tr("Right-click to add a subnetwork; add access points under it, then GSE/SMV addresses");
		editable << false;
	}
	else if (parts.size() == 2 && parts[0] == "subnet")
	{
		int si = parts[1].toInt();
		if (si >= 0 && si < (int)m_model.subnets.size())
		{
			keys << tr("Name") << tr("Type");
			values << QString::fromUtf8(m_model.subnets[si].name.c_str())
				<< QString::fromUtf8(m_model.subnets[si].type.c_str());
			editable << true << true;
		}
	}
	else if (parts.size() == 3 && parts[0] == "cap")
	{
		int si = parts[1].toInt();
		int ci = parts[2].toInt();
		if (si >= 0 && si < (int)m_model.subnets.size() && ci >= 0 && ci < (int)m_model.subnets[si].aps.size())
		{
			const SclConnectedAP& cap = m_model.subnets[si].aps[ci];
			keys << tr("apName") << tr("iedName");
			values << QString::fromUtf8(cap.apName.c_str()) << QString::fromUtf8(cap.iedName.c_str());
			editable << true << true;
			for (size_t p = 0; p < cap.pParams.size(); ++p)
			{
				keys << tr("P:") + QString::fromUtf8(cap.pParams[p].first.c_str());
				values << QString::fromUtf8(cap.pParams[p].second.c_str());
				editable << true;
			}
		}
	}
	else if (parts.size() == 4 && (parts[0] == "gse" || parts[0] == "smv"))
	{
		bool isGse = (parts[0] == "gse");
		int si = parts[1].toInt(), ci = parts[2].toInt(), ai = parts[3].toInt();
		SclCommAddress* addr = 0;
		if (si >= 0 && si < (int)m_model.subnets.size() && ci >= 0 && ci < (int)m_model.subnets[si].aps.size())
		{
			SclConnectedAP& cap = m_model.subnets[si].aps[ci];
			std::vector<SclCommAddress>& list = isGse ? cap.gses : cap.smvs;
			if (ai >= 0 && ai < (int)list.size())
				addr = &list[ai];
		}
		if (addr)
		{
			keys << tr("ldInst") << tr("cbName");
			values << QString::fromUtf8(addr->ldInst.c_str()) << QString::fromUtf8(addr->cbName.c_str());
			editable << true << true;
			for (size_t p = 0; p < addr->pParams.size(); ++p)
			{
				keys << tr("P:") + QString::fromUtf8(addr->pParams[p].first.c_str());
				values << QString::fromUtf8(addr->pParams[p].second.c_str());
				editable << true;
			}
			if (isGse)
			{
				keys << tr("MinTime(ms)") << tr("MaxTime(ms)");
				values << QString::fromUtf8(addr->minTime.c_str()) << QString::fromUtf8(addr->maxTime.c_str());
				editable << true << true;
			}
		}
	}
	else if (parts.size() == 4 && parts[0] == "ds")
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int di = parts[3].toInt();
		if (ld && ln && di >= 0 && di < (int)ln->dataSets.size())
		{
			const SclDataSet& ds = ln->dataSets[di];
			keys << tr("Name") << tr("Description") << tr("FCDA count");
			values << QString::fromUtf8(ds.name.c_str()) << QString::fromUtf8(ds.desc.c_str())
				   << QString::number((int)ds.fcdas.size());
			editable << true << true << false;
		}
	}
	else if (parts.size() == 4 && (parts[0] == "rpt" || parts[0] == "gcb" || parts[0] == "smcb" || parts[0] == "logcb" || parts[0] == "setcb"))
	{
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int idx = parts[3].toInt();
		if (ln && parts[0] == "rpt" && idx >= 0 && idx < (int)ln->reports.size())
		{
			const SclReportControl& rc = ln->reports[idx];
			keys << tr("Name") << tr("RptID") << tr("DataSets") << tr("confRev") << tr("intgPd(ms)") << tr("bufTime")
				 << tr("buffered") << tr("indexed") << tr("maxClients(-1 to omit)")
				 << tr("TrgOps:dchg") << tr("TrgOps:qchg") << tr("TrgOps:dupd") << tr("TrgOps:period") << tr("TrgOps:gi")
				 << tr("Opt:seqNum") << tr("Opt:timeStamp") << tr("Opt:dataSet") << tr("Opt:reasonCode")
				 << tr("Opt:dataRef") << tr("Opt:bufOvfl") << tr("Opt:entryID") << tr("Opt:config");
			values << QString::fromUtf8(rc.name.c_str()) << QString::fromUtf8(rc.rptId.c_str())
				   << QString::fromUtf8(rc.dataSet.c_str()) << QString::fromUtf8(rc.confRev.c_str())
				   << QString::fromUtf8(rc.intgPd.c_str()) << QString::fromUtf8(rc.bufTime.c_str())
				   << (rc.buffered ? "true" : "false") << (rc.indexed ? "true" : "false")
				   << (rc.maxClients >= 0 ? QString::number(rc.maxClients) : QString())
				   << (rc.trgOps.dchg ? "true" : "false") << (rc.trgOps.qchg ? "true" : "false")
				   << (rc.trgOps.dupd ? "true" : "false") << (rc.trgOps.period ? "true" : "false")
				   << (rc.trgOps.gi ? "true" : "false")
				   << (rc.optFields.seqNum ? "true" : "false") << (rc.optFields.timeStamp ? "true" : "false")
				   << (rc.optFields.dataSet ? "true" : "false") << (rc.optFields.reasonCode ? "true" : "false")
				   << (rc.optFields.dataRef ? "true" : "false") << (rc.optFields.bufOvfl ? "true" : "false")
				<< (rc.optFields.entryID ? "true" : "false") << (rc.optFields.config ? "true" : "false");
			for (int i = 0; i < keys.size(); ++i) editable << true;
			kindMap.insert(6, EditBool);
			kindMap.insert(7, EditBool);
			kindMap.insert(8, EditInt);
			for (int i = 9; i <= 13; ++i) kindMap.insert(i, EditBool);
			for (int i = 14; i <= 21; ++i) kindMap.insert(i, EditBool);
		}
		else if (ln && parts[0] == "gcb" && idx >= 0 && idx < (int)ln->gses.size())
		{
			const SclGseControl& gc = ln->gses[idx];
			keys << tr("Name") << tr("appID") << tr("DataSets") << tr("confRev") << tr("fixedOffs");
			values << QString::fromUtf8(gc.name.c_str()) << QString::fromUtf8(gc.appId.c_str())
				<< QString::fromUtf8(gc.dataSet.c_str()) << QString::fromUtf8(gc.confRev.c_str())
				<< (gc.fixedOffs ? "true" : "false");
			for (int i = 0; i < keys.size(); ++i) editable << true;
			kindMap.insert(4, EditBool);
		}
		else if (ln && parts[0] == "smcb" && idx >= 0 && idx < (int)ln->smvs.size())
		{
			const SclSampledValueControl& sc = ln->smvs[idx];
			keys << tr("Name") << tr("smvID") << tr("DataSets") << tr("confRev")
				<< tr("smpRate") << tr("nofASDU") << tr("smpMod(S/P)");
			values << QString::fromUtf8(sc.name.c_str()) << QString::fromUtf8(sc.smvId.c_str())
				<< QString::fromUtf8(sc.dataSet.c_str()) << QString::fromUtf8(sc.confRev.c_str())
				<< QString::fromUtf8(sc.smpRate.c_str()) << QString::fromUtf8(sc.nofASDU.c_str())
				<< QString::fromUtf8(sc.smpMod.c_str());
			for (int i = 0; i < keys.size(); ++i) editable << true;
			kindMap.insert(4, EditInt);
			kindMap.insert(5, EditInt);
			kindMap.insert(6, EditEnum);
			optMap.insert(6, QStringList() << "S" << "P");
		}
		else if (ln && parts[0] == "logcb" && idx >= 0 && idx < (int)ln->logs.size())
		{
			const SclLogControl& lc = ln->logs[idx];
			keys << tr("Name") << tr("DataSets") << tr("logName") << tr("intgPd(ms)")
				<< tr("TrgOps:dchg") << tr("TrgOps:qchg") << tr("TrgOps:dupd") << tr("TrgOps:period") << tr("TrgOps:gi");
			values << QString::fromUtf8(lc.name.c_str()) << QString::fromUtf8(lc.dataSet.c_str())
				<< QString::fromUtf8(lc.logName.c_str()) << QString::fromUtf8(lc.intgPd.c_str())
				<< (lc.trgOps.dchg ? "true" : "false") << (lc.trgOps.qchg ? "true" : "false")
				<< (lc.trgOps.dupd ? "true" : "false") << (lc.trgOps.period ? "true" : "false")
				<< (lc.trgOps.gi ? "true" : "false");
			for (int i = 0; i < keys.size(); ++i) editable << true;
			for (int i = 4; i <= 8; ++i) kindMap.insert(i, EditBool);
		}
		else if (ln && parts[0] == "setcb" && idx >= 0 && idx < (int)ln->settings.size())
		{
			const SclSettingControl& sc = ln->settings[idx];
			keys << tr("Name") << tr("DataSets") << tr("numOfSG");
			values << QString::fromUtf8(sc.name.c_str()) << QString::fromUtf8(sc.dataSet.c_str())
				<< QString::fromUtf8(sc.numOfSG.c_str());
			for (int i = 0; i < keys.size(); ++i) editable << true;
		}
	}
	else if (parts.size() == 3 && (parts[0] == "ds-sec" || parts[0] == "cb-sec"))
	{
		keys << tr("Note");
		values << tr("Right-click the logical node to add");
		editable << false;
	}

	// 表单渲染：每行独立 HBox（标签最小宽 150 + 编辑器伸展），杜绝文字与输入框重叠
	auto makeCombo = [this, path](const QStringList& opts, const QString& cur, int row) -> QComboBox* {
		QComboBox* cb = new QComboBox(m_propScroll->widget());
		cb->addItems(opts);
		int ix = opts.indexOf(cur);
		if (ix < 0 && !cur.isEmpty())
		{
			cb->insertItem(0, cur);
			ix = 0;
		}
		cb->setCurrentIndex(ix < 0 ? 0 : ix);
		connect(cb, &QComboBox::currentTextChanged, this, [this, path, row](const QString& v) {
			if (m_loadingProps) return;
			applyProp(path, row, v);
		});
		return cb;
	};
	auto makeText = [this, path](const QString& cur, int row, bool ro, QLineEdit*& outLe) -> QLineEdit* {
		QLineEdit* le = new QLineEdit(cur, m_propScroll->widget());
		le->setReadOnly(ro);
		if (!ro)
		{
			const QString orig = cur;
			connect(le, &QLineEdit::editingFinished, this, [this, path, row, le, orig]() {
				if (m_loadingProps || le->text() == orig) return;
				applyProp(path, row, le->text());
			});
		}
		outLe = le;
		return le;
	};

	int row = 0;
	for (int i = 0; i < keys.size(); ++i)
	{
		bool canEdit = (i < editable.size() && editable[i]);
		int kind = kindMap.value(i, canEdit ? (int)EditText : (int)EditNone);
		QStringList opts = optMap.value(i);
		if (!canEdit)
			kind = EditNone;

		QWidget* rowW = new QWidget(m_propScroll->widget());
		rowW->setObjectName("propRow");
		QHBoxLayout* rl = new QHBoxLayout(rowW);
		rl->setContentsMargins(0, 0, 0, 0);
		rl->setSpacing(10);

		QLabel* lab = new QLabel(keys[i], rowW);
		lab->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		lab->setMinimumWidth(150);
		rl->addWidget(lab);

		QWidget* ed = 0;
		if (kind == EditNone)
		{
			QLineEdit* le = 0;
			ed = makeText(values[i], i, true, le);
		}
		else if (kind == EditBool)
		{
			ed = makeCombo(QStringList() << "true" << "false", values[i], i);
		}
		else if (kind == EditEnum)
		{
			ed = makeCombo(opts, values[i], i);
		}
		else if (kind == EditInt)
		{
			QLineEdit* le = 0;
			ed = makeText(values[i], i, false, le);
			le->setValidator(new QIntValidator(le));
		}
		else if (kind == EditFloat)
		{
			QLineEdit* le = 0;
			ed = makeText(values[i], i, false, le);
			QDoubleValidator* dv = new QDoubleValidator(le);
			dv->setLocale(QLocale::c());
			le->setValidator(dv);
		}
		else
		{
			QLineEdit* le = 0;
			ed = makeText(values[i], i, false, le);
		}

		rl->addWidget(ed, 1);
		m_propGrid->addWidget(rowW, row, 0, 1, 2);
		++row;
	}
	// 尾部弹簧：吸收剩余高度，属性行靠上排列
	QWidget* spacer = new QWidget(panel);
	spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	m_propGrid->addWidget(spacer, row, 0, 1, 2);
	m_loadingProps = false;
}

void MainWindow::applyProp(const QString& path, int row, const QString& value)
{
	std::string v = value.toUtf8().constData();
	QStringList parts = path.split(':');

	if (path == "ied")
	{
		SclIed& ied = m_model.ied;
		switch (row)
		{
		case 0:
			if (!validIdent(v))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("IED name must be 1-64 chars, start with a letter, letters/digits/underscore only"));
				populateProps(path);
				return;
			}
			ied.name = v;
			break;
		case 1: ied.desc = v; break;
		case 2: ied.manufacturer = v; break;
		case 3: ied.type = v; break;
		case 4:
			if (!validIdent(v))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("AccessPoint name must be 1-64 chars, start with a letter, letters/digits/underscore only"));
				populateProps(path);
				return;
			}
			ied.apName = v;
			break;
		case 5: ied.ip = v; updatePParam(ied, "IP", v); break;
		case 6: ied.ipSubnet = v; updatePParam(ied, "IP-SUBNET", v); break;
		case 7: ied.ipGateway = v; updatePParam(ied, "IP-GATEWAY", v); break;
		case 8:
		{
			bool ok = false;
			int port = value.toInt(&ok);
			if (!ok || port <= 0 || port > 65535)
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("Port must be an integer between 1 and 65535"));
				populateProps(path);
				return;
			}
			ied.port = port;
			updatePParam(ied, "MMS-Port", v);
			break;
		}
		default: return;
		}
		// IP/掩码/网关/端口修改后同步 Communication 段（导出时以 subnets 为准）
		if (row >= 5 && row <= 8)
			syncMainApPParams(m_model);
	}
	else if (path == "server")
	{
		if (row == 0)
		{
			if (value.trimmed().isEmpty())
				m_model.ied.serverTimeout = -1;
			else
			{
				bool ok = false;
				int t = value.toInt(&ok);
				if (!ok || t < 0)
				{
					QMessageBox::warning(this, tr("Validation failed"), tr("Timeout must be a non-negative integer or empty"));
					populateProps(path);
					return;
				}
				m_model.ied.serverTimeout = t;
			}
		}
		else if (row == 1)
		{
			QString vs = value.trimmed().toLower();
			if (vs == "true" || vs == "1" || vs == tr("Yes"))
				m_model.ied.authentication = true;
			else if (vs == "false" || vs == "0" || vs == tr("No"))
				m_model.ied.authentication = false;
			else
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("Authentication: enter true or false"));
				populateProps(path);
				return;
			}
		}
		else return;
	}
	else if (parts.size() == 2 && parts[0] == "hitem")
	{
		int hi = parts[1].toInt();
		if (hi < 0 || hi >= (int)m_model.history.size()) return;
		switch (row)
		{
		case 0: m_model.history[hi].version = v; break;
		case 1: m_model.history[hi].what = v; break;
		case 2: m_model.history[hi].who = v; break;
		case 3: m_model.history[hi].when = v; break;
		case 4: m_model.history[hi].why = v; break;
		default: return;
		}
	}
	else if (parts.size() == 2 && parts[0] == "ap")
	{
		int ai = parts[1].toInt();
		if (ai == 0)
		{
			if (row != 0) return;
			if (!validIdent(v) || apNameExists(v, -1))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid and unique"));
				populateProps(path);
				return;
			}
			m_model.ied.apName = v;
		}
		else
		{
			int ei = ai - 1;
			if (ei >= (int)m_model.ied.extraAps.size()) return;
			if (row == 0)
			{
				if (!validIdent(v) || apNameExists(v, ei))
				{
					QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid and unique"));
					populateProps(path);
					return;
				}
				m_model.ied.extraAps[ei].name = v;
			}
			else if (row == 1)
			{
				if (!v.empty() && apNameExists(v, ei))
				{
					QMessageBox::warning(this, tr("Validation failed"), tr("ServerAt must reference an existing access point name"));
					populateProps(path);
					return;
				}
				m_model.ied.extraAps[ei].serverAt = v;
			}
			else return;
		}
	}
	else if (parts.size() == 2 && parts[0] == "ld")
	{
		int li = parts[1].toInt();
		SclLDevice* ld = ldAt(li);
		if (ld == 0) return;
		if (row == 0)
		{
			if (!validIdent(v) || ldInstExists(v, li))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("Instance must be valid and unique in the file"));
				populateProps(path);
				return;
			}
			ld->inst = v;
			ld->ldName = v;
		}
		else if (row == 1) ld->ldName = v;
		else if (row == 2) ld->desc = v;
		else if (row == 3) ld->comment = v;
		else return;
	}
	else if (parts[0] == "ln0" || parts[0] == "ln")
	{
		bool isLn0 = (parts[0] == "ln0");
		int li = parts[1].toInt();
		int ni = isLn0 ? -1 : parts[2].toInt();
		SclLn* ln = lnAt(li, ni);
		SclLDevice* ld = ldAt(li);
		if (ln == 0 || ld == 0) return;
		if (row == 0)
		{
			if (!v.empty() && !validIdent(v))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("Prefix may contain letters/digits/underscore only"));
				populateProps(path);
				return;
			}
			ln->prefix = v;
		}
		else if (row == 2 && !isLn0)
		{
			if (!allDigits(v))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("Inst must be a non-empty number"));
				populateProps(path);
				return;
			}
			if (lnExistsInLd(*ld, ln->prefix, ln->lnClass, v, ni))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("prefix and inst combination duplicates an existing LN, please change"));
				populateProps(path);
				return;
			}
			ln->inst = v;
		}
		else if (row == 3) ln->desc = v;
		else if (row == 4) ln->comment = v;
		else return;
	}
	else if (parts.size() == 4 && parts[0] == "do")
	{
		SclDo* dobj = doAt(parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
		if (dobj == 0) return;
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		if (row == 0)
		{
			if (ln == 0) return;
			if (!validIdent(v))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("DO name must be 1-64 chars, start with a letter, letters/digits/underscore only"));
				populateProps(path);
				return;
			}
			for (size_t i = 0; i < ln->dos.size(); ++i)
			{
				if (ln->dos[i].name == v)
				{
					QMessageBox::warning(this, tr("Validation failed"), tr("DO name must be unique within this logical node"));
					populateProps(path);
					return;
				}
			}
			dobj->name = v;
		}
		else if (row == 2) dobj->desc = v;
		else return;
	}
	else if (parts.size() == 5 && parts[0] == "dai")
	{
		SclDo* dobj = doAt(parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
		if (dobj == 0 || row != 3) return;
		std::vector<SclDaiSpec> specs = daiSpecsFor(*dobj);
		int ki = parts[4].toInt();
		if (ki < 0 || ki >= (int)specs.size()) return;
		const SclDaiSpec& spec = specs[ki];
		QString normalized;
		if (!daiValueValid(spec, value, normalized))
		{
			QMessageBox::warning(this, tr("Validation failed"),
				tr("Value does not match type %1%2").arg(QString::fromUtf8(spec.bType.c_str()))
					.arg(spec.bType == "Enum" ? tr(", see allowed values in the property page") : QString()));
			populateProps(path);
			return;
		}
		dobj->SetDaiVal(spec.path, normalized.toUtf8().constData());
	}
	else if (parts.size() == 5 && parts[0] == "capp")
	{
		int sIdx = parts[1].toInt();
		int cIdx = parts[2].toInt();
		int k = parts[3].toInt();
		if (sIdx < 0 || sIdx >= (int)m_model.subnets.size()
			|| cIdx < 0 || cIdx >= (int)m_model.subnets[sIdx].aps.size()
			|| k < 0 || k >= (int)m_model.subnets[sIdx].aps[cIdx].pParams.size())
			return;
		if (row == 1)
		{
			if (v.empty())
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("Value cannot be empty"));
				populateProps(path);
				return;
			}
			m_model.subnets[sIdx].aps[cIdx].pParams[k].second = v;
		}
	}
	else if (parts.size() == 5 && parts[0] == "fcda")
	{
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int s = parts[3].toInt();
		int k = parts[4].toInt();
		if (ln == 0 || s < 0 || s >= (int)ln->dataSets.size() || k < 0 || k >= (int)ln->dataSets[s].fcdas.size())
			return;
		SclFcda& f = ln->dataSets[s].fcdas[k];
		switch (row)
		{
		case 0: f.iedName = v; break;   // 仅本地，不导出
		case 1:
			if (!v.empty() && !validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("ldInst must be valid")); populateProps(path); return; }
			f.ldInst = v; break;
		case 2: if (!v.empty() && !validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("Prefix may contain letters/digits/underscore only")); populateProps(path); return; } f.prefix = v; break;
		case 3:
			if (v.empty() || !validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("lnClass must be valid")); populateProps(path); return; }
			f.lnClass = v; break;
		case 4: if (!v.empty() && !allDigits(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("lnInst must be digits")); populateProps(path); return; } f.lnInst = v; break;
		case 5:
			if (v.empty() || !validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("doName must be valid")); populateProps(path); return; }
			f.doName = v; break;
		case 6: if (!v.empty() && !validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("daName must be valid")); populateProps(path); return; } f.daName = v; break;
		case 7: f.fc = v; break;
		default: return;
		}
	}
	else if (parts.size() == 4 && parts[0] == "extref")
	{
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int idx = parts[3].toInt();
		if (ln == 0 || idx < 0 || idx >= (int)ln->extRefs.size())
			return;
		SclExtRef& er = ln->extRefs[idx];
		switch (row)
		{
		case 0:
			if (v.empty()) { QMessageBox::warning(this, tr("Validation failed"), tr("iedName cannot be empty")); populateProps(path); return; }
			er.iedName = v; break;
		case 1: er.ldInst = v; break;
		case 2: if (!v.empty() && !validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("Prefix may contain letters/digits/underscore only")); populateProps(path); return; } er.prefix = v; break;
		case 3:
			if (v.empty()) { QMessageBox::warning(this, tr("Validation failed"), tr("lnClass cannot be empty")); populateProps(path); return; }
			if (!validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("lnClass must be valid")); populateProps(path); return; }
			er.lnClass = v; break;
		case 4: if (!v.empty() && !allDigits(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("lnInst must be digits")); populateProps(path); return; } er.lnInst = v; break;
		case 5:
			if (v.empty()) { QMessageBox::warning(this, tr("Validation failed"), tr("doName cannot be empty")); populateProps(path); return; }
			if (!validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("doName must be valid")); populateProps(path); return; }
			er.doName = v; break;
		case 6: if (!v.empty() && !validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("daName must be valid")); populateProps(path); return; } er.daName = v; break;
		case 7: er.desc = v; break;
		case 8: er.intAddr = v; break;
		default: return;
		}
	}
	else if (parts.size() == 2 && parts[0] == "subnet")
	{
		int si = parts[1].toInt();
		if (si < 0 || si >= (int)m_model.subnets.size()) return;
		if (row == 0)
		{
			if (value.trimmed().isEmpty())
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("SubNetwork name cannot be empty"));
				populateProps(path);
				return;
			}
			m_model.subnets[si].name = v;
		}
		else if (row == 1) m_model.subnets[si].type = v;
		else return;
	}
	else if (parts.size() == 3 && parts[0] == "cap")
	{
		int si = parts[1].toInt(), ci = parts[2].toInt();
		if (si < 0 || si >= (int)m_model.subnets.size() || ci < 0 || ci >= (int)m_model.subnets[si].aps.size()) return;
		SclConnectedAP& cap = m_model.subnets[si].aps[ci];
		if (row == 0) cap.apName = v;
		else if (row == 1) cap.iedName = v;
		else if (row >= 2 && row - 2 < (int)cap.pParams.size())
		{
			cap.pParams[row - 2].second = v;
			// 主接入点的 P 参数回写 IED 字段（IED 属性页显示/旧格式导出用）
			if (cap.iedName == m_model.ied.name)
			{
				m_model.ied.pParams = cap.pParams;
				for (size_t p = 0; p < cap.pParams.size(); ++p)
				{
					const std::string& type = cap.pParams[p].first;
					const std::string& pval = cap.pParams[p].second;
					if (type == "IP") m_model.ied.ip = pval;
					else if (type == "IP-SUBNET") m_model.ied.ipSubnet = pval;
					else if (type == "IP-GATEWAY") m_model.ied.ipGateway = pval;
					else if (type == "MMS-Port") m_model.ied.port = QString::fromUtf8(pval.c_str()).toInt();
				}
			}
		}
		else return;
	}
	else if (parts.size() == 4 && (parts[0] == "gse" || parts[0] == "smv"))
	{
		bool isGse = (parts[0] == "gse");
		int si = parts[1].toInt(), ci = parts[2].toInt(), ai = parts[3].toInt();
		if (si < 0 || si >= (int)m_model.subnets.size() || ci < 0 || ci >= (int)m_model.subnets[si].aps.size()) return;
		SclConnectedAP& cap = m_model.subnets[si].aps[ci];
		std::vector<SclCommAddress>& list = isGse ? cap.gses : cap.smvs;
		if (ai < 0 || ai >= (int)list.size()) return;
		SclCommAddress& addr = list[ai];
		int pCount = (int)addr.pParams.size();
		if (row == 0) addr.ldInst = v;
		else if (row == 1) addr.cbName = v;
		else if (row >= 2 && row - 2 < pCount) addr.pParams[row - 2].second = v;
		else if (isGse && row == 2 + pCount) addr.minTime = v;
		else if (isGse && row == 3 + pCount) addr.maxTime = v;
		else return;
	}
	else if (parts.size() == 4 && parts[0] == "ds")
	{
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int di = parts[3].toInt();
		if (ln == 0 || di < 0 || di >= (int)ln->dataSets.size()) return;
		if (row == 0)
		{
			if (!validIdent(v))
			{
				QMessageBox::warning(this, tr("Validation failed"), tr("DataSet name must be valid"));
				populateProps(path);
				return;
			}
			ln->dataSets[di].name = v;
		}
		else if (row == 1) ln->dataSets[di].desc = v;
		else return;
	}
	else if (parts.size() == 4 && (parts[0] == "rpt" || parts[0] == "gcb" || parts[0] == "smcb" || parts[0] == "logcb" || parts[0] == "setcb"))
	{
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		int idx = parts[3].toInt();
		if (ln == 0) return;
		if (parts[0] == "rpt" && idx >= 0 && idx < (int)ln->reports.size())
		{
			SclReportControl& rc = ln->reports[idx];
			bool b = false;
			switch (row)
			{
			case 0:
				if (!validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid")); populateProps(path); return; }
				rc.name = v; break;
			case 1: rc.rptId = v; break;
			case 2: rc.dataSet = v; break;
			case 3: rc.confRev = v; break;
			case 4: rc.intgPd = v; break;
			case 5: rc.bufTime = v; break;
			case 6: if (!parseBoolText(value, b)) { QMessageBox::warning(this, tr("Validation failed"), tr("Enter true or false")); populateProps(path); return; } rc.buffered = b; break;
			case 7: if (!parseBoolText(value, b)) { QMessageBox::warning(this, tr("Validation failed"), tr("Enter true or false")); populateProps(path); return; } rc.indexed = b; break;
			case 8: rc.maxClients = value.trimmed().isEmpty() ? -1 : value.toInt(); break;
			case 9: if (parseBoolText(value, b)) { rc.trgOps.present = true; rc.trgOps.dchg = b; } break;
			case 10: if (parseBoolText(value, b)) { rc.trgOps.present = true; rc.trgOps.qchg = b; } break;
			case 11: if (parseBoolText(value, b)) { rc.trgOps.present = true; rc.trgOps.dupd = b; } break;
			case 12: if (parseBoolText(value, b)) { rc.trgOps.present = true; rc.trgOps.period = b; } break;
			case 13: if (parseBoolText(value, b)) { rc.trgOps.present = true; rc.trgOps.gi = b; } break;
			case 14: if (parseBoolText(value, b)) { rc.optFields.present = true; rc.optFields.seqNum = b; } break;
			case 15: if (parseBoolText(value, b)) { rc.optFields.present = true; rc.optFields.timeStamp = b; } break;
			case 16: if (parseBoolText(value, b)) { rc.optFields.present = true; rc.optFields.dataSet = b; } break;
			case 17: if (parseBoolText(value, b)) { rc.optFields.present = true; rc.optFields.reasonCode = b; } break;
			case 18: if (parseBoolText(value, b)) { rc.optFields.present = true; rc.optFields.dataRef = b; } break;
			case 19: if (parseBoolText(value, b)) { rc.optFields.present = true; rc.optFields.bufOvfl = b; } break;
			case 20: if (parseBoolText(value, b)) { rc.optFields.present = true; rc.optFields.entryID = b; } break;
			case 21: if (parseBoolText(value, b)) { rc.optFields.present = true; rc.optFields.config = b; } break;
			default: return;
			}
		}
		else if (parts[0] == "gcb" && idx >= 0 && idx < (int)ln->gses.size())
		{
			SclGseControl& gc = ln->gses[idx];
			switch (row)
			{
			case 0:
				if (!validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid")); populateProps(path); return; }
				gc.name = v; break;
			case 1: gc.appId = v; break;
			case 2: gc.dataSet = v; break;
			case 3: gc.confRev = v; break;
			case 4: { bool b = false; if (!parseBoolText(value, b)) { QMessageBox::warning(this, tr("Validation failed"), tr("Enter true or false")); populateProps(path); return; } gc.fixedOffs = b; break; }
			default: return;
			}
		}
		else if (parts[0] == "smcb" && idx >= 0 && idx < (int)ln->smvs.size())
		{
			SclSampledValueControl& sc = ln->smvs[idx];
			switch (row)
			{
			case 0:
				if (!validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid")); populateProps(path); return; }
				sc.name = v; break;
			case 1: sc.smvId = v; break;
			case 2: sc.dataSet = v; break;
			case 3: sc.confRev = v; break;
			case 4: sc.smpRate = v; break;
			case 5: sc.nofASDU = v; break;
			case 6: sc.smpMod = v; break;
			default: return;
			}
		}
		else if (parts[0] == "logcb" && idx >= 0 && idx < (int)ln->logs.size())
		{
			SclLogControl& lc = ln->logs[idx];
			bool b = false;
			switch (row)
			{
			case 0:
				if (!validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid")); populateProps(path); return; }
				lc.name = v; break;
			case 1: lc.dataSet = v; break;
			case 2: lc.logName = v; break;
			case 3: lc.intgPd = v; break;
			case 4: if (parseBoolText(value, b)) { lc.trgOps.present = true; lc.trgOps.dchg = b; } break;
			case 5: if (parseBoolText(value, b)) { lc.trgOps.present = true; lc.trgOps.qchg = b; } break;
			case 6: if (parseBoolText(value, b)) { lc.trgOps.present = true; lc.trgOps.dupd = b; } break;
			case 7: if (parseBoolText(value, b)) { lc.trgOps.present = true; lc.trgOps.period = b; } break;
			case 8: if (parseBoolText(value, b)) { lc.trgOps.present = true; lc.trgOps.gi = b; } break;
			default: return;
			}
		}
		else if (parts[0] == "setcb" && idx >= 0 && idx < (int)ln->settings.size())
		{
			SclSettingControl& sc = ln->settings[idx];
			switch (row)
			{
			case 0:
				if (!validIdent(v)) { QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid")); populateProps(path); return; }
				sc.name = v; break;
			case 1: sc.dataSet = v; break;
			case 2: sc.numOfSG = v; break;
			default: return;
			}
		}
		else return;
	}
	else
	{
		return;
	}
	buildTree();
}

void MainWindow::log(const QString& text)
{
	m_log->appendPlainText(QTime::currentTime().toString("hh:mm:ss") + " " + text);
	logFileLine(text);
}

void MainWindow::validateAndLog(const std::string& sclText)
{
	ValidateOutcome out;
	std::string temp = QDir::tempPath().toUtf8().constData();
	temp += "/ieddesigner_validate.scl";
	ValidateSclText(sclText, m_model.ied.name, temp, out);
	log(tr("Loader check: DI=%1 AI=%2 DO=%3 AO=%4 SG=%5 ied=%6")
		.arg((int)out.table.diRefs.size()).arg((int)out.table.aiRefs.size())
		.arg((int)out.table.doRefs.size()).arg((int)out.table.aoRefs.size())
		.arg((int)out.table.sgRefs.size())
		.arg(QString::fromUtf8(out.detectedIed.c_str())));
	if (out.loaderOk)
		log(tr("Semantic check: DataSet=%1 Report=%2 GSE=%3 SV=%4 Log=%5 Setting=%6, GSE地址=%7 SMV地址=%8")
			.arg(out.datasetCount).arg(out.reportControlCount).arg(out.gseControlCount)
			.arg(out.sampledValueControlCount).arg(out.logControlCount).arg(out.settingControlCount)
			.arg(out.gseAddressCount).arg(out.smvAddressCount));
	if (out.sgQuirk)
		log(tr("Note: loader reported SG contains an invalid point (known loader quirk, ucType=2 rejected by ValidateRefs); point table still parsed"));
	else if (!out.loaderOk)
		log(tr("Loader check failed: %1").arg(QString::fromUtf8(out.errorText.c_str())));
	else
		log(tr("Loader check passed"));
	for (size_t i = 0; i < out.semanticErrors.size(); ++i)
		log(tr("Semantic warning: %1").arg(QString::fromUtf8(out.semanticErrors[i].c_str())));
}

void MainWindow::onNewModel()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("New model"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* iedEdit = new QLineEdit(SclDefaults::IED_NAME, &dlg);
	QLineEdit* apEdit = new QLineEdit(SclDefaults::AP_NAME, &dlg);
	QLineEdit* ldEdit = new QLineEdit(SclDefaults::LD_INST, &dlg);
	form->addRow(tr("IED name"), iedEdit);
	form->addRow(tr("AccessPoint"), apEdit);
	form->addRow(tr("LDevice inst"), ldEdit);

	// 通信参数：默认值取 SclDefaults（核心层常量），逐工程可改
	QGroupBox* commBox = new QGroupBox(tr("Communication parameters"), &dlg);
	QFormLayout* commForm = new QFormLayout(commBox);
	QLineEdit* ipEdit = new QLineEdit(SclDefaults::IP, &dlg);
	QLineEdit* subnetEdit = new QLineEdit(SclDefaults::IP_SUBNET, &dlg);
	QLineEdit* gwEdit = new QLineEdit(SclDefaults::IP_GATEWAY, &dlg);
	QLineEdit* portEdit = new QLineEdit(SclDefaults::MMS_PORT_STR, &dlg);
	QLineEdit* apTitleEdit = new QLineEdit(SclDefaults::OSI_AP_TITLE, &dlg);
	QLineEdit* aeQualEdit = new QLineEdit(SclDefaults::OSI_AE_QUALIFIER, &dlg);
	QLineEdit* pselEdit = new QLineEdit(SclDefaults::OSI_PSEL, &dlg);
	QLineEdit* sselEdit = new QLineEdit(SclDefaults::OSI_SSEL, &dlg);
	QLineEdit* tselEdit = new QLineEdit(SclDefaults::OSI_TSEL, &dlg);
	commForm->addRow(tr("IP"), ipEdit);
	commForm->addRow(tr("IP-SUBNET"), subnetEdit);
	commForm->addRow(tr("IP-GATEWAY"), gwEdit);
	commForm->addRow(tr("MMS-Port"), portEdit);
	commForm->addRow(tr("OSI-AP-Title"), apTitleEdit);
	commForm->addRow(tr("OSI-AE-Qualifier"), aeQualEdit);
	commForm->addRow(tr("OSI-PSEL"), pselEdit);
	commForm->addRow(tr("OSI-SSEL"), sselEdit);
	commForm->addRow(tr("OSI-TSEL"), tselEdit);
	form->addRow(commBox);

	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;

	if (!validIdent(iedEdit->text().toUtf8().constData()) ||
		!validIdent(apEdit->text().toUtf8().constData()) ||
		!validIdent(ldEdit->text().toUtf8().constData()))
	{
		QMessageBox::warning(this, tr("Validation failed"),
			tr("IED name / AccessPoint / LDevice inst must all be 1-64 chars, start with a letter, letters/digits/underscore only"));
		return;
	}

	std::string ip = ipEdit->text().toUtf8().constData();
	std::string ipSubnet = subnetEdit->text().toUtf8().constData();
	std::string ipGateway = gwEdit->text().toUtf8().constData();
	std::string portText = portEdit->text().toUtf8().constData();
	if (!validIpv4(ip) || !validIpv4(ipSubnet) || !validIpv4(ipGateway))
	{
		QMessageBox::warning(this, tr("Validation failed"),
			tr("IP / IP-SUBNET / IP-GATEWAY must be valid IPv4 addresses (e.g. 192.168.1.100)"));
		return;
	}
	bool portOk = allDigits(portText) && portText.size() <= 5;
	int port = 0;
	if (portOk)
	{
		for (size_t i = 0; i < portText.size(); ++i)
			port = port * 10 + (portText[i] - '0');
		portOk = port >= 1 && port <= 65535;
	}
	if (!portOk)
	{
		QMessageBox::warning(this, tr("Validation failed"),
			tr("MMS-Port must be a number between 1 and 65535"));
		return;
	}

	m_model = MakeNewModel();
	m_model.ied.name = iedEdit->text().toUtf8().constData();
	m_model.ied.apName = apEdit->text().toUtf8().constData();
	m_model.ied.lds[0].inst = ldEdit->text().toUtf8().constData();
	m_model.ied.lds[0].ldName = m_model.ied.lds[0].inst;

	// 通信参数落到 ied 字段与 Communication 段 P 参数（两处需同步）
	m_model.ied.ip = ip;
	m_model.ied.ipSubnet = ipSubnet;
	m_model.ied.ipGateway = ipGateway;
	m_model.ied.port = port;
	updatePParam(m_model.ied, "IP", ip);
	updatePParam(m_model.ied, "IP-SUBNET", ipSubnet);
	updatePParam(m_model.ied, "IP-GATEWAY", ipGateway);
	updatePParam(m_model.ied, "MMS-Port", portText);
	updatePParam(m_model.ied, "OSI-AP-Title", apTitleEdit->text().toUtf8().constData());
	updatePParam(m_model.ied, "OSI-AE-Qualifier", aeQualEdit->text().toUtf8().constData());
	updatePParam(m_model.ied, "OSI-PSEL", pselEdit->text().toUtf8().constData());
	updatePParam(m_model.ied, "OSI-SSEL", sselEdit->text().toUtf8().constData());
	updatePParam(m_model.ied, "OSI-TSEL", tselEdit->text().toUtf8().constData());
	if (!m_model.subnets.empty() && !m_model.subnets[0].aps.empty())
		m_model.subnets[0].aps[0].pParams = m_model.ied.pParams;

	m_currentFile.clear();
	buildTree();
	log(tr("New model: IED=%1 (with full communication parameters and Services declaration)").arg(iedEdit->text()));
}

void MainWindow::onTreeContextMenu(const QPoint& pos)
{
	QTreeWidgetItem* item = m_tree->itemAt(pos);
	if (item == 0)
		return;
	QString path = item->data(0, Qt::UserRole).toString();
	QStringList parts = path.split(':');
	QMenu menu(this);

	QAction* addHistAct = 0;
	QAction* delHistAct = 0;
	QAction* addApAct = 0;
	QAction* delApAct = 0;
	QAction* addLdAct = 0;
	QAction* delLdAct = 0;
	QAction* copyLdAct = 0;
	QAction* pasteLdAct = 0;
	QAction* copyLnAct = 0;
	QAction* pasteLnAct = 0;
	QAction* delLnAct = 0;
	QAction* refAct = 0;
	QAction* expandAct = 0;
	QAction* collapseAct = 0;
	QAction* addDoAct = 0;
	QAction* editDoAct = 0;
	QAction* delDoAct = 0;
	QAction* addSubAct = 0;
	QAction* delSubAct = 0;
	QAction* addCapAct = 0;
	QAction* delCapAct = 0;
	QAction* addGseAddrAct = 0;
	QAction* addSmvAddrAct = 0;
	QAction* delAddrAct = 0;
	QAction* addDsAct = 0;
	QAction* editDsAct = 0;
	QAction* delDsAct = 0;
	QAction* addExtRefAct = 0;
	QAction* editExtRefAct = 0;
	QAction* delExtRefAct = 0;
	QAction* delFcdaAct = 0;
	QAction* addRptAct = 0;
	QAction* addGcbAct = 0;
	QAction* addSmcbAct = 0;
	QAction* addLogcbAct = 0;
	QAction* addSetcbAct = 0;
	QAction* delCbAct = 0;

	if (path == "ied")
	{
		addHistAct = menu.addAction(tr("Add revision history"));
		addApAct = menu.addAction(tr("Add AccessPoint"));
		refAct = menu.addAction(tr("Get references"));
		menu.addSeparator();
		expandAct = menu.addAction(tr("Expand all"));
		collapseAct = menu.addAction(tr("Collapse all"));
	}
	else if (parts.size() == 2 && parts[0] == "hitem")
	{
		delHistAct = menu.addAction(tr("Delete revision history"));
	}
	else if (parts.size() == 2 && parts[0] == "ap")
	{
		refAct = menu.addAction(tr("Get references"));
		if (parts[1].toInt() > 0)
			delApAct = menu.addAction(tr("Delete access point"));
	}
	else if (path == "server")
	{
		addLdAct = menu.addAction(tr("Add logical device (LD)"));
	}
	else if (parts.size() == 2 && parts[0] == "ln0")
	{
		addDoAct = menu.addAction(tr("Add DO"));
		addExtRefAct = menu.addAction(tr("Add external reference"));
		addDsAct = menu.addAction(tr("Add DataSet"));
		QMenu* cbMenu = menu.addMenu(tr("Add control block"));
		addRptAct = cbMenu->addAction(tr("Report control (ReportControl)"));
		addGcbAct = cbMenu->addAction(tr("GOOSE control (GSEControl)"));
		addSmcbAct = cbMenu->addAction(tr("Sampled value control (SampledValueControl)"));
		addLogcbAct = cbMenu->addAction(tr("Log control (LogControl)"));
		addSetcbAct = cbMenu->addAction(tr("Setting control (SettingControl)"));
	}
	else if (parts.size() == 2 && parts[0] == "ld")
	{
		addLdAct = menu.addAction(tr("Add logical node"));
		menu.addSeparator();
		copyLdAct = menu.addAction(tr("Copy logical device"));
		if (g_hasClipLd)
			pasteLdAct = menu.addAction(tr("Paste logical device"));
		delLdAct = menu.addAction(tr("Delete logical device"));
		menu.addSeparator();
		refAct = menu.addAction(tr("Get references"));
	}
	else if (parts.size() == 3 && parts[0] == "ln")
	{
		addDoAct = menu.addAction(tr("Add DO"));
		addExtRefAct = menu.addAction(tr("Add external reference"));
		menu.addSeparator();
		copyLnAct = menu.addAction(tr("Copy logical node"));
		if (g_hasClipLn)
			pasteLnAct = menu.addAction(tr("Paste logical node"));
		delLnAct = menu.addAction(tr("Delete logical node"));
		menu.addSeparator();
		refAct = menu.addAction(tr("Get references"));
	}
	else if (parts.size() == 4 && parts[0] == "do")
	{
		editDoAct = menu.addAction(tr("Rename DO"));
		delDoAct = menu.addAction(tr("Delete DO"));
		menu.addSeparator();
		refAct = menu.addAction(tr("Get references"));
	}
	else if (parts.size() == 4 && parts[0] == "ds")
	{
		editDsAct = menu.addAction(tr("Edit FCDA members"));
		delDsAct = menu.addAction(tr("Delete DataSet"));
	}
	else if (parts.size() == 5 && parts[0] == "fcda")
	{
		delFcdaAct = menu.addAction(tr("Delete FCDA member"));
	}
	else if (parts.size() == 3 && parts[0] == "ds-sec")
	{
		addDsAct = menu.addAction(tr("Add DataSet"));
	}
	else if (parts.size() == 3 && parts[0] == "rpt-sec")
	{
		addRptAct = menu.addAction(tr("Add report control"));
	}
	else if (parts.size() == 3 && parts[0] == "gse-sec")
	{
		addGcbAct = menu.addAction(tr("Add GOOSE control"));
	}
	else if (parts.size() == 3 && parts[0] == "smv-sec")
	{
		addSmcbAct = menu.addAction(tr("Add sampled value control"));
	}
	else if (parts.size() == 3 && parts[0] == "log-sec")
	{
		addLogcbAct = menu.addAction(tr("Add log control"));
	}
	else if (parts.size() == 3 && parts[0] == "set-sec")
	{
		addSetcbAct = menu.addAction(tr("Add setting control"));
	}
	else if (parts.size() == 3 && parts[0] == "ext-sec")
	{
		addExtRefAct = menu.addAction(tr("Add external reference"));
	}
	else if (parts.size() == 4 && parts[0] == "extref")
	{
		editExtRefAct = menu.addAction(tr("Edit external reference"));
		delExtRefAct = menu.addAction(tr("Delete external reference"));
	}
	else if (parts.size() == 4 && (parts[0] == "rpt" || parts[0] == "gcb" || parts[0] == "smcb" || parts[0] == "logcb" || parts[0] == "setcb"))
	{
		delCbAct = menu.addAction(tr("Delete control block"));
	}
	else if (path == "comm")
	{
		addSubAct = menu.addAction(tr("Add SubNetwork"));
	}
	else if (parts.size() == 2 && parts[0] == "subnet")
	{
		addCapAct = menu.addAction(tr("Add ConnectedAP"));
		delSubAct = menu.addAction(tr("Delete SubNetwork"));
	}
	else if (parts.size() == 3 && parts[0] == "cap")
	{
		addGseAddrAct = menu.addAction(tr("Add GSE address"));
		addSmvAddrAct = menu.addAction(tr("Add SMV address"));
		delCapAct = menu.addAction(tr("Delete access point"));
	}
	else if (parts.size() == 4 && (parts[0] == "gse" || parts[0] == "smv"))
	{
		delAddrAct = menu.addAction(tr("Delete communication address"));
	}

	QAction* picked = menu.exec(m_tree->mapToGlobal(pos));
	if (picked == 0)
		return;

	if (picked == addHistAct) { addHistoryItem(); return; }
	if (picked == delHistAct) { deleteHistoryItem(parts[1].toInt()); return; }
	if (picked == addApAct) { addAccessPoint(); return; }
	if (picked == delApAct) { deleteAccessPoint(parts[1].toInt() - 1); return; }
	if (picked == addLdAct && path == "server") { addLDevice(); return; }
	if (picked == delLdAct) { deleteLDevice(parts[1].toInt()); return; }
	if (picked == copyLdAct) { copyLDevice(parts[1].toInt()); return; }
	if (picked == pasteLdAct) { pasteLDevice(); return; }
	if (picked == copyLnAct) { copyLogicNode(parts[1].toInt(), parts[2].toInt()); return; }
	if (picked == pasteLnAct) { pasteLogicNode(parts[1].toInt()); return; }
	if (picked == delLnAct)
	{
		int li = parts[1].toInt();
		int ni = parts[2].toInt();
		SclLDevice* ld = ldAt(li);
		if (ld == 0 || ni < 0 || ni >= (int)ld->lns.size())
			return;
		QString name = QString::fromUtf8(ld->lns[ni].Name().c_str());
		ld->lns.erase(ld->lns.begin() + ni);
		buildTree();
		log(tr("Delete logical node %1").arg(name));
		return;
	}
	if (picked == refAct) { showReference(path); return; }
	if (picked == expandAct) { m_tree->expandAll(); return; }
	if (picked == collapseAct) { m_tree->collapseAll(); return; }
	if (picked == addDoAct) { addDo(parts[1].toInt(), parts[0] == "ln0" ? -1 : parts[2].toInt()); return; }
	if (picked == editDoAct) { editDo(parts[1].toInt(), parts[2].toInt(), parts[3].toInt()); return; }
	if (picked == delDoAct) { deleteDo(parts[1].toInt(), parts[2].toInt(), parts[3].toInt()); return; }
	if (picked == addSubAct) { addSubNet(); return; }
	if (picked == delSubAct) { deleteSubNet(parts[1].toInt()); return; }
	if (picked == addCapAct) { addConnectedAP(parts[1].toInt()); return; }
	if (picked == delCapAct) { deleteConnectedAP(parts[1].toInt(), parts[2].toInt()); return; }
	if (picked == addGseAddrAct) { addCommAddress(parts[1].toInt(), parts[2].toInt(), true); return; }
	if (picked == addSmvAddrAct) { addCommAddress(parts[1].toInt(), parts[2].toInt(), false); return; }
	if (picked == delAddrAct) { deleteCommAddress(parts[1].toInt(), parts[2].toInt(), parts[0] == "gse", parts[3].toInt()); return; }
	if (picked == addDsAct) { addDataSet(parts[1].toInt(), parts[0] == "ln0" ? -1 : parts[2].toInt()); return; }
	if (picked == addExtRefAct)
	{
		int ni = (parts[0] == "ln0") ? -1 : parts[2].toInt();
		addExternalRef(parts[1].toInt(), ni);
		return;
	}
	if (picked == delFcdaAct)
	{
		int li = parts[1].toInt();
		int ni = parts[2].toInt();
		int s = parts[3].toInt();
		int k = parts[4].toInt();
		SclLn* ln = lnAt(li, ni);
		if (ln == 0 || s < 0 || s >= (int)ln->dataSets.size() || k < 0 || k >= (int)ln->dataSets[s].fcdas.size())
			return;
		if (QMessageBox::question(this, tr("Confirm"),
			tr("Delete FCDA member %1?").arg(QString::fromUtf8(ln->dataSets[s].fcdas[k].RefText().c_str()))) != QMessageBox::Yes)
			return;
		ln->dataSets[s].fcdas.erase(ln->dataSets[s].fcdas.begin() + k);
		buildTree();
		log(tr("FCDA member removed"));
		return;
	}
	if (picked == editExtRefAct || picked == delExtRefAct)
	{
		int li = parts[1].toInt();
		int ni = parts[2].toInt();
		int idx = parts[3].toInt();
		if (picked == editExtRefAct)
			editExternalRef(li, ni, idx);
		else
			deleteExternalRef(li, ni, idx);
		return;
	}
	if (picked == editDsAct || picked == delDsAct)
	{
		int li = parts[1].toInt();
		int ni = parts[2].toInt();
		int di = parts[3].toInt();
		SclLn* ln = lnAt(li, ni);
		if (ln == 0 || di < 0 || di >= (int)ln->dataSets.size()) return;
		if (picked == editDsAct)
			editDataSetDialog(ln->dataSets[di], li);
		else
			deleteDataSet(li, ni, di);
		return;
	}
	if (picked == delCbAct) { deleteControlBlock(parts[0], parts[1].toInt(), parts[2].toInt(), parts[3].toInt()); return; }
	if (picked == addRptAct) { addReport(parts[1].toInt(), parts[0] == "ln0" ? -1 : parts[2].toInt()); return; }
	if (picked == addGcbAct) { addGseControl(parts[1].toInt(), parts[0] == "ln0" ? -1 : parts[2].toInt()); return; }
	if (picked == addSmcbAct) { addSmvControl(parts[1].toInt(), parts[0] == "ln0" ? -1 : parts[2].toInt()); return; }
	if (picked == addLogcbAct) { addLogControl(parts[1].toInt(), parts[0] == "ln0" ? -1 : parts[2].toInt()); return; }
	if (picked == addSetcbAct) { addSettingControl(parts[1].toInt(), parts[0] == "ln0" ? -1 : parts[2].toInt()); return; }

	// 兼容旧入口：ld 节点上的“添加逻辑节点”对话框
	if (picked == addLdAct && parts.size() == 2 && parts[0] == "ld")
	{
		int li = parts[1].toInt();
		SclLDevice* ld = ldAt(li);
		if (ld == 0)
			return;
		QDialog dlg(this);
		dlg.setWindowTitle(tr("Add logical node"));
		QVBoxLayout* vbox = new QVBoxLayout(&dlg);
		QListWidget* list = new QListWidget(&dlg);
		const std::vector<SclLnT>& table = SclTemplates::LnTypes();
		for (size_t i = 0; i < table.size(); ++i)
		{
			if (table[i].lnClass == std::string("LLN0"))
				continue;
			QString label = QString::fromUtf8(table[i].lnClass) + " - " + QString::fromUtf8(table[i].desc);
			new QListWidgetItem(label, list);
			list->item(list->count() - 1)->setData(Qt::UserRole, QString::fromUtf8(table[i].lnClass));
		}
		vbox->addWidget(list);
		QHBoxLayout* hbox = new QHBoxLayout;
		QLabel* instLabel = new QLabel(tr("inst"), &dlg);
		QLineEdit* instEdit = new QLineEdit("1", &dlg);
		QLabel* prefixLabel = new QLabel(tr("prefix"), &dlg);
		QLineEdit* prefixEdit = new QLineEdit("", &dlg);
		hbox->addWidget(instLabel);
		hbox->addWidget(instEdit);
		hbox->addWidget(prefixLabel);
		hbox->addWidget(prefixEdit);
		vbox->addLayout(hbox);
		QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
		vbox->addWidget(box);
		connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
		connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
		while (dlg.exec() == QDialog::Accepted)
		{
			if (list->currentItem() == 0)
				return;
			std::string lnClass = list->currentItem()->data(Qt::UserRole).toString().toUtf8().constData();
			std::string inst = instEdit->text().toUtf8().constData();
			std::string prefix = prefixEdit->text().toUtf8().constData();
			if (!allDigits(inst))
			{
				QMessageBox::warning(&dlg, tr("Validation failed"), tr("Inst must be a non-empty number"));
				continue;
			}
			if (!prefix.empty() && !validIdent(prefix))
			{
				QMessageBox::warning(&dlg, tr("Validation failed"), tr("Prefix may contain letters/digits/underscore only"));
				continue;
			}
			if (lnExistsInLd(*ld, prefix, lnClass, inst, -1))
			{
				QMessageBox::warning(&dlg, tr("Validation failed"), tr("prefix and inst combination duplicates an existing LN in the model file, please change"));
				continue;
			}
			SclLn ln = SclTemplates::MakeLn(lnClass);
			ln.inst = inst;
			ln.prefix = prefix;
			ld->lns.push_back(ln);
			buildTree();
			log(tr("Add logical node %1%2 to %3").arg(QString::fromUtf8(lnClass.c_str())).arg(instEdit->text()).arg(QString::fromUtf8(ld->inst.c_str())));
			return;
		}
	}
}

void MainWindow::addHistoryItem()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add revision history"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* versionEdit = new QLineEdit("1.0", &dlg);
	QLineEdit* whatEdit = new QLineEdit(&dlg);
	QLineEdit* whoEdit = new QLineEdit(&dlg);
	QLineEdit* whenEdit = new QLineEdit(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), &dlg);
	QLineEdit* whyEdit = new QLineEdit(&dlg);
	form->addRow(tr("Version"), versionEdit);
	form->addRow(tr("What"), whatEdit);
	form->addRow(tr("Who"), whoEdit);
	form->addRow(tr("When"), whenEdit);
	form->addRow(tr("Why"), whyEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;

	SclHistoryItem item;
	item.version = versionEdit->text().toUtf8().constData();
	item.what = whatEdit->text().toUtf8().constData();
	item.who = whoEdit->text().toUtf8().constData();
	item.when = whenEdit->text().toUtf8().constData();
	item.why = whyEdit->text().toUtf8().constData();
	m_model.history.push_back(item);
	buildTree();
	log(tr("Revision history added: %1").arg(QString::fromUtf8(item.what.c_str())));
}

void MainWindow::deleteHistoryItem(int idx)
{
	if (idx < 0 || idx >= (int)m_model.history.size())
		return;
	if (QMessageBox::question(this, tr("Confirm"), tr("Delete this revision history entry?")) != QMessageBox::Yes)
		return;
	m_model.history.erase(m_model.history.begin() + idx);
	buildTree();
	log(tr("Delete revision history H%1").arg(idx + 1));
}

void MainWindow::addAccessPoint()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add AccessPoint"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit("accessPoint2", &dlg);
	QComboBox* serverAtCombo = new QComboBox(&dlg);
	serverAtCombo->addItem(tr("None (standalone access point)"), QString());
	serverAtCombo->addItem(tr("Primary access point: %1").arg(QString::fromUtf8(m_model.ied.apName.c_str())),
		QString::fromUtf8(m_model.ied.apName.c_str()));
	for (size_t i = 0; i < m_model.ied.extraAps.size(); ++i) {
		serverAtCombo->addItem(QString::fromUtf8(m_model.ied.extraAps[i].name.c_str()),
			QString::fromUtf8(m_model.ied.extraAps[i].name.c_str()));
	}
	form->addRow(tr("Name"), nameEdit);
	form->addRow(tr("ServerAt target"), serverAtCombo);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;

	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name) || apNameExists(name, -1))
	{
		QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid and unique"));
		return;
	}
	SclAccessPoint ap;
	ap.name = name;
	ap.serverAt = serverAtCombo->currentData().toString().toUtf8().constData();
	m_model.ied.extraAps.push_back(ap);
	buildTree();
	log(tr("Add access point %1%2").arg(QString::fromUtf8(name.c_str()))
		.arg(ap.serverAt.empty() ? tr(" (standalone)") : tr("（ServerAt -> %1）").arg(QString::fromUtf8(ap.serverAt.c_str()))));
}

void MainWindow::deleteAccessPoint(int extraIdx)
{
	if (extraIdx < 0 || extraIdx >= (int)m_model.ied.extraAps.size())
		return;
	if (QMessageBox::question(this, tr("Confirm"),
		tr("Delete access point %1?").arg(QString::fromUtf8(m_model.ied.extraAps[extraIdx].name.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(m_model.ied.extraAps[extraIdx].name.c_str());
	m_model.ied.extraAps.erase(m_model.ied.extraAps.begin() + extraIdx);
	buildTree();
	log(tr("Delete access point %1").arg(name));
}

void MainWindow::addLDevice()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add logical device (LD)"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* instEdit = new QLineEdit(QString("LD%1").arg((int)m_model.ied.lds.size() + 1), &dlg);
	QLineEdit* descEdit = new QLineEdit(&dlg);
	form->addRow(tr("Instance"), instEdit);
	form->addRow(tr("Description"), descEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;

	std::string inst = instEdit->text().toUtf8().constData();
	if (!validIdent(inst) || ldInstExists(inst, -1))
	{
		QMessageBox::warning(this, tr("Validation failed"), tr("Instance must be valid and unique in the file"));
		return;
	}
	SclLDevice ld;
	ld.inst = inst;
	ld.ldName = inst;
	ld.desc = descEdit->text().toUtf8().constData();
	ld.ln0 = SclTemplates::MakeLn("LLN0");
	m_model.ied.lds.push_back(ld);
	buildTree();
	log(tr("Add logical device %1").arg(QString::fromUtf8(inst.c_str())));
}

void MainWindow::deleteLDevice(int li)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0)
		return;
	if (QMessageBox::question(this, tr("Confirm"),
		tr("Delete logical device %1 and all its logical nodes?").arg(QString::fromUtf8(ld->inst.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(ld->inst.c_str());
	m_model.ied.lds.erase(m_model.ied.lds.begin() + li);
	buildTree();
	log(tr("Delete logical device %1").arg(name));
}

void MainWindow::copyLDevice(int li)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0)
		return;
	g_clipLd = *ld;
	g_hasClipLd = true;
	log(tr("Logical device %1 copied; paste it on the Server node").arg(QString::fromUtf8(ld->inst.c_str())));
}

void MainWindow::pasteLDevice()
{
	if (!g_hasClipLd)
		return;
	SclLDevice ld = g_clipLd;
	std::string base = ld.inst;
	int n = 1;
	while (ldInstExists(ld.inst, -1)) {
		ld.inst = base + "_" + std::to_string(n++);
	}
	ld.ldName = ld.inst;
	m_model.ied.lds.push_back(ld);
	buildTree();
	log(tr("Paste logical device %1 (source: %2)").arg(QString::fromUtf8(ld.inst.c_str())).arg(QString::fromUtf8(base.c_str())));
}

void MainWindow::copyLogicNode(int li, int ni)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0 || ni < 0 || ni >= (int)ld->lns.size())
		return;
	g_clipLn = ld->lns[ni];
	g_hasClipLn = true;
	log(tr("Logical node %1 copied; paste it on an LDevice node").arg(QString::fromUtf8(g_clipLn.Name().c_str())));
}

void MainWindow::pasteLogicNode(int li)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0 || !g_hasClipLn)
		return;
	SclLn ln = g_clipLn;
	std::string base = ln.inst;
	int n = 1;
	while (lnExistsInLd(*ld, ln.prefix, ln.lnClass, ln.inst, -1)) {
		ln.inst = base + "_" + std::to_string(n++);
	}
	ld->lns.push_back(ln);
	buildTree();
	log(tr("Paste logical node %1 to %2").arg(QString::fromUtf8(ln.Name().c_str())).arg(QString::fromUtf8(ld->inst.c_str())));
}

void MainWindow::showReference(const QString& path)
{
	std::string iedName = m_model.ied.name;
	QStringList parts = path.split(':');
	std::string iecRef;
	std::string mmsRef;

	if (path == "ied")
	{
		iecRef = iedName;
		mmsRef = iedName;
	}
	else if (path == "server" || (parts.size() == 2 && parts[0] == "ap"))
	{
		return;
	}
	else if (parts.size() == 2 && parts[0] == "ld")
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		if (ld == 0) return;
		iecRef = iedName + "/" + ld->inst;
		mmsRef = iedName + ld->inst;
	}
	else if (parts[0] == "ln0" && parts.size() == 2)
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		if (ld == 0) return;
		iecRef = iedName + "/" + ld->inst + "/LLN0";
		mmsRef = iedName + ld->inst + "/LLN0";
	}
	else if (parts[0] == "ln" && parts.size() == 3)
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		if (ld == 0 || ln == 0) return;
		iecRef = iedName + "/" + ld->inst + "/" + ln->Name();
		mmsRef = iedName + ld->inst + "/" + ln->Name();
	}
	else if (parts.size() == 4 && parts[0] == "do")
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		SclLn* ln = lnAt(parts[1].toInt(), parts[2].toInt());
		SclDo* dobj = doAt(parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
		if (ld == 0 || ln == 0 || dobj == 0) return;
		iecRef = iedName + "/" + ld->inst + "/" + ln->Name() + "." + dobj->name;
		mmsRef = iedName + ld->inst + "/" + ln->Name() + "$" + cdcToDefaultFc(dobj->cdc) + "$" + dobj->name;
	}
	else
	{
		return;
	}

	QDialog dlg(this);
	dlg.setWindowTitle(tr("Reference info"));
	dlg.resize(620, 180);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);
	QFormLayout* form = new QFormLayout;
	QLineEdit* iecEdit = new QLineEdit(QString::fromUtf8(iecRef.c_str()), &dlg);
	QLineEdit* mmsEdit = new QLineEdit(QString::fromUtf8(mmsRef.c_str()), &dlg);
	iecEdit->setReadOnly(true);
	mmsEdit->setReadOnly(true);
	form->addRow(tr("IEC61850 reference"), iecEdit);
	form->addRow(tr("MMS reference"), mmsEdit);
	vbox->addLayout(form);
	QHBoxLayout* btns = new QHBoxLayout;
	QPushButton* copyIec = new QPushButton(tr("Copy IEC61850 reference"), &dlg);
	QPushButton* copyMms = new QPushButton(tr("Copy MMS reference"), &dlg);
	btns->addWidget(copyIec);
	btns->addWidget(copyMms);
	btns->addStretch(1);
	vbox->addLayout(btns);
	connect(copyIec, &QPushButton::clicked, this, [this, iecEdit]() {
		QApplication::clipboard()->setText(iecEdit->text());
		log(tr("IEC61850 reference copied to clipboard"));
	});
	connect(copyMms, &QPushButton::clicked, this, [this, mmsEdit]() {
		QApplication::clipboard()->setText(mmsEdit->text());
		log(tr("MMS reference copied to clipboard"));
	});
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, &dlg);
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	vbox->addWidget(box);
	dlg.exec();
}

// 直接预览文件
void MainWindow::onXmlPreview()
{
	std::string text = SclWriter::BuildSclText(m_model, true);
	QDialog dlg(this);
	dlg.setWindowTitle(tr("XML source preview"));
	dlg.resize(920, 700);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);
	QPlainTextEdit* edit = new QPlainTextEdit(&dlg);
	edit->setReadOnly(true);
	edit->setPlainText(QString::fromUtf8(text.c_str()));
	QFont mono(QString("Consolas"));
	mono.setStyleHint(QFont::Monospace);
	edit->setFont(mono);
	vbox->addWidget(edit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, &dlg);
	// 第三期任务 4：预览内直接触发 XSD 校验
	QPushButton* xsdBtn = box->addButton(tr("XSD validation"), QDialogButtonBox::ActionRole);
	connect(xsdBtn, &QPushButton::clicked, [&dlg, text]() {
		std::vector<XsdValidator::XsdError> errors;
		std::string errText;
		std::string dir = XsdValidator::EnsureSchemasExtracted();
		if (dir.empty())
		{
			QMessageBox::warning(&dlg, QObject::tr("XSD validation"), QObject::tr("Cannot extract schema suite"));
			return;
		}
		if (!XsdValidator::Validate(text, dir, errors, errText))
		{
			QMessageBox::warning(&dlg, QObject::tr("XSD validation"), QString::fromUtf8(errText.c_str()));
			return;
		}
		if (errors.empty())
			QMessageBox::information(&dlg, QObject::tr("XSD validation"), QObject::tr("XSD validation passed (SCL 2003 schema)"));
		else
			QMessageBox::warning(&dlg, QObject::tr("XSD validation"),
				QObject::tr("XSD validation found %1 issue(s), see the list on the main window log.").arg((int)errors.size()));
	});
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	vbox->addWidget(box);
	dlg.exec();
}

// 第三期任务 4：XSD 校验（schema 套件内置于 qrc，结果走日志 + 错误列表窗口）
void MainWindow::validateXsdAndReport(const std::string& sclText)
{
	std::string dir = XsdValidator::EnsureSchemasExtracted();
	if (dir.empty())
	{
		log(tr("XSD validation skipped: cannot extract schema suite"));
		return;
	}
	std::vector<XsdValidator::XsdError> errors;
	std::string errText;
	if (!XsdValidator::Validate(sclText, dir, errors, errText))
	{
		log(tr("XSD validation failed: %1").arg(QString::fromUtf8(errText.c_str())));
		return;
	}
	if (errors.empty())
	{
		log(tr("XSD validation passed (SCL 2003 schema)"));
		return;
	}
	log(tr("XSD validation found %1 issue(s)").arg((int)errors.size()));
	showXsdErrorList(errors);
}

void MainWindow::showXsdErrorList(const std::vector<XsdValidator::XsdError>& errors)
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("XSD validation issues"));
	dlg.resize(780, 440);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);
	vbox->addWidget(new QLabel(tr("The generated SCL violates the schema (line numbers refer to the generated file):"), &dlg));
	QTableWidget* table = new QTableWidget((int)errors.size(), 2, &dlg);
	table->setHorizontalHeaderLabels(QStringList() << tr("Line") << tr("Message"));
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->horizontalHeader()->setStretchLastSection(true);
	table->setColumnWidth(0, 70);
	for (size_t i = 0; i < errors.size(); ++i)
	{
		table->setItem((int)i, 0, new QTableWidgetItem(
			errors[i].line > 0 ? QString::number(errors[i].line) : QString("-")));
		table->setItem((int)i, 1, new QTableWidgetItem(QString::fromUtf8(errors[i].message.c_str())));
	}
	vbox->addWidget(table);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, &dlg);
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	vbox->addWidget(box);
	dlg.exec();
}

// 第三期任务 6：保存/导出时自动追加（或更新）当天修订历史条目
void MainWindow::ensureAutoHistory()
{
	const QString today = QDate::currentDate().toString("yyyy-MM-dd");
	const std::string whatText = tr("Model updated and saved").toUtf8().constData();
	if (!m_model.history.empty())
	{
		SclHistoryItem& last = m_model.history.back();
		if (QString::fromUtf8(last.when.c_str()).startsWith(today))
		{
			// 当天已有条目：更新说明，避免同日重复堆积
			last.what = whatText;
			return;
		}
	}
	SclHistoryItem item;
	item.version = today.toUtf8().constData();
	item.what = whatText;
	item.who = "";
	item.when = today.toUtf8().constData();
	item.why = "";
	m_model.history.push_back(item);
}

void MainWindow::onSaveCidAs()
{
	QString path = QFileDialog::getSaveFileName(this, tr("Save CID"),
		QDir::currentPath() + "/model.cid", tr("CID (*.cid)"));
	if (path.isEmpty())
		return;
	m_currentFile = path;
	onSaveCid();
}

void MainWindow::onSaveCid()
{
	if (m_currentFile.isEmpty())
	{
		onSaveCidAs();
		return;
	}
	ensureAutoHistory();
	std::string err;
	std::string path = m_currentFile.toUtf8().constData();
	if (!SclWriter::WriteCid(m_model, path, err))
	{
		QMessageBox::warning(this, tr("Save failed"), QString::fromUtf8(err.c_str()));
		return;
	}
	log(tr("CID saved: %1").arg(m_currentFile));
	std::string text = SclWriter::BuildSclText(m_model, true);
	validateAndLog(text);
	validateXsdAndReport(text);
}

void MainWindow::onExportIcd()
{
	QString path = QFileDialog::getSaveFileName(this, tr("Export ICD file"),
		QDir::currentPath() + "/model.icd", tr("ICD (*.icd)"));
	if (path.isEmpty())
		return;
	ensureAutoHistory();
	std::string err;
	if (!SclWriter::WriteIcd(m_model, path.toUtf8().constData(), err))
	{
		QMessageBox::warning(this, tr("Export failed"), QString::fromUtf8(err.c_str()));
		return;
	}
	log(tr("ICD exported: %1").arg(path));
	std::string text = SclWriter::BuildSclText(m_model, false);
	validateAndLog(text);
	validateXsdAndReport(text);
}

void MainWindow::onExportCid()
{
	QString path = QFileDialog::getSaveFileName(this, tr("Export CID file"),
		QDir::currentPath() + "/model.cid", tr("CID (*.cid)"));
	if (path.isEmpty())
		return;
	ensureAutoHistory();
	std::string err;
	if (!SclWriter::WriteCid(m_model, path.toUtf8().constData(), err))
	{
		QMessageBox::warning(this, tr("Export failed"), QString::fromUtf8(err.c_str()));
		return;
	}
	log(tr("CID exported: %1").arg(path));
	std::string text = SclWriter::BuildSclText(m_model, true);
	validateAndLog(text);
	validateXsdAndReport(text);
}

void MainWindow::onExportCsv()
{
	QString path = QFileDialog::getSaveFileName(this, tr("Export CSV point table"),
		QDir::currentPath() + "/model_points.csv", tr("CSV (*.csv)"));
	if (path.isEmpty())
		return;
	std::string sclText = SclWriter::BuildSclText(m_model, false);
	ValidateOutcome out;
	std::string temp = QDir::tempPath().toUtf8().constData();
	temp += "/ieddesigner_validate.scl";
	ValidateSclText(sclText, m_model.ied.name, temp, out);
	if (!out.refsParsed)
	{
		QMessageBox::warning(this, tr("Export failed"),
			tr("Loader parsed no points: %1").arg(QString::fromUtf8(out.errorText.c_str())));
		return;
	}
	std::string err;
	if (!CsvWriter::WriteCsv(m_model, out.table, path.toUtf8().constData(), err))
	{
		QMessageBox::warning(this, tr("Export failed"), QString::fromUtf8(err.c_str()));
		return;
	}
	log(tr("CSV exported: %1 (DI=%2 AI=%3 DO=%4 AO=%5 SG=%6)").arg(path)
		.arg((int)out.table.diRefs.size()).arg((int)out.table.aiRefs.size())
		.arg((int)out.table.doRefs.size()).arg((int)out.table.aoRefs.size())
		.arg((int)out.table.sgRefs.size()));
}

void MainWindow::onOpenScl()
{
	QString path = QFileDialog::getOpenFileName(this, tr("Open SCL model"),
		QDir::currentPath(), tr("SCL (*.icd *.cid *.scd);;All files (*.*)"));
	if (path.isEmpty())
		return;
	openSclPath(path);
}

void MainWindow::openSclPath(const QString& path)
{
	if (path.isEmpty())
		return;
	if (!QFile::exists(path))
	{
		QMessageBox::warning(this, tr("Open failed"), tr("File does not exist: %1").arg(path));
		return;
	}
	setBusy(true);
	log(tr("Opening %1 ...").arg(path));

	QFutureWatcher<SclIedListResult>* listWatcher = new QFutureWatcher<SclIedListResult>(this);
	connect(listWatcher, &QFutureWatcher<SclIedListResult>::finished, this,
		[this, listWatcher, path]() {
		SclIedListResult r = listWatcher->result();
		listWatcher->deleteLater();
		if (r.ieds.empty())
		{
			setBusy(false);
			QMessageBox::warning(this, tr("Open failed"), QString::fromUtf8(r.err.c_str()));
			return;
		}
		QString pick;
		if (r.ieds.size() > 1)
		{
			QStringList items;
			for (size_t i = 0; i < r.ieds.size(); ++i) {
				items << QString::fromUtf8(r.ieds[i].c_str());
			}
			pick = QInputDialog::getItem(this, tr("Select IED"),
				tr("The file contains multiple IEDs, select one to open:"), items, 0, false);
			if (pick.isEmpty())
			{
				setBusy(false);
				return;
			}
		}
		else
		{
			pick = QString::fromUtf8(r.ieds[0].c_str());
		}

		QFutureWatcher<SclLoadResult>* loadWatcher = new QFutureWatcher<SclLoadResult>(this);
		connect(loadWatcher, &QFutureWatcher<SclLoadResult>::finished, this,
			[this, loadWatcher, path, pick]() {
			SclLoadResult r2 = loadWatcher->result();
			loadWatcher->deleteLater();
			setBusy(false);
			if (!r2.ok)
			{
				QMessageBox::warning(this, tr("Open failed"), QString::fromUtf8(r2.err.c_str()));
				return;
			}
			m_model = r2.model;
			m_currentFile = path;
			buildTree();
			pushRecentFile(path);
			int lnCount = 0;
			int doCount = 0;
			for (size_t li = 0; li < m_model.ied.lds.size(); ++li)
			{
				const SclLDevice& ld = m_model.ied.lds[li];
				lnCount += (int)ld.lns.size();
				doCount += (int)ld.ln0.dos.size();
				for (size_t ni = 0; ni < ld.lns.size(); ++ni) {
					doCount += (int)ld.lns[ni].dos.size();
				}
			}
			log(tr("Opened %1: IED=%2 LD=%3 LN=%4 DO=%5").arg(path)
				.arg(pick)
				.arg((int)m_model.ied.lds.size()).arg(lnCount).arg(doCount));
		});
		std::string spath = path.toUtf8().constData();
		std::string spick = pick.toUtf8().constData();
		loadWatcher->setFuture(QtConcurrent::run([spath, spick]() {
			SclLoadResult r;
			r.ok = SclReader::ReadSclFile(spath, spick, r.model, r.err);
			return r;
		}));
	});
	std::string spath = path.toUtf8().constData();
	listWatcher->setFuture(QtConcurrent::run([spath]() {
		SclIedListResult r;
		r.ieds = SclReader::ListIeds(spath, r.err);
		return r;
	}));
}

void MainWindow::onImportJson()
{
	QString path = QFileDialog::getOpenFileName(this, tr("Import online JSON"),
		QDir::currentPath(), tr("JSON (*.json);;All files (*.*)"));
	if (path.isEmpty())
		return;
	std::string err;
	SclModel loaded;
	if (!JsonModel::ReadOnlineJsonFile(path.toUtf8().constData(), "",
		m_model.ied.ip, m_model.ied.port, loaded, err))
	{
		QMessageBox::warning(this, tr("Import failed"), QString::fromUtf8(err.c_str()));
		return;
	}
	m_model = loaded;
	m_currentFile.clear();
	buildTree();
	int lnCount = 0;
	int doCount = 0;
	for (size_t li = 0; li < m_model.ied.lds.size(); ++li)
	{
		const SclLDevice& ld = m_model.ied.lds[li];
		lnCount += (int)ld.lns.size();
		doCount += (int)ld.ln0.dos.size();
		for (size_t ni = 0; ni < ld.lns.size(); ++ni) {
			doCount += (int)ld.lns[ni].dos.size();
		}
	}
	log(tr("Online JSON imported %1: IED=%2 LD=%3 LN=%4 DO=%5").arg(path)
		.arg(QString::fromUtf8(m_model.ied.name.c_str()))
		.arg((int)m_model.ied.lds.size()).arg(lnCount).arg(doCount));
}

void MainWindow::onOnlineRead()
{
	if (m_browseThread != 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Read IED online"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* ipEdit = new QLineEdit(m_model.ied.ip.c_str(), &dlg);
	QLineEdit* portEdit = new QLineEdit(QString::number(m_model.ied.port), &dlg);
	QLineEdit* iedEdit = new QLineEdit("simpleIO", &dlg);
	form->addRow(tr("IP"), ipEdit);
	form->addRow(tr("Port"), portEdit);
	form->addRow(tr("IED name (optional)"), iedEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;

	m_browseThread = new OnlineBrowseThread(ipEdit->text(), portEdit->text().toInt(), iedEdit->text(), this);
	connect(m_browseThread, SIGNAL(finished()), this, SLOT(onOnlineFinished()));
	m_onlineAct->setEnabled(false);
	m_jsonAct->setEnabled(false);
	log(tr("Starting online read %1:%2 ...").arg(ipEdit->text()).arg(portEdit->text()));
	m_browseThread->start();
}

void MainWindow::onOnlineFinished()
{
	OnlineBrowseThread* t = m_browseThread;
	m_browseThread = 0;
	m_onlineAct->setEnabled(true);
	m_jsonAct->setEnabled(true);
	if (!t->m_ok)
	{
		log(tr("Online read failed: %1").arg(t->m_error));
		t->deleteLater();
		return;
	}
	m_model = t->m_model;
	m_currentFile.clear();
	buildTree();
	int lnCount = 0;
	int doCount = 0;
	for (size_t li = 0; li < m_model.ied.lds.size(); ++li)
	{
		const SclLDevice& ld = m_model.ied.lds[li];
		lnCount += (int)ld.lns.size();
		doCount += (int)ld.ln0.dos.size();
		for (size_t ni = 0; ni < ld.lns.size(); ++ni) {
			doCount += (int)ld.lns[ni].dos.size();
		}
	}
	log(tr("Online read finished: IED=%1 LD=%2 LN=%3 DO=%4").arg(QString::fromUtf8(m_model.ied.name.c_str()))
		.arg((int)m_model.ied.lds.size()).arg(lnCount).arg(doCount));
	t->deleteLater();
}

bool MainWindow::parseBoolText(const QString& text, bool& out) const
{
	QString vs = text.trimmed().toLower();
	if (vs == "true" || vs == "1" || vs == tr("Yes")) { out = true; return true; }
	if (vs == "false" || vs == "0" || vs == tr("No")) { out = false; return true; }
	return false;
}

void MainWindow::addSubNet()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add SubNetwork"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("SubNetwork%1").arg((int)m_model.subnets.size() + 1), &dlg);
	QComboBox* typeCombo = new QComboBox(&dlg);
	typeCombo->addItem("8-MMS");
	typeCombo->addItem("4-StationBus");
	typeCombo->addItem("10-UDP");
	form->addRow(tr("Name"), nameEdit);
	form->addRow(tr("Type"), typeCombo);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	SclSubNetwork sn;
	sn.name = nameEdit->text().toUtf8().constData();
	sn.type = typeCombo->currentText().toUtf8().constData();
	m_model.subnets.push_back(sn);
	buildTree();
	log(tr("Add SubNetwork %1").arg(nameEdit->text()));
}

void MainWindow::deleteSubNet(int idx)
{
	if (idx < 0 || idx >= (int)m_model.subnets.size())
		return;
	if (QMessageBox::question(this, tr("Confirm"),
		tr("Delete SubNetwork %1 and all its access points?").arg(QString::fromUtf8(m_model.subnets[idx].name.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(m_model.subnets[idx].name.c_str());
	m_model.subnets.erase(m_model.subnets.begin() + idx);
	buildTree();
	log(tr("Delete SubNetwork %1").arg(name));
}

void MainWindow::addConnectedAP(int subnetIdx)
{
	if (subnetIdx < 0 || subnetIdx >= (int)m_model.subnets.size())
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add ConnectedAP"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* apEdit = new QLineEdit(m_model.ied.apName.c_str(), &dlg);
	QLineEdit* iedEdit = new QLineEdit(m_model.ied.name.c_str(), &dlg);
	form->addRow(tr("apName"), apEdit);
	form->addRow(tr("iedName"), iedEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	SclConnectedAP cap;
	cap.apName = apEdit->text().toUtf8().constData();
	cap.iedName = iedEdit->text().toUtf8().constData();
	m_model.subnets[subnetIdx].aps.push_back(cap);
	buildTree();
	log(tr("Add access point %1 to SubNetwork %2").arg(apEdit->text()).arg(QString::fromUtf8(m_model.subnets[subnetIdx].name.c_str())));
}

void MainWindow::deleteConnectedAP(int subnetIdx, int apIdx)
{
	if (subnetIdx < 0 || subnetIdx >= (int)m_model.subnets.size())
		return;
	if (apIdx < 0 || apIdx >= (int)m_model.subnets[subnetIdx].aps.size())
		return;
	if (QMessageBox::question(this, tr("Confirm"), tr("Delete this access point?")) != QMessageBox::Yes)
		return;
	m_model.subnets[subnetIdx].aps.erase(m_model.subnets[subnetIdx].aps.begin() + apIdx);
	buildTree();
	log(tr("Delete access point"));
}

void MainWindow::addCommAddress(int subnetIdx, int apIdx, bool isGse)
{
	if (subnetIdx < 0 || subnetIdx >= (int)m_model.subnets.size())
		return;
	if (apIdx < 0 || apIdx >= (int)m_model.subnets[subnetIdx].aps.size())
		return;
	SclConnectedAP& cap = m_model.subnets[subnetIdx].aps[apIdx];
	QDialog dlg(this);
	dlg.setWindowTitle(isGse ? tr("Add GSE address") : tr("Add SMV address"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* ldEdit = new QLineEdit(m_model.ied.lds.empty() ? QString() : QString::fromUtf8(m_model.ied.lds[0].inst.c_str()), &dlg);
	QLineEdit* cbEdit = new QLineEdit(isGse ? "gcb1" : "msvcb1", &dlg);
	QLineEdit* macEdit = new QLineEdit(isGse ? "01-0C-CD-01-00-01" : "01-0C-CD-04-00-01", &dlg);
	QLineEdit* appidEdit = new QLineEdit(isGse ? "0x0001" : "0x4000", &dlg);
	form->addRow(tr("ldInst"), ldEdit);
	form->addRow(tr("cbName"), cbEdit);
	form->addRow(tr("MAC-Address"), macEdit);
	form->addRow(tr("APPID"), appidEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	SclCommAddress addr;
	addr.ldInst = ldEdit->text().toUtf8().constData();
	addr.cbName = cbEdit->text().toUtf8().constData();
	addr.pParams.push_back(std::make_pair("MAC-Address", macEdit->text().toUtf8().constData()));
	addr.pParams.push_back(std::make_pair("APPID", appidEdit->text().toUtf8().constData()));
	addr.pParams.push_back(std::make_pair("VLAN-ID", "0x001"));
	addr.pParams.push_back(std::make_pair("VLAN-PRIORITY", "4"));
	if (isGse)
	{
		addr.minTime = "10";
		addr.maxTime = "1000";
		cap.gses.push_back(addr);
	}
	else
	{
		cap.smvs.push_back(addr);
	}
	buildTree();
	log(tr("Add %1 communication address %2/%3").arg(isGse ? tr("GSE") : tr("SMV"))
		.arg(QString::fromUtf8(addr.ldInst.c_str())).arg(QString::fromUtf8(addr.cbName.c_str())));
}

void MainWindow::deleteCommAddress(int subnetIdx, int apIdx, bool isGse, int addrIdx)
{
	if (subnetIdx < 0 || subnetIdx >= (int)m_model.subnets.size())
		return;
	if (apIdx < 0 || apIdx >= (int)m_model.subnets[subnetIdx].aps.size())
		return;
	SclConnectedAP& cap = m_model.subnets[subnetIdx].aps[apIdx];
	std::vector<SclCommAddress>& list = isGse ? cap.gses : cap.smvs;
	if (addrIdx < 0 || addrIdx >= (int)list.size())
		return;
	if (QMessageBox::question(this, tr("Confirm"), tr("Delete this communication address?")) != QMessageBox::Yes)
		return;
	list.erase(list.begin() + addrIdx);
	buildTree();
	log(tr("Delete communication address"));
}

void MainWindow::addDataSet(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add DataSet"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("ds%1").arg((int)ln->dataSets.size() + 1), &dlg);
	QLineEdit* descEdit = new QLineEdit(&dlg);
	form->addRow(tr("Name"), nameEdit);
	form->addRow(tr("Description"), descEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name))
	{
		QMessageBox::warning(this, tr("Validation failed"), tr("DataSet name must be valid"));
		return;
	}
	for (size_t i = 0; i < ln->dataSets.size(); ++i)
	{
		if (ln->dataSets[i].name == name)
		{
			QMessageBox::warning(this, tr("Validation failed"), tr("DataSet name must be unique within this logical node"));
			return;
		}
	}
	SclDataSet ds;
	ds.name = name;
	ds.desc = descEdit->text().toUtf8().constData();
	if (!editDataSetDialog(ds, li))
		return;
	ln->dataSets.push_back(ds);
	buildTree();
	log(tr("Add DataSet %1 (%2 FCDA)").arg(nameEdit->text()).arg((int)ds.fcdas.size()));
}

bool MainWindow::editDataSetDialog(SclDataSet& ds, int li)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0)
		return false;

	// 第三期任务 2：FCDA 增量编辑 + DA 级成员 + 上移/下移/删除
	std::vector<SclFcda> cur = ds.fcdas;   // 当前成员列表（提交时写回）
	bool treeUpdating = false;

	auto sameRef = [](const SclFcda& f, const SclFcda& g) {
		return f.ldInst == g.ldInst && f.prefix == g.prefix && f.lnClass == g.lnClass &&
			f.lnInst == g.lnInst && f.doName == g.doName && f.daName == g.daName;
	};

	QDialog dlg(this);
	dlg.setWindowTitle(tr("Edit DataSet: %1").arg(QString::fromUtf8(ds.name.c_str())));
	dlg.resize(960, 620);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);
	vbox->addWidget(new QLabel(tr("Check a DO for DO-level FCDA, or a DAI for DA-level FCDA; uncheck to remove. Use buttons to reorder the list."), &dlg));
	QHBoxLayout* mid = new QHBoxLayout;
	QTreeWidget* srcTree = new QTreeWidget(&dlg);
	srcTree->setHeaderLabels(QStringList() << tr("Source model") << tr("CDC"));
	mid->addWidget(srcTree, 3);

	QVBoxLayout* listLay = new QVBoxLayout;
	QListWidget* fcdaList = new QListWidget(&dlg);
	fcdaList->setSelectionMode(QAbstractItemView::SingleSelection);
	listLay->addWidget(fcdaList, 1);
	QHBoxLayout* listBtns = new QHBoxLayout;
	QPushButton* upBtn = new QPushButton(tr("Move up"), &dlg);
	QPushButton* downBtn = new QPushButton(tr("Move down"), &dlg);
	QPushButton* removeBtn = new QPushButton(tr("Remove"), &dlg);
	listBtns->addWidget(upBtn);
	listBtns->addWidget(downBtn);
	listBtns->addWidget(removeBtn);
	listBtns->addStretch(1);
	listLay->addLayout(listBtns);
	mid->addLayout(listLay, 2);
	vbox->addLayout(mid);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	vbox->addWidget(box);

	auto findFcdaIndex = [&](const SclFcda& f) -> int {
		for (size_t k = 0; k < cur.size(); ++k)
			if (sameRef(cur[k], f))
				return (int)k;
		return -1;
	};

	auto refreshList = [&]() {
		fcdaList->clear();
		for (size_t k = 0; k < cur.size(); ++k)
		{
			QString text = QString::fromUtf8(cur[k].RefText().c_str()) +
				QString(" [") + QString::fromUtf8(cur[k].fc.c_str()) + QString("]");
			if (cur[k].daName.empty())
				text += tr(" (DO level)");
			fcdaList->addItem(text);
		}
	};

	// 源树：LD -> LN -> DO(勾选=DO 级) -> DAI(勾选=DA 级)；UserRole 存 "li:ni:di[:k]"
	auto addLnNodes = [&](QTreeWidgetItem* ldItem, const SclLDevice& mld, const SclLn& mln, int l, int ni) {
		QTreeWidgetItem* lnItem = new QTreeWidgetItem(ldItem, QStringList(QString::fromUtf8(mln.Name().c_str())));
		for (size_t d = 0; d < mln.dos.size(); ++d)
		{
			const SclDo& mdo = mln.dos[d];
			QTreeWidgetItem* doItem = new QTreeWidgetItem(lnItem,
				QStringList() << QString::fromUtf8(mdo.name.c_str()) << QString::fromUtf8(mdo.cdc.c_str()));
			doItem->setData(0, Qt::UserRole, QString("%1:%2:%3").arg(l).arg(ni).arg((int)d));
			SclFcda f;
			f.iedName = m_model.ied.name;
			f.ldInst = mld.inst;
			f.prefix = mln.prefix;
			f.lnClass = mln.lnClass;
			f.lnInst = mln.inst;
			f.doName = mdo.name;
			f.fc = cdcToDefaultFc(mdo.cdc);
			if (findFcdaIndex(f) >= 0)
				doItem->setCheckState(0, Qt::Checked);
			else
				doItem->setCheckState(0, Qt::Unchecked);
			std::vector<SclDaiSpec> specs = daiSpecsFor(mdo);
			for (size_t k = 0; k < specs.size(); ++k)
			{
				if (specs[k].path == "d")
					continue;
				QTreeWidgetItem* daiItem = new QTreeWidgetItem(doItem,
					QStringList() << QString::fromUtf8(specs[k].path.c_str())
						<< QString::fromUtf8(specs[k].fc.c_str()));
				daiItem->setData(0, Qt::UserRole,
					QString("%1:%2:%3:%4").arg(l).arg(ni).arg((int)d).arg((int)k));
				SclFcda df = f;
				df.daName = specs[k].path;
				df.fc = specs[k].fc;
				if (findFcdaIndex(df) >= 0)
					daiItem->setCheckState(0, Qt::Checked);
				else
					daiItem->setCheckState(0, Qt::Unchecked);
			}
		}
	};
	treeUpdating = true;
	for (size_t l = 0; l < m_model.ied.lds.size(); ++l)
	{
		const SclLDevice& mld = m_model.ied.lds[l];
		QTreeWidgetItem* ldItem = new QTreeWidgetItem(srcTree, QStringList(QString::fromUtf8(mld.inst.c_str())));
		addLnNodes(ldItem, mld, mld.ln0, (int)l, -1);
		for (size_t n = 0; n < mld.lns.size(); ++n)
			addLnNodes(ldItem, mld, mld.lns[n], (int)l, (int)n);
	}
	treeUpdating = false;

	// 勾选/取消勾选 → 增量追加/移除成员（保持现有顺序）
	QObject::connect(srcTree, &QTreeWidget::itemChanged, [&](QTreeWidgetItem* it, int col) {
		if (treeUpdating || col != 0)
			return;
		QStringList pp = it->data(0, Qt::UserRole).toString().split(':');
		if (pp.size() < 3)
			return;
		int li = pp[0].toInt();
		int ni = pp[1].toInt();
		int di = pp[2].toInt();
		SclLDevice* mld = ldAt(li);
		SclLn* mln = lnAt(li, ni);
		SclDo* mdo = doAt(li, ni, di);
		if (mld == 0 || mln == 0 || mdo == 0)
			return;
		SclFcda f;
		f.iedName = m_model.ied.name;
		f.ldInst = mld->inst;
		f.prefix = mln->prefix;
		f.lnClass = mln->lnClass;
		f.lnInst = mln->inst;
		f.doName = mdo->name;
		bool checked = (it->checkState(0) == Qt::Checked);
		if (pp[0] == "do")
		{
			f.fc = cdcToDefaultFc(mdo->cdc);
		}
		else if (pp[0] == "dai" && pp.size() >= 4)
		{
			std::vector<SclDaiSpec> specs = daiSpecsFor(*mdo);
			int k = pp[3].toInt();
			if (k < 0 || k >= (int)specs.size())
				return;
			f.daName = specs[k].path;
			f.fc = specs[k].fc;
		}
		else
		{
			return;
		}
		int idx = findFcdaIndex(f);
		if (checked && idx < 0)
			cur.push_back(f);
		else if (!checked && idx >= 0)
			cur.erase(cur.begin() + idx);
		refreshList();
	});

	// 由 FCDA 反查树项并取消勾选（删除按钮联动）
	auto uncheckFcda = [&](const SclFcda& f) {
		for (int t = 0; t < srcTree->topLevelItemCount(); ++t)
		{
			QTreeWidgetItem* ldItem = srcTree->topLevelItem(t);
			for (int a = 0; a < ldItem->childCount(); ++a)
			{
				QTreeWidgetItem* lnItem = ldItem->child(a);
				for (int b = 0; b < lnItem->childCount(); ++b)
				{
					QTreeWidgetItem* doItem = lnItem->child(b);
					for (int c = -1; c < doItem->childCount(); ++c)
					{
						QTreeWidgetItem* it = (c < 0) ? doItem : doItem->child(c);
						QStringList pp = it->data(0, Qt::UserRole).toString().split(':');
						if (pp.size() < 3)
							continue;
						int li2 = pp[0].toInt();
						int ni2 = pp[1].toInt();
						int di2 = pp[2].toInt();
						SclLDevice* mld = ldAt(li2);
						SclLn* mln = lnAt(li2, ni2);
						SclDo* mdo = doAt(li2, ni2, di2);
						if (mld == 0 || mln == 0 || mdo == 0)
							continue;
						SclFcda g;
						g.iedName = m_model.ied.name;
						g.ldInst = mld->inst;
						g.prefix = mln->prefix;
						g.lnClass = mln->lnClass;
						g.lnInst = mln->inst;
						g.doName = mdo->name;
						if (pp[0] == "dai" && pp.size() >= 4)
						{
							std::vector<SclDaiSpec> specs = daiSpecsFor(*mdo);
							int k = pp[3].toInt();
							if (k < 0 || k >= (int)specs.size())
								continue;
							g.daName = specs[k].path;
							g.fc = specs[k].fc;
						}
						else
						{
							g.fc = cdcToDefaultFc(mdo->cdc);
						}
						if (sameRef(g, f))
						{
							it->setCheckState(0, Qt::Unchecked);
							return;
						}
					}
				}
			}
		}
	};

	QObject::connect(upBtn, &QPushButton::clicked, [&]() {
		int row = fcdaList->currentRow();
		if (row <= 0 || row >= (int)cur.size())
			return;
		std::swap(cur[row], cur[row - 1]);
		refreshList();
		fcdaList->setCurrentRow(row - 1);
	});
	QObject::connect(downBtn, &QPushButton::clicked, [&]() {
		int row = fcdaList->currentRow();
		if (row < 0 || row + 1 >= (int)cur.size())
			return;
		std::swap(cur[row], cur[row + 1]);
		refreshList();
		fcdaList->setCurrentRow(row + 1);
	});
	QObject::connect(removeBtn, &QPushButton::clicked, [&]() {
		int row = fcdaList->currentRow();
		if (row < 0 || row >= (int)cur.size())
			return;
		SclFcda f = cur[row];
		cur.erase(cur.begin() + row);
		uncheckFcda(f);
		refreshList();
	});

	refreshList();

	if (dlg.exec() != QDialog::Accepted)
		return false;

	ds.fcdas = cur;
	log(tr("DataSet %1 updated to %2 FCDA").arg(QString::fromUtf8(ds.name.c_str())).arg((int)ds.fcdas.size()));
	return true;
}

void MainWindow::deleteDataSet(int li, int ni, int di)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0 || di < 0 || di >= (int)ln->dataSets.size())
		return;
	if (QMessageBox::question(this, tr("Confirm"),
		tr("Delete DataSet %1?").arg(QString::fromUtf8(ln->dataSets[di].name.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(ln->dataSets[di].name.c_str());
	ln->dataSets.erase(ln->dataSets.begin() + di);
	buildTree();
	log(tr("Delete DataSet %1").arg(name));
}

// ---------------- ExtRef 外部信号引用（第三期任务 1） ----------------

// 编辑一条 ExtRef：左侧当前模型浏览树（点击 DO/DAI 自动填充表单），右侧 9 字段表单
bool MainWindow::editExtRefDialog(SclExtRef& er)
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("External reference (ExtRef)"));
	dlg.resize(900, 560);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);
	vbox->addWidget(new QLabel(tr("Click a DO/DAI in the source tree to fill the form; edit fields for cross-IED references."), &dlg));
	QHBoxLayout* mid = new QHBoxLayout;
	QTreeWidget* srcTree = new QTreeWidget(&dlg);
	srcTree->setHeaderLabels(QStringList() << tr("Source model") << tr("CDC"));
	mid->addWidget(srcTree, 3);
	QWidget* formPanel = new QWidget(&dlg);
	QFormLayout* form = new QFormLayout(formPanel);
	QLineEdit* iedEdit = new QLineEdit(QString::fromUtf8(er.iedName.c_str()), &dlg);
	QLineEdit* ldEdit = new QLineEdit(QString::fromUtf8(er.ldInst.c_str()), &dlg);
	QLineEdit* prefixEdit = new QLineEdit(QString::fromUtf8(er.prefix.c_str()), &dlg);
	QLineEdit* lnClassEdit = new QLineEdit(QString::fromUtf8(er.lnClass.c_str()), &dlg);
	QLineEdit* lnInstEdit = new QLineEdit(QString::fromUtf8(er.lnInst.c_str()), &dlg);
	QLineEdit* doEdit = new QLineEdit(QString::fromUtf8(er.doName.c_str()), &dlg);
	QLineEdit* daEdit = new QLineEdit(QString::fromUtf8(er.daName.c_str()), &dlg);
	QLineEdit* descEdit = new QLineEdit(QString::fromUtf8(er.desc.c_str()), &dlg);
	QLineEdit* intAddrEdit = new QLineEdit(QString::fromUtf8(er.intAddr.c_str()), &dlg);
	form->addRow(tr("iedName"), iedEdit);
	form->addRow(tr("ldInst"), ldEdit);
	form->addRow(tr("prefix"), prefixEdit);
	form->addRow(tr("lnClass"), lnClassEdit);
	form->addRow(tr("lnInst"), lnInstEdit);
	form->addRow(tr("doName"), doEdit);
	form->addRow(tr("daName (empty = DO level)"), daEdit);
	form->addRow(tr("Description"), descEdit);
	form->addRow(tr("intAddr"), intAddrEdit);
	mid->addWidget(formPanel, 2);
	vbox->addLayout(mid);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	vbox->addWidget(box);

	// 源树：LD -> LN -> DO -> DAI（点击叶子/DO 自动填表；节点 path 存 UserRole）
	auto addLnNodes = [&](QTreeWidgetItem* ldItem, const SclLn& mln, int li, int ni) {
		QTreeWidgetItem* lnItem = new QTreeWidgetItem(ldItem,
			QStringList(QString::fromUtf8(mln.Name().c_str())));
		for (size_t d = 0; d < mln.dos.size(); ++d)
		{
			QTreeWidgetItem* doItem = new QTreeWidgetItem(lnItem,
				QStringList() << QString::fromUtf8(mln.dos[d].name.c_str())
					<< QString::fromUtf8(mln.dos[d].cdc.c_str()));
			doItem->setData(0, Qt::UserRole, QString("do:%1:%2:%3").arg(li).arg(ni).arg((int)d));
			std::vector<SclDaiSpec> specs = daiSpecsFor(mln.dos[d]);
			for (size_t k = 0; k < specs.size(); ++k)
			{
				if (specs[k].path == "d")
					continue;
				QTreeWidgetItem* daiItem = new QTreeWidgetItem(doItem,
					QStringList() << QString::fromUtf8(specs[k].path.c_str())
						<< QString::fromUtf8(specs[k].fc.c_str()));
				daiItem->setData(0, Qt::UserRole,
					QString("dai:%1:%2:%3:%4").arg(li).arg(ni).arg((int)d).arg((int)k));
			}
		}
	};
	for (size_t l = 0; l < m_model.ied.lds.size(); ++l)
	{
		const SclLDevice& mld = m_model.ied.lds[l];
		QTreeWidgetItem* ldItem = new QTreeWidgetItem(srcTree, QStringList(QString::fromUtf8(mld.inst.c_str())));
		addLnNodes(ldItem, mld.ln0, (int)l, -1);
		for (size_t n = 0; n < mld.lns.size(); ++n)
			addLnNodes(ldItem, mld.lns[n], (int)l, (int)n);
	}
	srcTree->expandToDepth(2);

	// 点击 DO/DAI 自动填充表单（iedName 取当前 IED，跨装置引用可手改）
	connect(srcTree, &QTreeWidget::itemClicked, this,
		[this, iedEdit, ldEdit, prefixEdit, lnClassEdit, lnInstEdit, doEdit, daEdit]
		(QTreeWidgetItem* it, int) {
			QString tag = it->data(0, Qt::UserRole).toString();
			QStringList pp = tag.split(':');
			if (pp.size() < 4)
				return;
			int li = pp[1].toInt();
			int ni = pp[2].toInt();
			int di = pp[3].toInt();
			SclLDevice* mld = ldAt(li);
			SclLn* mln = lnAt(li, ni);
			SclDo* mdo = doAt(li, ni, di);
			if (mld == 0 || mln == 0 || mdo == 0)
				return;
			iedEdit->setText(QString::fromUtf8(m_model.ied.name.c_str()));
			ldEdit->setText(QString::fromUtf8(mld->inst.c_str()));
			prefixEdit->setText(QString::fromUtf8(mln->prefix.c_str()));
			lnClassEdit->setText(QString::fromUtf8(mln->lnClass.c_str()));
			lnInstEdit->setText(QString::fromUtf8(mln->inst.c_str()));
			doEdit->setText(QString::fromUtf8(mdo->name.c_str()));
			if (pp[0] == "dai" && pp.size() >= 5)
			{
				std::vector<SclDaiSpec> specs = daiSpecsFor(*mdo);
				int k = pp[4].toInt();
				if (k >= 0 && k < (int)specs.size())
					daEdit->setText(QString::fromUtf8(specs[k].path.c_str()));
			}
			else
			{
				daEdit->clear();
			}
		});

	bool accepted = false;
	connect(box, &QDialogButtonBox::accepted, [&]() {
		QString ied = iedEdit->text();
		QString doN = doEdit->text();
		QString lnClass = lnClassEdit->text();
		QString daN = daEdit->text();
		if (ied.isEmpty())
		{
			QMessageBox::warning(&dlg, tr("Validation failed"), tr("iedName cannot be empty"));
			return;
		}
		if (lnClass.isEmpty() || !validIdent(lnClass.toUtf8().constData()))
		{
			QMessageBox::warning(&dlg, tr("Validation failed"), tr("lnClass must be valid"));
			return;
		}
		if (doN.isEmpty() || !validIdent(doN.toUtf8().constData()))
		{
			QMessageBox::warning(&dlg, tr("Validation failed"), tr("doName must be valid"));
			return;
		}
		if (!daN.isEmpty() && !validIdent(daN.toUtf8().constData()))
		{
			QMessageBox::warning(&dlg, tr("Validation failed"), tr("daName must be valid"));
			return;
		}
		er.iedName = ied.toUtf8().constData();
		er.ldInst = ldEdit->text().toUtf8().constData();
		er.prefix = prefixEdit->text().toUtf8().constData();
		er.lnClass = lnClass.toUtf8().constData();
		er.lnInst = lnInstEdit->text().toUtf8().constData();
		er.doName = doN.toUtf8().constData();
		er.daName = daN.toUtf8().constData();
		er.desc = descEdit->text().toUtf8().constData();
		er.intAddr = intAddrEdit->text().toUtf8().constData();
		accepted = true;
		dlg.accept();
	});
	connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	dlg.exec();
	return accepted;
}

void MainWindow::addExternalRef(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	SclExtRef er;
	if (!editExtRefDialog(er))
		return;
	ln->extRefs.push_back(er);
	buildTree();
	log(tr("Add external reference %1").arg(QString::fromUtf8(er.RefText().c_str())));
}

void MainWindow::editExternalRef(int li, int ni, int idx)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0 || idx < 0 || idx >= (int)ln->extRefs.size())
		return;
	SclExtRef er = ln->extRefs[idx];
	if (!editExtRefDialog(er))
		return;
	ln->extRefs[idx] = er;
	buildTree();
	log(tr("Update external reference %1").arg(QString::fromUtf8(er.RefText().c_str())));
}

void MainWindow::deleteExternalRef(int li, int ni, int idx)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0 || idx < 0 || idx >= (int)ln->extRefs.size())
		return;
	QString text = QString::fromUtf8(ln->extRefs[idx].RefText().c_str());
	if (QMessageBox::question(this, tr("Confirm"),
		tr("Delete external reference %1?").arg(text)) != QMessageBox::Yes)
		return;
	ln->extRefs.erase(ln->extRefs.begin() + idx);
	buildTree();
	log(tr("Delete external reference %1").arg(text));
}

// goose
SclCommAddress* MainWindow::findGseAddress(const std::string& ldInst, const std::string& cbName)
{
	for (size_t s = 0; s < m_model.subnets.size(); ++s) {
		for (size_t c = 0; c < m_model.subnets[s].aps.size(); ++c) {
			for (size_t g = 0; g < m_model.subnets[s].aps[c].gses.size(); ++g) 
			{
				SclCommAddress& a = m_model.subnets[s].aps[c].gses[g];
				if (a.ldInst == ldInst && a.cbName == cbName)
					return &a;
			}
		}
	}
	return 0;
}

// sv
SclCommAddress* MainWindow::findSmvAddress(const std::string& ldInst, const std::string& cbName)
{
	for (size_t s = 0; s < m_model.subnets.size(); ++s) {
		for (size_t c = 0; c < m_model.subnets[s].aps.size(); ++c) {
			for (size_t g = 0; g < m_model.subnets[s].aps[c].smvs.size(); ++g)
			{
				SclCommAddress& a = m_model.subnets[s].aps[c].smvs[g];
				if (a.ldInst == ldInst && a.cbName == cbName)
					return &a;
			}
		}
	}
	return 0;
}

SclCommAddress* MainWindow::ensureGseAddress(const std::string& ldInst, const std::string& cbName)
{
	SclCommAddress* existing = findGseAddress(ldInst, cbName);
	if (existing)
		return existing;
	if (m_model.subnets.empty() || m_model.subnets[0].aps.empty())
	{
		SclConnectedAP cap;
		cap.apName = m_model.ied.apName;
		cap.iedName = m_model.ied.name;
		SclSubNetwork sn;
		sn.name = "SubNetworkName";
		sn.type = "8-MMS";
		sn.aps.push_back(cap);
		m_model.subnets.push_back(sn);
	}
	SclCommAddress addr;
	addr.ldInst = ldInst;
	addr.cbName = cbName;
	addr.pParams.push_back(std::make_pair("MAC-Address", "01-0C-CD-01-00-01"));
	addr.pParams.push_back(std::make_pair("APPID", QString("0x%1").arg(0x0001 + (int)m_model.subnets[0].aps[0].gses.size(), 4, 16, QChar('0')).toUtf8().constData()));
	addr.pParams.push_back(std::make_pair("VLAN-ID", "0x001"));
	addr.pParams.push_back(std::make_pair("VLAN-PRIORITY", "4"));
	m_model.subnets[0].aps[0].gses.push_back(addr);
	return &m_model.subnets[0].aps[0].gses.back();
}

SclCommAddress* MainWindow::ensureSmvAddress(const std::string& ldInst, const std::string& cbName)
{
	SclCommAddress* existing = findSmvAddress(ldInst, cbName);
	if (existing)
		return existing;
	if (m_model.subnets.empty() || m_model.subnets[0].aps.empty())
	{
		SclConnectedAP cap;
		cap.apName = m_model.ied.apName;
		cap.iedName = m_model.ied.name;
		SclSubNetwork sn;
		sn.name = "SubNetworkName";
		sn.type = "8-MMS";
		sn.aps.push_back(cap);
		m_model.subnets.push_back(sn);
	}
	SclCommAddress addr;
	addr.ldInst = ldInst;
	addr.cbName = cbName;
	addr.pParams.push_back(std::make_pair("MAC-Address", "01-0C-CD-04-00-01"));
	addr.pParams.push_back(std::make_pair("APPID", QString("0x%1").arg(0x4000 + (int)m_model.subnets[0].aps[0].smvs.size(), 4, 16, QChar('0')).toUtf8().constData()));
	addr.pParams.push_back(std::make_pair("VLAN-ID", "0x001"));
	addr.pParams.push_back(std::make_pair("VLAN-PRIORITY", "4"));
	m_model.subnets[0].aps[0].smvs.push_back(addr);
	return &m_model.subnets[0].aps[0].smvs.back();
}

void MainWindow::addReport(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	if (ln->dataSets.empty())
	{
		QMessageBox::warning(this, tr("Hint"), tr("Please add a DataSet under this logical node first"));
		return;
	}
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add report control block"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("brcb%1").arg((int)ln->reports.size() + 1), &dlg);
	QComboBox* dsCombo = new QComboBox(&dlg);
	for (size_t i = 0; i < ln->dataSets.size(); ++i)
		dsCombo->addItem(QString::fromUtf8(ln->dataSets[i].name.c_str()));
	QComboBox* bufferedCombo = new QComboBox(&dlg);
	bufferedCombo->addItem(tr("BRCB (buffered)"));
	bufferedCombo->addItem(tr("URCB (unbuffered)"));
	QLineEdit* rptIdEdit = new QLineEdit(m_model.ied.name.c_str(), &dlg);
	form->addRow(tr("Name"), nameEdit);
	form->addRow(tr("DataSets"), dsCombo);
	form->addRow(tr("Type"), bufferedCombo);
	form->addRow(tr("RptID"), rptIdEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name))
	{
		QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid"));
		return;
	}
	for (size_t i = 0; i < ln->reports.size(); ++i)
	{
		if (ln->reports[i].name == name)
		{
			QMessageBox::warning(this, tr("Validation failed"), tr("Control block name must be unique"));
			return;
		}
	}
	SclReportControl rc;
	rc.name = name;
	rc.dataSet = dsCombo->currentText().toUtf8().constData();
	rc.buffered = (bufferedCombo->currentIndex() == 0);
	rc.rptId = rptIdEdit->text().toUtf8().constData();
	if (rc.rptId.empty())
		rc.rptId = m_model.ied.name + "_" + (ldAt(li) ? ldAt(li)->inst : std::string("LD")) + "_" + name;
	rc.confRev = "1";
	rc.trgOps.present = true;
	rc.trgOps.dchg = true;
	rc.trgOps.qchg = true;
	rc.trgOps.period = true;
	rc.trgOps.gi = true;
	rc.optFields.present = true;
	rc.optFields.seqNum = true;
	rc.optFields.timeStamp = true;
	rc.optFields.reasonCode = true;
	rc.optFields.bufOvfl = true;
	rc.optFields.entryID = true;
	ln->reports.push_back(rc);
	buildTree();
	log(tr("Add report control block %1 (%2, DataSet %3)").arg(QString::fromUtf8(name.c_str()))
		.arg(rc.buffered ? tr("BRCB") : tr("URCB")).arg(QString::fromUtf8(rc.dataSet.c_str())));
}

void MainWindow::addGseControl(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	SclLDevice* ld = ldAt(li);
	if (ln == 0 || ld == 0)
		return;
	if (ln->dataSets.empty())
	{
		QMessageBox::warning(this, tr("Hint"), tr("Please add a DataSet under this logical node first"));
		return;
	}
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add GOOSE control block"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("gcb%1").arg((int)ln->gses.size() + 1), &dlg);
	QComboBox* dsCombo = new QComboBox(&dlg);
	for (size_t i = 0; i < ln->dataSets.size(); ++i)
		dsCombo->addItem(QString::fromUtf8(ln->dataSets[i].name.c_str()));
	QLineEdit* appIdEdit = new QLineEdit(QString("%1_%2_gcb").arg(QString::fromUtf8(m_model.ied.name.c_str()))
		.arg(QString::fromUtf8(ld->inst.c_str())), &dlg);
	form->addRow(tr("Name"), nameEdit);
	form->addRow(tr("DataSets"), dsCombo);
	form->addRow(tr("appID"), appIdEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name))
	{
		QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid"));
		return;
	}
	for (size_t i = 0; i < ln->gses.size(); ++i)
	{
		if (ln->gses[i].name == name)
		{
			QMessageBox::warning(this, tr("Validation failed"), tr("Control block name must be unique"));
			return;
		}
	}
	SclGseControl gc;
	gc.name = name;
	gc.dataSet = dsCombo->currentText().toUtf8().constData();
	gc.appId = appIdEdit->text().toUtf8().constData();
	if (gc.appId.empty())
		gc.appId = m_model.ied.name + "_" + ld->inst + "_" + name;
	gc.confRev = "1";
	ln->gses.push_back(gc);
	ensureGseAddress(ld->inst, name);
	buildTree();
	log(tr("Add GOOSE control block %1 (DataSet %2); GSE address auto-created, adjust it under the Communication node")
		.arg(QString::fromUtf8(name.c_str())).arg(QString::fromUtf8(gc.dataSet.c_str())));
}

void MainWindow::addSmvControl(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	SclLDevice* ld = ldAt(li);
	if (ln == 0 || ld == 0)
		return;
	if (ln->dataSets.empty())
	{
		QMessageBox::warning(this, tr("Hint"), tr("Please add a DataSet under this logical node first"));
		return;
	}
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add sampled value control block"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("msvcb%1").arg((int)ln->smvs.size() + 1), &dlg);
	QComboBox* dsCombo = new QComboBox(&dlg);
	for (size_t i = 0; i < ln->dataSets.size(); ++i)
		dsCombo->addItem(QString::fromUtf8(ln->dataSets[i].name.c_str()));
	QLineEdit* smvIdEdit = new QLineEdit(QString("%1_%2_msvcb").arg(QString::fromUtf8(m_model.ied.name.c_str()))
		.arg(QString::fromUtf8(ld->inst.c_str())), &dlg);
	QLineEdit* rateEdit = new QLineEdit("80", &dlg);
	form->addRow(tr("Name"), nameEdit);
	form->addRow(tr("DataSets"), dsCombo);
	form->addRow(tr("smvID"), smvIdEdit);
	form->addRow(tr("smpRate"), rateEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name))
	{
		QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid"));
		return;
	}
	for (size_t i = 0; i < ln->smvs.size(); ++i)
	{
		if (ln->smvs[i].name == name)
		{
			QMessageBox::warning(this, tr("Validation failed"), tr("Control block name must be unique"));
			return;
		}
	}
	SclSampledValueControl sc;
	sc.name = name;
	sc.dataSet = dsCombo->currentText().toUtf8().constData();
	sc.smvId = smvIdEdit->text().toUtf8().constData();
	if (sc.smvId.empty())
		sc.smvId = m_model.ied.name + "_" + ld->inst + "_" + name;
	sc.smpRate = rateEdit->text().toUtf8().constData();
	if (sc.smpRate.empty())
		sc.smpRate = "80";
	sc.confRev = "1";
	ln->smvs.push_back(sc);
	ensureSmvAddress(ld->inst, name);
	buildTree();
	log(tr("Add sampled value control block %1; SMV address auto-created").arg(QString::fromUtf8(name.c_str())));
}

void MainWindow::addLogControl(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add log control block"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("lc%1").arg((int)ln->logs.size() + 1), &dlg);
	QLineEdit* logNameEdit = new QLineEdit(&dlg);
	form->addRow(tr("Name"), nameEdit);
	form->addRow(tr("logName"), logNameEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name))
	{
		QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid"));
		return;
	}
	SclLogControl lc;
	lc.name = name;
	lc.logName = logNameEdit->text().toUtf8().constData();
	lc.trgOps.present = true;
	ln->logs.push_back(lc);
	buildTree();
	log(tr("Add log control block %1").arg(QString::fromUtf8(name.c_str())));
}

void MainWindow::addSettingControl(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add setting control block"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("scb%1").arg((int)ln->settings.size() + 1), &dlg);
	QLineEdit* sgEdit = new QLineEdit("2", &dlg);
	form->addRow(tr("Name"), nameEdit);
	form->addRow(tr("numOfSG"), sgEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name))
	{
		QMessageBox::warning(this, tr("Validation failed"), tr("Name must be valid"));
		return;
	}
	SclSettingControl sc;
	sc.name = name;
	sc.numOfSG = sgEdit->text().toUtf8().constData();
	ln->settings.push_back(sc);
	buildTree();
	log(tr("Add setting control block %1").arg(QString::fromUtf8(name.c_str())));
}

void MainWindow::deleteControlBlock(const QString& kind, int li, int ni, int idx)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	QString name;
	if (kind == "rpt")
	{
		if (idx < 0 || idx >= (int)ln->reports.size()) return;
		name = QString::fromUtf8(ln->reports[idx].name.c_str());
		if (QMessageBox::question(this, tr("Confirm"), tr("Delete report control block %1?").arg(name)) != QMessageBox::Yes) return;
		ln->reports.erase(ln->reports.begin() + idx);
	}
	else if (kind == "gcb")
	{
		if (idx < 0 || idx >= (int)ln->gses.size()) return;
		name = QString::fromUtf8(ln->gses[idx].name.c_str());
		if (QMessageBox::question(this, tr("Confirm"), tr("Delete GOOSE control block %1?").arg(name)) != QMessageBox::Yes) return;
		ln->gses.erase(ln->gses.begin() + idx);
	}
	else if (kind == "smcb")
	{
		if (idx < 0 || idx >= (int)ln->smvs.size()) return;
		name = QString::fromUtf8(ln->smvs[idx].name.c_str());
		if (QMessageBox::question(this, tr("Confirm"), tr("Delete sampled value control block %1?").arg(name)) != QMessageBox::Yes) return;
		ln->smvs.erase(ln->smvs.begin() + idx);
	}
	else if (kind == "logcb")
	{
		if (idx < 0 || idx >= (int)ln->logs.size()) return;
		name = QString::fromUtf8(ln->logs[idx].name.c_str());
		if (QMessageBox::question(this, tr("Confirm"), tr("Delete log control block %1?").arg(name)) != QMessageBox::Yes) return;
		ln->logs.erase(ln->logs.begin() + idx);
	}
	else if (kind == "setcb")
	{
		if (idx < 0 || idx >= (int)ln->settings.size()) return;
		name = QString::fromUtf8(ln->settings[idx].name.c_str());
		if (QMessageBox::question(this, tr("Confirm"), tr("Delete setting control block %1?").arg(name)) != QMessageBox::Yes) return;
		ln->settings.erase(ln->settings.begin() + idx);
	}
	buildTree();
	log(tr("Delete control block %1").arg(name));
}

// ---------------- DO/DAI 编辑与模板库 ----------------
std::vector<SclCdcTemplate> MainWindow::mergedCdcTemplates() const
{
	std::vector<SclCdcTemplate> list = SclTemplates::BuiltinCdcTemplates();
    for(const auto& item : m_userCdc) {
        list.push_back(item);
    }
	return list;
}

SclCdcTemplate MainWindow::findCdcTemplate(const std::string& cdc, bool& found) const
{
	std::vector<SclCdcTemplate> list = mergedCdcTemplates();
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (list[i].cdc == cdc)
		{
			found = true;
			return list[i];
		}
	}
	found = false;
	return SclCdcTemplate();
}

QStringList MainWindow::enumIdList() const
{
	QStringList ids;
	ids << "Beh" << "ModKind" << "HealthKind" << "Dbpos" << "CtlModels";
	for (size_t i = 0; i < m_model.extraEnumTypes.size(); ++i)
	{
		QString id = QString::fromUtf8(m_model.extraEnumTypes[i].id.c_str());
		if (!ids.contains(id))
			ids << id;
	}
	return ids;
}

std::vector<std::pair<int, std::string> > MainWindow::enumValsFor(const std::string& id) const
{
	std::vector<std::pair<int, std::string> > vals = SclTemplates::EnumValsFor(id);
	if (!vals.empty())
		return vals;
	for (size_t i = 0; i < m_model.extraEnumTypes.size(); ++i)
	{
		if (m_model.extraEnumTypes[i].id == id)
			return m_model.extraEnumTypes[i].vals;
	}
	return vals;
}

std::vector<SclDaiSpec> MainWindow::daiSpecsFor(const SclDo& dobj) const
{
	return SclTemplates::DaiSpecsForDo(dobj.cdc, dobj.stValEnum, dobj.customDas, dobj.hasCustomDas);
}

bool MainWindow::cdcNeedsStValEnum(const std::vector<SclDa>& das)
{
	for (size_t i = 0; i < das.size(); ++i) {
		if (das[i].name == "stVal" && das[i].bType == "Enum" && das[i].structType.empty())
			return true;
	}
	return false;
}

bool MainWindow::daiTypeEditable(const std::string& bType)
{
	return bType == "BOOLEAN" || bType.compare(0, 3, "INT") == 0 || bType.compare(0, 4, "UINT") == 0 ||
		   bType == "FLOAT32" || bType == "FLOAT64" || bType == "Enum" ||
		   bType.compare(0, 9, "VisString") == 0 || bType.compare(0, 7, "Unicode") == 0;
}

bool MainWindow::daiValueValid(const SclDaiSpec& spec, const QString& value, QString& normalized) const
{
	const std::string& bType = spec.bType;
	if (bType == "BOOLEAN")
	{
		QString vs = value.trimmed().toLower();
		if (vs == "true" || vs == "1" || vs == tr("Yes"))
		{
			normalized = "true";
			return true;
		}
		if (vs == "false" || vs == "0" || vs == tr("No"))
		{
			normalized = "false";
			return true;
		}
		return false;
	}
	if (bType.compare(0, 3, "INT") == 0)
	{
		bool ok = false;
		qlonglong n = value.trimmed().toLongLong(&ok);
		if (!ok) return false;
		if (bType != "INT64" && bType != "INT128")
		{
			// 有符号窄整型范围粗校验
			qlonglong bits = 8;
			if (bType == "INT8") bits = 8;
			else if (bType == "INT16") bits = 16;
			else if (bType == "INT32") bits = 32;
			qlonglong lim = (qlonglong)1 << (bits - 1);
			if (n < -lim || n > lim - 1) return false;
		}
		normalized = QString::number(n);
		return true;
	}
	if (bType.compare(0, 4, "UINT") == 0)
	{
		bool ok = false;
		qulonglong n = value.trimmed().toULongLong(&ok);
		if (!ok) return false;
		normalized = QString::number(n);
		return true;
	}
	if (bType == "FLOAT32" || bType == "FLOAT64")
	{
		bool ok = false;
		double d = value.trimmed().toDouble(&ok);
		if (!ok) return false;
		normalized = QString::number(d);
		return true;
	}
	if (bType == "Enum")
	{
		std::vector<std::pair<int, std::string> > evals = enumValsFor(spec.enumId);
		for (size_t i = 0; i < evals.size(); ++i)
		{
			if (QString::fromUtf8(evals[i].second.c_str()) == value.trimmed())
			{
				normalized = value.trimmed();
				return true;
			}
		}
		return false;
	}
	if (bType.compare(0, 9, "VisString") == 0 || bType.compare(0, 7, "Unicode") == 0)
	{
		normalized = value;
		return true;
	}
	return false;
}

void MainWindow::loadUserTemplates()
{
	m_userCdc.clear();
	QString path = QCoreApplication::applicationDirPath() + "/config/templates_user.json";
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return;
	QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isArray())
		return;
	QJsonArray arr = doc.array();
	for (int i = 0; i < arr.size(); ++i)
	{
		QJsonObject o = arr[i].toObject();
		SclCdcTemplate t;
		t.cdc = o.value("cdc").toString().toUtf8().constData();
		t.desc = o.value("desc").toString().toUtf8().constData();
		t.isCustom = true;
		QJsonArray das = o.value("das").toArray();
		for (int j = 0; j < das.size(); ++j)
		{
			QJsonObject d = das[j].toObject();
			SclDa da;
			da.name = d.value("name").toString().toUtf8().constData();
			da.bType = d.value("bType").toString().toUtf8().constData();
			da.fc = d.value("fc").toString().toUtf8().constData();
			da.structType = d.value("structType").toString().toUtf8().constData();
			t.das.push_back(da);
		}
		if (!t.cdc.empty() && !t.das.empty())
			m_userCdc.push_back(t);
	}
}

void MainWindow::saveUserTemplates() const
{
	QString dir = QCoreApplication::applicationDirPath() + "/config";
	QDir().mkpath(dir);
	QJsonArray arr;
	for (size_t i = 0; i < m_userCdc.size(); ++i)
	{
		const SclCdcTemplate& t = m_userCdc[i];
		QJsonObject o;
		o.insert("cdc", QString::fromUtf8(t.cdc.c_str()));
		o.insert("desc", QString::fromUtf8(t.desc.c_str()));
		QJsonArray das;
		for (size_t j = 0; j < t.das.size(); ++j)
		{
			QJsonObject d;
			d.insert("name", QString::fromUtf8(t.das[j].name.c_str()));
			d.insert("bType", QString::fromUtf8(t.das[j].bType.c_str()));
			d.insert("fc", QString::fromUtf8(t.das[j].fc.c_str()));
			d.insert("structType", QString::fromUtf8(t.das[j].structType.c_str()));
			das.append(d);
		}
		o.insert("das", das);
		arr.append(o);
	}
	QFile f(dir + "/templates_user.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write(QJsonDocument(arr).toJson());
    }
}

void MainWindow::addDo(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	std::vector<SclCdcTemplate> templates = mergedCdcTemplates();

	QDialog dlg(this);
	dlg.setWindowTitle(tr("Add DO"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(&dlg);
	QComboBox* cdcCombo = new QComboBox(&dlg);
	for (size_t i = 0; i < templates.size(); ++i)
	{
		QString label = QString::fromUtf8(templates[i].cdc.c_str()) +
			(templates[i].isCustom ? tr(" (custom)") : QString());
		if (!templates[i].desc.empty())
			label += " - " + QString::fromUtf8(templates[i].desc.c_str());
		cdcCombo->addItem(label, QString::fromUtf8(templates[i].cdc.c_str()));
	}
	QComboBox* enumCombo = new QComboBox(&dlg);
	enumCombo->addItems(enumIdList());
	QLineEdit* descEdit = new QLineEdit(&dlg);
	form->addRow(tr("DO name"), nameEdit);
	form->addRow(tr("CDC template"), cdcCombo);
	form->addRow(tr("Enum type (stVal)"), enumCombo);
	form->addRow(tr("Description"), descEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	while (dlg.exec() == QDialog::Accepted)
	{
		std::string name = nameEdit->text().toUtf8().constData();
		std::string cdc = cdcCombo->currentData().toString().toUtf8().constData();
		if (!validIdent(name))
		{
			QMessageBox::warning(&dlg, tr("Validation failed"), tr("DO name must be 1-64 chars, start with a letter, letters/digits/underscore only"));
			continue;
		}
		bool dup = false;
        for (size_t i = 0; i < ln->dos.size(); ++i) {
			if (ln->dos[i].name == name) dup = true;
        }
		if (dup)
		{
			QMessageBox::warning(&dlg, tr("Validation failed"), tr("DO name must be unique within this logical node"));
			continue;
		}
		bool found = false;
		SclCdcTemplate tpl = findCdcTemplate(cdc, found);
		if (!found)
		{
			QMessageBox::warning(&dlg, tr("Validation failed"), tr("CDC template not found"));
			continue;
		}
		std::string stValEnum;
		if (cdcNeedsStValEnum(tpl.das))
			stValEnum = enumCombo->currentText().toUtf8().constData();
		SclDo dobj(name, cdc, stValEnum);
		if (tpl.isCustom)
		{
			dobj.hasCustomDas = true;
			dobj.customDas = tpl.das;
			dobj.customDoTypeId = "USR_" + cdc;
		}
		ln->dos.push_back(dobj);
		buildTree();
		log(tr("Add DO %1 (CDC=%2) to %3").arg(nameEdit->text())
			.arg(QString::fromUtf8(cdc.c_str()))
			.arg(QString::fromUtf8(ln->Name().c_str())));
		return;
	}
}

void MainWindow::editDo(int li, int ni, int di)
{
	SclLn* ln = lnAt(li, ni);
	SclDo* dobj = doAt(li, ni, di);
	if (ln == 0 || dobj == 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Rename DO"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString::fromUtf8(dobj->name.c_str()), &dlg);
	form->addRow(tr("DO name"), nameEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name) || name == dobj->name)
		return;
	for (size_t i = 0; i < ln->dos.size(); ++i)
	{
		if (ln->dos[i].name == name)
		{
			QMessageBox::warning(this, tr("Validation failed"), tr("DO name must be unique within this logical node"));
			return;
		}
	}
	QString old = QString::fromUtf8(dobj->name.c_str());
	dobj->name = name;
	buildTree();
	log(tr("DO renamed: %1 -> %2").arg(old).arg(QString::fromUtf8(name.c_str())));
}

void MainWindow::deleteDo(int li, int ni, int di)
{
	SclLn* ln = lnAt(li, ni);
	SclDo* dobj = doAt(li, ni, di);
	if (ln == 0 || dobj == 0)
		return;
	if (QMessageBox::question(this, tr("Confirm"),
		tr("Delete DO %1?").arg(QString::fromUtf8(dobj->name.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(dobj->name.c_str());
	ln->dos.erase(ln->dos.begin() + di);
	buildTree();
	log(tr("Delete DO %1").arg(name));
}

void MainWindow::manageTemplates()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Template library"));
	dlg.resize(820, 560);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);

	QTabWidget* tabs = new QTabWidget(&dlg);
	vbox->addWidget(tabs);

	// Tab1: CDC 模板
	QWidget* cdcTab = new QWidget(&dlg);
	QVBoxLayout* cdcLay = new QVBoxLayout(cdcTab);
	QHBoxLayout* searchLay = new QHBoxLayout;
	searchLay->addWidget(new QLabel(tr("Filter:"), &dlg));
	QLineEdit* searchEdit = new QLineEdit(&dlg);
	searchLay->addWidget(searchEdit);
	cdcLay->addLayout(searchLay);
	QTableWidget* table = new QTableWidget(0, 4, &dlg);
	table->setHorizontalHeaderLabels(QStringList() << tr("Type") << tr("CDC") << tr("DA count") << tr("DA list"));
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	cdcLay->addWidget(table);

	// 表格填充（filter 为 CDC 名筛选）
	auto fillTable = [&](const QString& filter) {
		table->setRowCount(0);
		std::vector<SclCdcTemplate> all = mergedCdcTemplates();
		int row = 0;
		for (size_t i = 0; i < all.size(); ++i)
		{
			QString cdc = QString::fromUtf8(all[i].cdc.c_str());
			if (!filter.isEmpty() && !cdc.contains(filter, Qt::CaseInsensitive))
				continue;
			table->insertRow(row);
			table->setItem(row, 0, new QTableWidgetItem(all[i].isCustom ? tr("Custom") : tr("Built-in")));
			table->setItem(row, 1, new QTableWidgetItem(cdc));
			table->setItem(row, 2, new QTableWidgetItem(QString::number((int)all[i].das.size())));
			QString das;
            for (size_t d = 0; d < all[i].das.size(); ++d) {
				das += (d ? "、" : "") + QString::fromUtf8(all[i].das[d].name.c_str());
            }
			table->setItem(row, 3, new QTableWidgetItem(das));
			table->item(row, 0)->setData(Qt::UserRole, (int)i);
			++row;
		}
	};
	fillTable(QString());
	connect(searchEdit, &QLineEdit::textChanged, this, [&dlg, fillTable](const QString& t) {
		Q_UNUSED(&dlg);
		fillTable(t);
	});

	QHBoxLayout* cdcBtns = new QHBoxLayout;
	QPushButton* addBtn = new QPushButton(tr("Add template"), &dlg);
	QPushButton* editBtn = new QPushButton(tr("Edit template"), &dlg);
	QPushButton* delBtn = new QPushButton(tr("Delete template"), &dlg);
	cdcBtns->addWidget(addBtn);
	cdcBtns->addWidget(editBtn);
	cdcBtns->addWidget(delBtn);
	cdcBtns->addStretch(1);
	cdcLay->addLayout(cdcBtns);
	tabs->addTab(cdcTab, tr("Common data classes (CDC)"));

	// Tab2: LNClass 模板（只读）
	QWidget* lnTab = new QWidget(&dlg);
	QVBoxLayout* lnLay = new QVBoxLayout(lnTab);
	QTableWidget* lnTable = new QTableWidget(0, 3, &dlg);
	lnTable->setHorizontalHeaderLabels(QStringList() << tr("LnClass") << tr("Description") << tr("DO list"));
	lnTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	lnTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	const std::vector<SclLnT>& lnTypes = SclTemplates::LnTypes();
	for (size_t i = 0; i < lnTypes.size(); ++i)
	{
		lnTable->insertRow((int)i);
		lnTable->setItem((int)i, 0, new QTableWidgetItem(QString::fromUtf8(lnTypes[i].lnClass)));
		lnTable->setItem((int)i, 1, new QTableWidgetItem(QString::fromUtf8(lnTypes[i].desc)));
		QString dos;
		for (int d = 0; d < lnTypes[i].count; ++d)
			dos += (d ? "、" : "") + QString::fromUtf8(lnTypes[i].dos[d].name);
		lnTable->setItem((int)i, 2, new QTableWidgetItem(dos));
	}
	lnLay->addWidget(new QLabel(tr("Logical node class templates are built-in standards (read-only). Pick from this table when adding logical nodes."), &dlg));
	lnLay->addWidget(lnTable);
	tabs->addTab(lnTab, tr("Logical node classes (LNClass)"));

	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, &dlg);
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	vbox->addWidget(box);

	// 编辑/新增 CDC 模板的内嵌对话框
	auto editCdcDialog = [&](SclCdcTemplate& t, bool isNew) -> bool {
		QDialog ed(&dlg);
		ed.setWindowTitle(isNew ? tr("Add CDC template") : tr("Edit CDC template"));
		ed.resize(560, 420);
		QVBoxLayout* lay = new QVBoxLayout(&ed);
		QFormLayout* form = new QFormLayout;
		QLineEdit* cdcEdit = new QLineEdit(QString::fromUtf8(t.cdc.c_str()), &ed);
		QLineEdit* descEdit = new QLineEdit(QString::fromUtf8(t.desc.c_str()), &ed);
		form->addRow(tr("CDC name"), cdcEdit);
		form->addRow(tr("Description"), descEdit);
		lay->addLayout(form);
		QTableWidget* daTable = new QTableWidget(0, 4, &ed);
		daTable->setHorizontalHeaderLabels(QStringList() << tr("name") << tr("bType") << tr("fc") << tr("type (required for Struct)"));
		QStringList bTypes, fcs;
		bTypes << "BOOLEAN" << "INT8" << "INT16" << "INT32" << "INT64" << "INT128" << "UINT8" << "UINT16"
               << "UINT32" << "UINT64" << "FLOAT32" << "FLOAT64" << "Enum" << "VisString255" << "VisString64"
               << "Unicode255" << "Quality" << "Timestamp" << "Struct" << "Octet64";
		fcs << "ST" << "MX" << "SP" << "SV" << "CF" << "DC" << "SG" << "SE" << "EX" << "BR" << "RP" << "CO";
		auto addDaRow = [daTable, bTypes, fcs](const SclDa& da) {
			int r = daTable->rowCount();
			daTable->insertRow(r);
			daTable->setItem(r, 0, new QTableWidgetItem(QString::fromUtf8(da.name.c_str())));
			QComboBox* bCombo = new QComboBox(daTable);
			bCombo->addItems(bTypes);
			int bi = bTypes.indexOf(QString::fromUtf8(da.bType.c_str()));
			bCombo->setCurrentIndex(bi < 0 ? 0 : bi);
			daTable->setCellWidget(r, 1, bCombo);
			QComboBox* fCombo = new QComboBox(daTable);
			fCombo->addItems(fcs);
			int fi = fcs.indexOf(QString::fromUtf8(da.fc.c_str()));
			fCombo->setCurrentIndex(fi < 0 ? 0 : fi);
			daTable->setCellWidget(r, 2, fCombo);
			daTable->setItem(r, 3, new QTableWidgetItem(QString::fromUtf8(da.structType.c_str())));
		};
        for (size_t i = 0; i < t.das.size(); ++i) {
			addDaRow(t.das[i]);
        }
		lay->addWidget(daTable);
		QHBoxLayout* daBtns = new QHBoxLayout;
		QPushButton* rowAdd = new QPushButton(tr("Add DA"), &ed);
		QPushButton* rowDel = new QPushButton(tr("Delete selected DA"), &ed);
		daBtns->addWidget(rowAdd);
		daBtns->addWidget(rowDel);
		daBtns->addStretch(1);
		lay->addLayout(daBtns);
		QDialogButtonBox* edBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &ed);
		lay->addWidget(edBox);
		connect(rowAdd, &QPushButton::clicked, daTable, [addDaRow, daTable]() {
			SclDa empty;
			empty.bType = "BOOLEAN";
			empty.fc = "ST";
			addDaRow(empty);
		});
		connect(rowDel, &QPushButton::clicked, daTable, [daTable]() {
			int r = daTable->currentRow();
			if (r >= 0)
				daTable->removeRow(r);
		});
		connect(edBox, SIGNAL(accepted()), &ed, SLOT(accept()));
		connect(edBox, SIGNAL(rejected()), &ed, SLOT(reject()));
        if (ed.exec() != QDialog::Accepted) {
			return false;
        }
		std::string cdc = cdcEdit->text().toUtf8().constData();
		if (!validIdent(cdc))
		{
			QMessageBox::warning(&ed, tr("Validation failed"), tr("CDC name must be valid"));
			return false;
		}
		if (isNew)
		{
			std::vector<SclCdcTemplate> all = mergedCdcTemplates();
			for (size_t i = 0; i < all.size(); ++i)
			{
				if (all[i].cdc == cdc)
				{
					QMessageBox::warning(&ed, tr("Validation failed"), tr("CDC name duplicates an existing template"));
					return false;
				}
			}
		}
		t.cdc = cdc;
		t.desc = descEdit->text().toUtf8().constData();
		t.das.clear();
		for (int r = 0; r < daTable->rowCount(); ++r)
		{
			SclDa da;
			da.name = daTable->item(r, 0)->text().toUtf8().constData();
			QComboBox* bCombo = (QComboBox*)daTable->cellWidget(r, 1);
			QComboBox* fCombo = (QComboBox*)daTable->cellWidget(r, 2);
			da.bType = bCombo ? bCombo->currentText().toUtf8().constData() : "BOOLEAN";
			da.fc = fCombo ? fCombo->currentText().toUtf8().constData() : "ST";
			da.structType = daTable->item(r, 3) ? daTable->item(r, 3)->text().toUtf8().constData() : "";
			if (!da.name.empty())
			{
				if (da.bType == "Struct" && da.structType.empty())
				{
					QMessageBox::warning(&ed, tr("Validation failed"), tr("A Struct DA requires a type (e.g. AnalogueValue_1)"));
					return false;
				}
				t.das.push_back(da);
			}
		}
		if (t.das.empty())
		{
			QMessageBox::warning(&ed, tr("Validation failed"), tr("At least one DA is required"));
			return false;
		}
		return true;
	};

	connect(addBtn, &QPushButton::clicked, this, [&]() {
		SclCdcTemplate t;
		t.isCustom = true;
		if (!editCdcDialog(t, true))
			return;
		m_userCdc.push_back(t);
		saveUserTemplates();
		fillTable(searchEdit->text());
		log(tr("Custom CDC template added: %1").arg(QString::fromUtf8(t.cdc.c_str())));
	});
	connect(editBtn, &QPushButton::clicked, this, [&]() {
		int r = table->currentRow();
		if (r < 0) return;
		int idx = table->item(r, 0)->data(Qt::UserRole).toInt();
		std::vector<SclCdcTemplate> all = mergedCdcTemplates();
		if (idx < 0 || idx >= (int)all.size()) return;
		if (!all[idx].isCustom)
		{
			QMessageBox::information(&dlg, tr("Hint"), tr("Built-in templates cannot be edited; create a custom template instead"));
			return;
		}
		SclCdcTemplate t = all[idx];
        if (!editCdcDialog(t, false)) {
			return;
        }
		for (size_t u = 0; u < m_userCdc.size(); ++u)
		{
			if (m_userCdc[u].cdc == all[idx].cdc)
			{
				m_userCdc[u] = t;
				break;
			}
		}
		saveUserTemplates();
		fillTable(searchEdit->text());
		log(tr("Custom CDC template updated: %1").arg(QString::fromUtf8(t.cdc.c_str())));
	});
	connect(delBtn, &QPushButton::clicked, this, [&]() {
		int r = table->currentRow();
		if (r < 0) return;
		int idx = table->item(r, 0)->data(Qt::UserRole).toInt();
		std::vector<SclCdcTemplate> all = mergedCdcTemplates();
		if (idx < 0 || idx >= (int)all.size()) return;
		if (!all[idx].isCustom)
		{
			QMessageBox::information(&dlg, tr("Hint"), tr("Built-in templates cannot be deleted"));
			return;
		}
		if (QMessageBox::question(&dlg, tr("Confirm"), tr("Delete template %1?").arg(QString::fromUtf8(all[idx].cdc.c_str()))) != QMessageBox::Yes)
			return;
		for (size_t u = 0; u < m_userCdc.size(); ++u)
		{
			if (m_userCdc[u].cdc == all[idx].cdc)
			{
				QString name = QString::fromUtf8(m_userCdc[u].cdc.c_str());
				m_userCdc.erase(m_userCdc.begin() + u);
				saveUserTemplates();
				fillTable(searchEdit->text());
				log(tr("Custom CDC template deleted: %1").arg(name));
				break;
			}
		}
	});

	dlg.exec();
}
