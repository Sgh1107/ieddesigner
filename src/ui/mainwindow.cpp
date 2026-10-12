#include "mainwindow.h"
#include "scltemplates.h"
#include "sclreader.h"
#include "jsonmodel.h"
#include "onlineclient.h"
#include "sclwriter.h"
#include "csvwriter.h"
#include "modelvalidate.h"

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
#include <QStringList>
#include <QTreeWidget>
#include <cctype>

namespace
{
	// 应用级剪贴板（IED/LD/LN 复制粘贴，进程内共享）
	SclLDevice g_clipLd;
	bool g_hasClipLd = false;
	SclLn g_clipLn;
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

	// 标识符合法性：1-64 位，字母开头，仅字母数字下划线
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
		for (size_t i = 0; i < s.size(); ++i)
			if (s[i] < '0' || s[i] > '9')
				return false;
		return true;
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

	m_tree = new QTreeWidget(this);
	m_tree->setColumnCount(2);
	m_tree->setHeaderLabels(QStringList() << tr("标题") << tr("详细"));
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	setCentralWidget(m_tree);

	QDockWidget* propDock = new QDockWidget(tr("属性"), this);
	m_props = new QTableWidget(this);
	m_props->setColumnCount(2);
	m_props->setHorizontalHeaderLabels(QStringList() << tr("属性") << tr("值"));
	propDock->setWidget(m_props);
	addDockWidget(Qt::LeftDockWidgetArea, propDock);

	QDockWidget* logDock = new QDockWidget(tr("日志"), this);
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
	connect(m_props, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(onPropChanged(QTableWidgetItem*)));

	m_model = MakeNewModel();
	buildTree();
	m_tree->header()->setStretchLastSection(false);          // 关闭最后一列自动拉伸
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);  // 第0列自适应拉伸
	m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);  // 第1列自适应拉伸
	m_tree->header()->resizeSection(0, 300);                 // 初始宽度，作为比例参考
	m_tree->header()->resizeSection(1, 500);
	log(tr("就绪。新建模型已载入：IED=%1").arg(QString::fromUtf8(m_model.ied.name.c_str())));
}

MainWindow::~MainWindow()
{
	logFileLine(tr("===== ieddesigner 退出 ====="));
	if (m_logFile.isOpen())
		m_logFile.close();
}

void MainWindow::initLogFile()
{
	QString dir = QCoreApplication::applicationDirPath() + "/log";
	if (!QDir().mkpath(dir))
		return;
	m_logFile.setFileName(dir + "/" + QDate::currentDate().toString("yyyy-MM-dd") + ".log");
	if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
		return;
	logFileLine(tr("===== ieddesigner 启动（Qt %1）=====").arg(QT_VERSION_STR));
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
	QToolBar* tb = addToolBar(tr("常用"));
	tb->setObjectName("mainToolbar");
	tb->setIconSize(QSize(20, 20));
	tb->setMovable(false);
	tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
	tb->addAction(m_actNew);
	tb->addAction(m_actOpen);
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
	QMenu* fileMenu = menuBar()->addMenu(tr("文件"));
	m_actNew = new QAction(QIcon(":/icons/new.svg"), tr("新建"), this);
	connect(m_actNew, SIGNAL(triggered()), this, SLOT(onNewModel()));
	fileMenu->addAction(m_actNew);
	m_actOpen = new QAction(QIcon(":/icons/open.svg"), tr("打开SCL模型"), this);
	connect(m_actOpen, SIGNAL(triggered()), this, SLOT(onOpenScl()));
	fileMenu->addAction(m_actOpen);
	m_recentMenu = fileMenu->addMenu(tr("最近打开"));
	rebuildRecentMenu();
	m_actSave = new QAction(QIcon(":/icons/save.svg"), tr("保存"), this);
	connect(m_actSave, SIGNAL(triggered()), this, SLOT(onSaveCid()));
	fileMenu->addAction(m_actSave);
	fileMenu->addAction(tr("另存为"), this, SLOT(onSaveCidAs()));
	QAction* closeAct = fileMenu->addAction(tr("关闭"));
	closeAct->setEnabled(false);
	fileMenu->addSeparator();
	fileMenu->addAction(tr("退出"), this, SLOT(close()));

	QMenu* toolMenu = menuBar()->addMenu(tr("工具"));
	QAction* expIcdAct = new QAction(QIcon(":/icons/export.svg"), tr("导出ICD文件"), this);
	connect(expIcdAct, SIGNAL(triggered()), this, SLOT(onExportIcd()));
	toolMenu->addAction(expIcdAct);
	m_actExportIcd = expIcdAct;
	toolMenu->addAction(tr("导出CID文件"), this, SLOT(onExportCid()));
	toolMenu->addAction(tr("导出CSV点表"), this, SLOT(onExportCsv()));
	toolMenu->addSeparator();
	m_actTemplates = new QAction(QIcon(":/icons/templates.svg"), tr("模板库管理"), this);
	connect(m_actTemplates, SIGNAL(triggered()), this, SLOT(manageTemplates()));
	toolMenu->addAction(m_actTemplates);
	m_actXml = new QAction(QIcon(":/icons/preview.svg"), tr("XML 源码预览"), this);
	connect(m_actXml, SIGNAL(triggered()), this, SLOT(onXmlPreview()));
	toolMenu->addAction(m_actXml);
	toolMenu->addSeparator();
	m_jsonAct = toolMenu->addAction(tr("导入在线JSON"));
	connect(m_jsonAct, SIGNAL(triggered()), this, SLOT(onImportJson()));
	m_onlineAct = toolMenu->addAction(tr("在线读取IED"));
	connect(m_onlineAct, SIGNAL(triggered()), this, SLOT(onOnlineRead()));
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
		QAction* emptyAct = m_recentMenu->addAction(tr("（无）"));
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
		QAction* clearAct = m_recentMenu->addAction(tr("清空历史"));
		connect(clearAct, &QAction::triggered, this, [this]() {
			QSettings s("newfert", "ieddesigner");
			s.remove("recentFiles");
			rebuildRecentMenu();
			log(tr("已清空最近打开历史"));
		});
	}
}

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
	if (kind == "ied") return ied;
	if (kind == "history" || kind == "hitem") return historyIc;
	if (kind == "ap" || kind == "cap") return serverIc;
	if (kind == "server") return serverIc;
	if (kind == "ld") return ldIc;
	if (kind == "ln" || kind == "ln0") return lnIc;
	if (kind == "do") return doIc;
	if (kind == "dai") return daiIc;
	if (kind == "comm" || kind == "subnet") return commIc;
	if (kind == "gse" || kind == "gcb") return gseIc;
	if (kind == "smv" || kind == "smcb") return smvIc;
	if (kind == "ds-sec" || kind == "ds") return dsIc;
	if (kind == "cb-sec" || kind == "rpt" || kind == "logcb" || kind == "setcb") return ctrlIc;
	return QIcon();
}

QTreeWidgetItem* MainWindow::addNode(QTreeWidgetItem* parent, const QString& text,
	const QString& detail, const QString& path)
{
	QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
	item->setText(0, text);
	item->setText(1, detail);
	item->setData(0, Qt::UserRole, path);
	item->setIcon(0, iconForPath(path));
	return item;
}

void MainWindow::buildTree()
{
	m_tree->clear();
	const SclIed& ied = m_model.ied;
	QTreeWidgetItem* iedItem = addNode(0, tr("IED ") + QString::fromUtf8(ied.name.c_str()),
		QString::fromUtf8(ied.desc.c_str()), "ied");

	// 修订历史
	if (!m_model.history.empty())
	{
		QTreeWidgetItem* histItem = addNode(iedItem, tr("修订历史"),
			tr("共 %1 条").arg((int)m_model.history.size()), "history");
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
		tr("主接入点"), "ap:0");
	QTreeWidgetItem* svrItem = addNode(apItem, tr("Server"), QString(), "server");
	for (size_t a = 0; a < ied.extraAps.size(); ++a)
	{
		QString detail;
		if (ied.extraAps[a].serverAt.empty())
			detail = tr("独立接入点");
		else
			detail = tr("ServerAt -> %1").arg(QString::fromUtf8(ied.extraAps[a].serverAt.c_str()));
		addNode(iedItem, tr("AccessPoint ") + QString::fromUtf8(ied.extraAps[a].name.c_str()),
			detail, QString("ap:%1").arg((int)(a + 1)));
	}

	// DO 下挂 DAI 子节点（点分路径，未设置的显示为空值）
	auto addDaiNodes = [this](QTreeWidgetItem* doItem, int li, int ni, int di, const SclDo& dobj) {
		std::vector<SclDaiSpec> specs = daiSpecsFor(dobj);
		for (size_t k = 0; k < specs.size(); ++k)
		{
			if (specs[k].path == "d")
				continue;
			QString detail = QString::fromUtf8(specs[k].bType.c_str()) + "/" + QString::fromUtf8(specs[k].fc.c_str());
			const std::string* val = dobj.FindDaiVal(specs[k].path);
			if (val)
				detail += " = " + QString::fromUtf8(val->c_str());
			addNode(doItem, tr("DAI ") + QString::fromUtf8(specs[k].path.c_str()), detail,
				QString("dai:%1:%2:%3:%4").arg(li).arg(ni).arg(di).arg((int)k));
		}
	};

	// LN 下数据集与控制块分组（仅在非空时显示）
	auto addCbSections = [&](QTreeWidgetItem* lnItem, int li, int ni, const SclLn& ln) {
		if (!ln.dataSets.empty())
		{
			QTreeWidgetItem* sec = addNode(lnItem, tr("数据集"), tr("%1 个").arg((int)ln.dataSets.size()),
				QString("ds-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.dataSets.size(); ++s)
				addNode(sec, tr("数据集 ") + QString::fromUtf8(ln.dataSets[s].name.c_str()),
					tr("%1 个 FCDA").arg((int)ln.dataSets[s].fcdas.size()),
					QString("ds:%1:%2:%3").arg(li).arg(ni).arg((int)s));
		}
		int cbCount = (int)(ln.reports.size() + ln.gses.size() + ln.smvs.size() + ln.logs.size() + ln.settings.size());
		if (cbCount > 0)
		{
			QTreeWidgetItem* sec = addNode(lnItem, tr("控制块"), tr("%1 个").arg(cbCount),
				QString("cb-sec:%1:%2").arg(li).arg(ni));
			for (size_t s = 0; s < ln.reports.size(); ++s)
				addNode(sec, tr("报告 ") + QString::fromUtf8(ln.reports[s].name.c_str()),
					(ln.reports[s].buffered ? tr("BRCB") : tr("URCB")) + QString(" -> ") +
					QString::fromUtf8(ln.reports[s].dataSet.c_str()),
					QString("rpt:%1:%2:%3").arg(li).arg(ni).arg((int)s));
			for (size_t s = 0; s < ln.gses.size(); ++s)
				addNode(sec, tr("GOOSE ") + QString::fromUtf8(ln.gses[s].name.c_str()),
					QString("appID=") + QString::fromUtf8(ln.gses[s].appId.c_str()),
					QString("gcb:%1:%2:%3").arg(li).arg(ni).arg((int)s));
			for (size_t s = 0; s < ln.smvs.size(); ++s)
				addNode(sec, tr("采样值 ") + QString::fromUtf8(ln.smvs[s].name.c_str()),
					QString("smvID=") + QString::fromUtf8(ln.smvs[s].smvId.c_str()),
					QString("smcb:%1:%2:%3").arg(li).arg(ni).arg((int)s));
			for (size_t s = 0; s < ln.logs.size(); ++s)
				addNode(sec, tr("日志 ") + QString::fromUtf8(ln.logs[s].name.c_str()),
					QString::fromUtf8(ln.logs[s].dataSet.c_str()),
					QString("logcb:%1:%2:%3").arg(li).arg(ni).arg((int)s));
			for (size_t s = 0; s < ln.settings.size(); ++s)
				addNode(sec, tr("定值 ") + QString::fromUtf8(ln.settings[s].name.c_str()),
					QString::fromUtf8(ln.settings[s].dataSet.c_str()),
					QString("setcb:%1:%2:%3").arg(li).arg(ni).arg((int)s));
		}
	};

	for (size_t li = 0; li < ied.lds.size(); ++li)
	{
		const SclLDevice& ld = ied.lds[li];
		QString ldDetail = QString::fromUtf8(ld.ldName.c_str());
		if (!ld.desc.empty())
			ldDetail += " " + QString::fromUtf8(ld.desc.c_str());
		if (!ld.comment.empty())
			ldDetail += " " + tr("[注释]");
		QTreeWidgetItem* ldItem = addNode(svrItem, tr("LDevice ") + QString::fromUtf8(ld.inst.c_str()),
			ldDetail, QString("ld:%1").arg((int)li));
		QTreeWidgetItem* ln0Item = addNode(ldItem, tr("LN0 ") + QString::fromUtf8(ld.ln0.lnClass.c_str()),
			tr("逻辑节点零"), QString("ln0:%1").arg((int)li));
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
				lnDetail += (lnDetail.isEmpty() ? QString() : QString(" ")) + tr("[注释]");
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
	QTreeWidgetItem* commItem = addNode(0, tr("通信"), tr("%1 个子网").arg((int)m_model.subnets.size()), "comm");
	for (size_t s = 0; s < m_model.subnets.size(); ++s)
	{
		const SclSubNetwork& sn = m_model.subnets[s];
		QTreeWidgetItem* snItem = addNode(commItem, tr("子网 ") + QString::fromUtf8(sn.name.c_str()),
			QString::fromUtf8(sn.type.c_str()), QString("subnet:%1").arg((int)s));
		for (size_t c = 0; c < sn.aps.size(); ++c)
		{
			const SclConnectedAP& cap = sn.aps[c];
			QTreeWidgetItem* capItem = addNode(snItem, tr("接入点 ") + QString::fromUtf8(cap.apName.c_str()),
				QString::fromUtf8(cap.iedName.c_str()) + tr("，P 参数 %1 项").arg((int)cap.pParams.size()),
				QString("cap:%1:%2").arg((int)s).arg((int)c));
			for (size_t g = 0; g < cap.gses.size(); ++g)
				addNode(capItem, tr("GSE ") + QString::fromUtf8(cap.gses[g].ldInst.c_str()) +
					QString("/") + QString::fromUtf8(cap.gses[g].cbName.c_str()),
					tr("地址 %1 项").arg((int)cap.gses[g].pParams.size()),
					QString("gse:%1:%2:%3").arg((int)s).arg((int)c).arg((int)g));
			for (size_t v = 0; v < cap.smvs.size(); ++v)
				addNode(capItem, tr("SMV ") + QString::fromUtf8(cap.smvs[v].ldInst.c_str()) +
					QString("/") + QString::fromUtf8(cap.smvs[v].cbName.c_str()),
					tr("地址 %1 项").arg((int)cap.smvs[v].pParams.size()),
					QString("smv:%1:%2:%3").arg((int)s).arg((int)c).arg((int)v));
		}
	}
	m_tree->expandToDepth(3);
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
	m_props->setRowCount(0);
	QStringList keys;
	QStringList values;
	QList<bool> editable;

	QStringList parts = path.split(':');
	if (path == "ied")
	{
		const SclIed& ied = m_model.ied;
		keys << tr("IED名称") << tr("描述") << tr("厂家") << tr("类型") << tr("AccessPoint")
			 << tr("IP") << tr("子网掩码") << tr("网关") << tr("端口");
		values << QString::fromUtf8(ied.name.c_str()) << QString::fromUtf8(ied.desc.c_str())
			<< QString::fromUtf8(ied.manufacturer.c_str()) << QString::fromUtf8(ied.type.c_str())
			<< QString::fromUtf8(ied.apName.c_str()) << QString::fromUtf8(ied.ip.c_str())
			<< QString::fromUtf8(ied.ipSubnet.c_str()) << QString::fromUtf8(ied.ipGateway.c_str())
			<< QString::number(ied.port);
		for (int i = 0; i < 9; ++i) editable << true;
	}
	else if (path == "history")
	{
		keys << tr("说明");
		values << tr("在 IED 节点右键“添加修订历史”，条目可在此编辑");
		editable << false;
	}
	else if (parts.size() == 2 && parts[0] == "hitem")
	{
		int hi = parts[1].toInt();
		if (hi >= 0 && hi < (int)m_model.history.size())
		{
			const SclHistoryItem& h = m_model.history[hi];
			keys << tr("版本") << tr("内容") << tr("执行人") << tr("时间") << tr("原因");
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
			keys << tr("名称") << tr("类型");
			values << QString::fromUtf8(m_model.ied.apName.c_str()) << tr("主接入点（含 Server）");
			editable << true << false;
		}
		else if (ai - 1 < (int)m_model.ied.extraAps.size())
		{
			const SclAccessPoint& ap = m_model.ied.extraAps[ai - 1];
			keys << tr("名称") << tr("ServerAt") << tr("类型");
			values << QString::fromUtf8(ap.name.c_str()) << QString::fromUtf8(ap.serverAt.c_str())
				<< (ap.serverAt.empty() ? tr("独立接入点") : tr("ServerAt 引用"));
			editable << true << true << false;
		}
	}
	else if (path == "server")
	{
		keys << tr("timeout") << tr("Authentication");
		values << (m_model.ied.serverTimeout >= 0 ? QString::number(m_model.ied.serverTimeout) : QString())
			<< (m_model.ied.authentication ? "true" : "false");
		editable << true << true;
	}
	else if (parts.size() == 2 && parts[0] == "ld")
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		if (ld)
		{
			keys << tr("Instance") << tr("LDName") << tr("描述") << tr("注释");
			values << QString::fromUtf8(ld->inst.c_str()) << QString::fromUtf8(ld->ldName.c_str())
				<< QString::fromUtf8(ld->desc.c_str()) << QString::fromUtf8(ld->comment.c_str());
			editable << true << true << true << true;
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
			keys << tr("Prefix") << tr("LnClass") << tr("Inst") << tr("描述") << tr("注释");
			values << QString::fromUtf8(ln->prefix.c_str()) << QString::fromUtf8(ln->lnClass.c_str())
				<< QString::fromUtf8(ln->inst.c_str()) << QString::fromUtf8(ln->desc.c_str())
				<< QString::fromUtf8(ln->comment.c_str());
			editable << true << false << !isLn0 << true << true;
		}
	}
	else if (parts.size() == 4 && parts[0] == "do")
	{
		SclDo* dobj = doAt(parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
		if (dobj)
		{
			keys << tr("名称") << tr("CDC") << tr("描述");
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
				bool canEdit = daiTypeEditable(spec.bType);
				keys << tr("DAI 路径") << tr("基本类型") << tr("FC") << tr("值");
				values << QString::fromUtf8(spec.path.c_str())
					<< QString::fromUtf8(spec.bType.c_str())
					<< QString::fromUtf8(spec.fc.c_str())
					<< (val ? QString::fromUtf8(val->c_str()) : QString());
				editable << false << false << false << canEdit;
				if (spec.bType == "Enum")
				{
					keys << tr("可选值");
					QStringList opts;
					std::vector<std::pair<int, std::string> > evals = enumValsFor(spec.enumId);
					for (size_t e = 0; e < evals.size(); ++e)
						opts << QString::fromUtf8(evals[e].second.c_str());
					values << (opts.isEmpty() ? tr("（未知枚举类型）") : opts.join(" / "));
					editable << false;
				}
			}
		}
	}

	else if (path == "comm")
	{
		keys << tr("说明");
		values << tr("右键添加子网；子网下添加接入点，接入点下添加 GSE/SMV 通信地址");
		editable << false;
	}
	else if (parts.size() == 2 && parts[0] == "subnet")
	{
		int si = parts[1].toInt();
		if (si >= 0 && si < (int)m_model.subnets.size())
		{
			keys << tr("名称") << tr("类型");
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
			keys << tr("名称") << tr("描述") << tr("FCDA 数");
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
			keys << tr("名称") << tr("RptID") << tr("数据集") << tr("confRev") << tr("intgPd(ms)") << tr("bufTime")
				 << tr("buffered") << tr("indexed") << tr("maxClients(-1 不输出)")
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
		}
		else if (ln && parts[0] == "gcb" && idx >= 0 && idx < (int)ln->gses.size())
		{
			const SclGseControl& gc = ln->gses[idx];
			keys << tr("名称") << tr("appID") << tr("数据集") << tr("confRev") << tr("fixedOffs");
			values << QString::fromUtf8(gc.name.c_str()) << QString::fromUtf8(gc.appId.c_str())
				<< QString::fromUtf8(gc.dataSet.c_str()) << QString::fromUtf8(gc.confRev.c_str())
				<< (gc.fixedOffs ? "true" : "false");
			for (int i = 0; i < keys.size(); ++i) editable << true;
		}
		else if (ln && parts[0] == "smcb" && idx >= 0 && idx < (int)ln->smvs.size())
		{
			const SclSampledValueControl& sc = ln->smvs[idx];
			keys << tr("名称") << tr("smvID") << tr("数据集") << tr("confRev")
				<< tr("smpRate") << tr("nofASDU") << tr("smpMod(S/P)");
			values << QString::fromUtf8(sc.name.c_str()) << QString::fromUtf8(sc.smvId.c_str())
				<< QString::fromUtf8(sc.dataSet.c_str()) << QString::fromUtf8(sc.confRev.c_str())
				<< QString::fromUtf8(sc.smpRate.c_str()) << QString::fromUtf8(sc.nofASDU.c_str())
				<< QString::fromUtf8(sc.smpMod.c_str());
			for (int i = 0; i < keys.size(); ++i) editable << true;
		}
		else if (ln && parts[0] == "logcb" && idx >= 0 && idx < (int)ln->logs.size())
		{
			const SclLogControl& lc = ln->logs[idx];
			keys << tr("名称") << tr("数据集") << tr("logName") << tr("intgPd(ms)")
				<< tr("TrgOps:dchg") << tr("TrgOps:qchg") << tr("TrgOps:dupd") << tr("TrgOps:period") << tr("TrgOps:gi");
			values << QString::fromUtf8(lc.name.c_str()) << QString::fromUtf8(lc.dataSet.c_str())
				<< QString::fromUtf8(lc.logName.c_str()) << QString::fromUtf8(lc.intgPd.c_str())
				<< (lc.trgOps.dchg ? "true" : "false") << (lc.trgOps.qchg ? "true" : "false")
				<< (lc.trgOps.dupd ? "true" : "false") << (lc.trgOps.period ? "true" : "false")
				<< (lc.trgOps.gi ? "true" : "false");
			for (int i = 0; i < keys.size(); ++i) editable << true;
		}
		else if (ln && parts[0] == "setcb" && idx >= 0 && idx < (int)ln->settings.size())
		{
			const SclSettingControl& sc = ln->settings[idx];
			keys << tr("名称") << tr("数据集") << tr("numOfSG");
			values << QString::fromUtf8(sc.name.c_str()) << QString::fromUtf8(sc.dataSet.c_str())
				<< QString::fromUtf8(sc.numOfSG.c_str());
			for (int i = 0; i < keys.size(); ++i) editable << true;
		}
	}
	else if (parts.size() == 3 && (parts[0] == "ds-sec" || parts[0] == "cb-sec"))
	{
		keys << tr("说明");
		values << tr("在逻辑节点右键添加");
		editable << false;
	}

	m_props->setRowCount(keys.size());
	for (int i = 0; i < keys.size(); ++i)
	{
		QTableWidgetItem* k = new QTableWidgetItem(keys[i]);
		k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		m_props->setItem(i, 0, k);
		QTableWidgetItem* v = new QTableWidgetItem(values[i]);
		if (i < editable.size() && editable[i])
			v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
		else
			v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		m_props->setItem(i, 1, v);
	}
	m_props->resizeColumnToContents(0);
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
				QMessageBox::warning(this, tr("校验失败"), tr("IED 名称长度 1-64 位，由字母开头，仅可包含字母数字下划线"));
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
				QMessageBox::warning(this, tr("校验失败"), tr("AccessPoint 名称长度 1-64 位，由字母开头，仅可包含字母数字下划线"));
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
				QMessageBox::warning(this, tr("校验失败"), tr("端口须为 1-65535 的整数"));
				populateProps(path);
				return;
			}
			ied.port = port;
			updatePParam(ied, "MMS-Port", v);
			break;
		}
		default: return;
		}
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
					QMessageBox::warning(this, tr("校验失败"), tr("timeout 须为非负整数或留空"));
					populateProps(path);
					return;
				}
				m_model.ied.serverTimeout = t;
			}
		}
		else if (row == 1)
		{
			QString vs = value.trimmed().toLower();
			if (vs == "true" || vs == "1" || vs == tr("是"))
				m_model.ied.authentication = true;
			else if (vs == "false" || vs == "0" || vs == tr("否"))
				m_model.ied.authentication = false;
			else
			{
				QMessageBox::warning(this, tr("校验失败"), tr("Authentication 请输入 true 或 false"));
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
				QMessageBox::warning(this, tr("校验失败"), tr("名称须合法且不重复"));
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
					QMessageBox::warning(this, tr("校验失败"), tr("名称须合法且不重复"));
					populateProps(path);
					return;
				}
				m_model.ied.extraAps[ei].name = v;
			}
			else if (row == 1)
			{
				if (!v.empty() && apNameExists(v, ei))
				{
					QMessageBox::warning(this, tr("校验失败"), tr("ServerAt 须指向存在的接入点名称"));
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
				QMessageBox::warning(this, tr("校验失败"), tr("Instance 需合法且在文件内唯一"));
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
				QMessageBox::warning(this, tr("校验失败"), tr("Prefix 仅可包含字母数字下划线"));
				populateProps(path);
				return;
			}
			ln->prefix = v;
		}
		else if (row == 2 && !isLn0)
		{
			if (!allDigits(v))
			{
				QMessageBox::warning(this, tr("校验失败"), tr("Inst 须为非空数字"));
				populateProps(path);
				return;
			}
			if (lnExistsInLd(*ld, ln->prefix, ln->lnClass, v, ni))
			{
				QMessageBox::warning(this, tr("校验失败"), tr("prefix 和 inst 组合后与现有 LN 重复，请修改"));
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
				QMessageBox::warning(this, tr("校验失败"), tr("DO 名称须为 1-64 位、字母开头、仅含字母数字下划线"));
				populateProps(path);
				return;
			}
			for (size_t i = 0; i < ln->dos.size(); ++i)
			{
				if (ln->dos[i].name == v)
				{
					QMessageBox::warning(this, tr("校验失败"), tr("DO 名称在该逻辑节点内须唯一"));
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
			QMessageBox::warning(this, tr("校验失败"),
				tr("值与类型 %1 不匹配%2").arg(QString::fromUtf8(spec.bType.c_str()))
					.arg(spec.bType == "Enum" ? tr("，可选值见属性页说明") : QString()));
			populateProps(path);
			return;
		}
		dobj->SetDaiVal(spec.path, normalized.toUtf8().constData());
	}
	else if (parts.size() == 2 && parts[0] == "subnet")
	{
		int si = parts[1].toInt();
		if (si < 0 || si >= (int)m_model.subnets.size()) return;
		if (row == 0)
		{
			if (value.trimmed().isEmpty())
			{
				QMessageBox::warning(this, tr("校验失败"), tr("子网名称不能为空"));
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
		else if (row >= 2 && row - 2 < (int)cap.pParams.size()) cap.pParams[row - 2].second = v;
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
				QMessageBox::warning(this, tr("校验失败"), tr("数据集名称须合法"));
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
				if (!validIdent(v)) { QMessageBox::warning(this, tr("校验失败"), tr("名称须合法")); populateProps(path); return; }
				rc.name = v; break;
			case 1: rc.rptId = v; break;
			case 2: rc.dataSet = v; break;
			case 3: rc.confRev = v; break;
			case 4: rc.intgPd = v; break;
			case 5: rc.bufTime = v; break;
			case 6: if (!parseBoolText(value, b)) { QMessageBox::warning(this, tr("校验失败"), tr("请输入 true 或 false")); populateProps(path); return; } rc.buffered = b; break;
			case 7: if (!parseBoolText(value, b)) { QMessageBox::warning(this, tr("校验失败"), tr("请输入 true 或 false")); populateProps(path); return; } rc.indexed = b; break;
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
				if (!validIdent(v)) { QMessageBox::warning(this, tr("校验失败"), tr("名称须合法")); populateProps(path); return; }
				gc.name = v; break;
			case 1: gc.appId = v; break;
			case 2: gc.dataSet = v; break;
			case 3: gc.confRev = v; break;
			case 4: { bool b = false; if (!parseBoolText(value, b)) { QMessageBox::warning(this, tr("校验失败"), tr("请输入 true 或 false")); populateProps(path); return; } gc.fixedOffs = b; break; }
			default: return;
			}
		}
		else if (parts[0] == "smcb" && idx >= 0 && idx < (int)ln->smvs.size())
		{
			SclSampledValueControl& sc = ln->smvs[idx];
			switch (row)
			{
			case 0:
				if (!validIdent(v)) { QMessageBox::warning(this, tr("校验失败"), tr("名称须合法")); populateProps(path); return; }
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
				if (!validIdent(v)) { QMessageBox::warning(this, tr("校验失败"), tr("名称须合法")); populateProps(path); return; }
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
				if (!validIdent(v)) { QMessageBox::warning(this, tr("校验失败"), tr("名称须合法")); populateProps(path); return; }
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

void MainWindow::onPropChanged(QTableWidgetItem* item)
{
	if (m_loadingProps || item == 0 || item->column() != 1)
		return;
	applyProp(m_currentPath, item->row(), item->text());
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
	log(tr("loader 试载：DI=%1 AI=%2 DO=%3 AO=%4 SG=%5 ied=%6")
		.arg((int)out.table.diRefs.size()).arg((int)out.table.aiRefs.size())
		.arg((int)out.table.doRefs.size()).arg((int)out.table.aoRefs.size())
		.arg((int)out.table.sgRefs.size())
		.arg(QString::fromUtf8(out.detectedIed.c_str())));
	if (out.sgQuirk)
		log(tr("注意：loader 校验报 SG contains an invalid point（loader 已知缺陷，ucType=2 被 ValidateRefs 拒绝），点表仍已解析"));
	else if (!out.loaderOk)
		log(tr("loader 校验失败：%1").arg(QString::fromUtf8(out.errorText.c_str())));
	else
		log(tr("loader 校验通过"));
}

void MainWindow::onNewModel()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("新建模型"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* iedEdit = new QLineEdit("TEMPLATE", &dlg);
	QLineEdit* apEdit = new QLineEdit("accessPoint1", &dlg);
	QLineEdit* ldEdit = new QLineEdit("LD1", &dlg);
	form->addRow(tr("IED 名称"), iedEdit);
	form->addRow(tr("AccessPoint"), apEdit);
	form->addRow(tr("LDevice inst"), ldEdit);
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
		QMessageBox::warning(this, tr("校验失败"),
			tr("IED 名称 / AccessPoint / LDevice inst 均须为 1-64 位、字母开头、仅含字母数字下划线"));
		return;
	}

	m_model = MakeNewModel();
	m_model.ied.name = iedEdit->text().toUtf8().constData();
	m_model.ied.apName = apEdit->text().toUtf8().constData();
	m_model.ied.lds[0].inst = ldEdit->text().toUtf8().constData();
	m_model.ied.lds[0].ldName = m_model.ied.lds[0].inst;
	m_currentFile.clear();
	buildTree();
	log(tr("新建模型：IED=%1（含完整通信参数与 Services 声明）").arg(iedEdit->text()));
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
	QAction* addRptAct = 0;
	QAction* addGcbAct = 0;
	QAction* addSmcbAct = 0;
	QAction* addLogcbAct = 0;
	QAction* addSetcbAct = 0;
	QAction* delCbAct = 0;

	if (path == "ied")
	{
		addHistAct = menu.addAction(tr("添加修订历史"));
		addApAct = menu.addAction(tr("添加接入点(AccessPoint)"));
		refAct = menu.addAction(tr("获取引用"));
		menu.addSeparator();
		expandAct = menu.addAction(tr("展开全部"));
		collapseAct = menu.addAction(tr("收起全部"));
	}
	else if (parts.size() == 2 && parts[0] == "hitem")
	{
		delHistAct = menu.addAction(tr("删除修订历史"));
	}
	else if (parts.size() == 2 && parts[0] == "ap")
	{
		refAct = menu.addAction(tr("获取引用"));
		if (parts[1].toInt() > 0)
			delApAct = menu.addAction(tr("删除接入点"));
	}
	else if (path == "server")
	{
		addLdAct = menu.addAction(tr("添加逻辑设备(LD)"));
	}
	else if (parts.size() == 2 && parts[0] == "ln0")
	{
		addDoAct = menu.addAction(tr("添加 DO"));
		addDsAct = menu.addAction(tr("添加数据集"));
		QMenu* cbMenu = menu.addMenu(tr("添加控制块"));
		addRptAct = cbMenu->addAction(tr("报告控制块(ReportControl)"));
		addGcbAct = cbMenu->addAction(tr("GOOSE 控制块(GSEControl)"));
		addSmcbAct = cbMenu->addAction(tr("采样值控制块(SampledValueControl)"));
		addLogcbAct = cbMenu->addAction(tr("日志控制块(LogControl)"));
		addSetcbAct = cbMenu->addAction(tr("定值控制块(SettingControl)"));
	}
	else if (parts.size() == 2 && parts[0] == "ld")
	{
		addLdAct = menu.addAction(tr("添加逻辑节点"));
		menu.addSeparator();
		copyLdAct = menu.addAction(tr("复制逻辑设备"));
		if (g_hasClipLd)
			pasteLdAct = menu.addAction(tr("粘贴逻辑设备"));
		delLdAct = menu.addAction(tr("删除逻辑设备"));
		menu.addSeparator();
		refAct = menu.addAction(tr("获取引用"));
	}
	else if (parts.size() == 3 && parts[0] == "ln")
	{
		addDoAct = menu.addAction(tr("添加 DO"));
		menu.addSeparator();
		copyLnAct = menu.addAction(tr("复制逻辑节点"));
		if (g_hasClipLn)
			pasteLnAct = menu.addAction(tr("粘贴逻辑节点"));
		delLnAct = menu.addAction(tr("删除逻辑节点"));
		menu.addSeparator();
		refAct = menu.addAction(tr("获取引用"));
	}
	else if (parts.size() == 4 && parts[0] == "do")
	{
		editDoAct = menu.addAction(tr("编辑 DO 名称"));
		delDoAct = menu.addAction(tr("删除 DO"));
		menu.addSeparator();
		refAct = menu.addAction(tr("获取引用"));
	}
	else if (parts.size() == 4 && parts[0] == "ds")
	{
		editDsAct = menu.addAction(tr("编辑 FCDA 成员"));
		delDsAct = menu.addAction(tr("删除数据集"));
	}
	else if (parts.size() == 4 && (parts[0] == "rpt" || parts[0] == "gcb" || parts[0] == "smcb" || parts[0] == "logcb" || parts[0] == "setcb"))
	{
		delCbAct = menu.addAction(tr("删除控制块"));
	}
	else if (path == "comm")
	{
		addSubAct = menu.addAction(tr("添加子网(SubNetwork)"));
	}
	else if (parts.size() == 2 && parts[0] == "subnet")
	{
		addCapAct = menu.addAction(tr("添加接入点(ConnectedAP)"));
		delSubAct = menu.addAction(tr("删除子网"));
	}
	else if (parts.size() == 3 && parts[0] == "cap")
	{
		addGseAddrAct = menu.addAction(tr("添加 GSE 地址"));
		addSmvAddrAct = menu.addAction(tr("添加 SMV 地址"));
		delCapAct = menu.addAction(tr("删除接入点"));
	}
	else if (parts.size() == 4 && (parts[0] == "gse" || parts[0] == "smv"))
	{
		delAddrAct = menu.addAction(tr("删除通信地址"));
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
		log(tr("删除逻辑节点 %1").arg(name));
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
		dlg.setWindowTitle(tr("添加逻辑节点"));
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
				QMessageBox::warning(&dlg, tr("校验失败"), tr("Inst 须为非空数字"));
				continue;
			}
			if (!prefix.empty() && !validIdent(prefix))
			{
				QMessageBox::warning(&dlg, tr("校验失败"), tr("Prefix 仅可包含字母数字下划线"));
				continue;
			}
			if (lnExistsInLd(*ld, prefix, lnClass, inst, -1))
			{
				QMessageBox::warning(&dlg, tr("校验失败"), tr("prefix 和 inst 组合后与模型文件中现有 LN 重复，请修改"));
				continue;
			}
			SclLn ln = SclTemplates::MakeLn(lnClass);
			ln.inst = inst;
			ln.prefix = prefix;
			ld->lns.push_back(ln);
			buildTree();
			log(tr("添加逻辑节点 %1%2 到 %3").arg(QString::fromUtf8(lnClass.c_str())).arg(instEdit->text()).arg(QString::fromUtf8(ld->inst.c_str())));
			return;
		}
	}
}

void MainWindow::addHistoryItem()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加修订历史"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* versionEdit = new QLineEdit("1.0", &dlg);
	QLineEdit* whatEdit = new QLineEdit(&dlg);
	QLineEdit* whoEdit = new QLineEdit(&dlg);
	QLineEdit* whenEdit = new QLineEdit(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), &dlg);
	QLineEdit* whyEdit = new QLineEdit(&dlg);
	form->addRow(tr("版本"), versionEdit);
	form->addRow(tr("内容"), whatEdit);
	form->addRow(tr("执行人"), whoEdit);
	form->addRow(tr("时间"), whenEdit);
	form->addRow(tr("原因"), whyEdit);
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
	log(tr("添加修订历史：%1").arg(QString::fromUtf8(item.what.c_str())));
}

void MainWindow::deleteHistoryItem(int idx)
{
	if (idx < 0 || idx >= (int)m_model.history.size())
		return;
	if (QMessageBox::question(this, tr("确认"), tr("确认删除该修订历史？")) != QMessageBox::Yes)
		return;
	m_model.history.erase(m_model.history.begin() + idx);
	buildTree();
	log(tr("删除修订历史 H%1").arg(idx + 1));
}

void MainWindow::addAccessPoint()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加接入点(AccessPoint)"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit("accessPoint2", &dlg);
	QComboBox* serverAtCombo = new QComboBox(&dlg);
	serverAtCombo->addItem(tr("无（独立接入点）"), QString());
	serverAtCombo->addItem(tr("主接入点：%1").arg(QString::fromUtf8(m_model.ied.apName.c_str())),
		QString::fromUtf8(m_model.ied.apName.c_str()));
	for (size_t i = 0; i < m_model.ied.extraAps.size(); ++i)
		serverAtCombo->addItem(QString::fromUtf8(m_model.ied.extraAps[i].name.c_str()),
			QString::fromUtf8(m_model.ied.extraAps[i].name.c_str()));
	form->addRow(tr("名称"), nameEdit);
	form->addRow(tr("ServerAt 指向"), serverAtCombo);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;

	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name) || apNameExists(name, -1))
	{
		QMessageBox::warning(this, tr("校验失败"), tr("名称须合法且不重复"));
		return;
	}
	SclAccessPoint ap;
	ap.name = name;
	ap.serverAt = serverAtCombo->currentData().toString().toUtf8().constData();
	m_model.ied.extraAps.push_back(ap);
	buildTree();
	log(tr("添加接入点 %1%2").arg(QString::fromUtf8(name.c_str()))
		.arg(ap.serverAt.empty() ? tr("（独立）") : tr("（ServerAt -> %1）").arg(QString::fromUtf8(ap.serverAt.c_str()))));
}

void MainWindow::deleteAccessPoint(int extraIdx)
{
	if (extraIdx < 0 || extraIdx >= (int)m_model.ied.extraAps.size())
		return;
	if (QMessageBox::question(this, tr("确认"),
		tr("确认删除接入点 %1？").arg(QString::fromUtf8(m_model.ied.extraAps[extraIdx].name.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(m_model.ied.extraAps[extraIdx].name.c_str());
	m_model.ied.extraAps.erase(m_model.ied.extraAps.begin() + extraIdx);
	buildTree();
	log(tr("删除接入点 %1").arg(name));
}

void MainWindow::addLDevice()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加逻辑设备(LD)"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* instEdit = new QLineEdit(QString("LD%1").arg((int)m_model.ied.lds.size() + 1), &dlg);
	QLineEdit* descEdit = new QLineEdit(&dlg);
	form->addRow(tr("Instance"), instEdit);
	form->addRow(tr("描述"), descEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;

	std::string inst = instEdit->text().toUtf8().constData();
	if (!validIdent(inst) || ldInstExists(inst, -1))
	{
		QMessageBox::warning(this, tr("校验失败"), tr("Instance 需合法且在文件内唯一"));
		return;
	}
	SclLDevice ld;
	ld.inst = inst;
	ld.ldName = inst;
	ld.desc = descEdit->text().toUtf8().constData();
	ld.ln0 = SclTemplates::MakeLn("LLN0");
	m_model.ied.lds.push_back(ld);
	buildTree();
	log(tr("添加逻辑设备 %1").arg(QString::fromUtf8(inst.c_str())));
}

void MainWindow::deleteLDevice(int li)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0)
		return;
	if (QMessageBox::question(this, tr("确认"),
		tr("确认删除逻辑设备 %1 及其下全部逻辑节点？").arg(QString::fromUtf8(ld->inst.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(ld->inst.c_str());
	m_model.ied.lds.erase(m_model.ied.lds.begin() + li);
	buildTree();
	log(tr("删除逻辑设备 %1").arg(name));
}

void MainWindow::copyLDevice(int li)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0)
		return;
	g_clipLd = *ld;
	g_hasClipLd = true;
	log(tr("已复制逻辑设备 %1，可在 Server 节点粘贴").arg(QString::fromUtf8(ld->inst.c_str())));
}

void MainWindow::pasteLDevice()
{
	if (!g_hasClipLd)
		return;
	SclLDevice ld = g_clipLd;
	std::string base = ld.inst;
	int n = 1;
	while (ldInstExists(ld.inst, -1))
		ld.inst = base + "_" + std::to_string(n++);
	ld.ldName = ld.inst;
	m_model.ied.lds.push_back(ld);
	buildTree();
	log(tr("粘贴逻辑设备 %1（源：%2）").arg(QString::fromUtf8(ld.inst.c_str())).arg(QString::fromUtf8(base.c_str())));
}

void MainWindow::copyLogicNode(int li, int ni)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0 || ni < 0 || ni >= (int)ld->lns.size())
		return;
	g_clipLn = ld->lns[ni];
	g_hasClipLn = true;
	log(tr("已复制逻辑节点 %1，可在 LDevice 节点粘贴").arg(QString::fromUtf8(g_clipLn.Name().c_str())));
}

void MainWindow::pasteLogicNode(int li)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0 || !g_hasClipLn)
		return;
	SclLn ln = g_clipLn;
	std::string base = ln.inst;
	int n = 1;
	while (lnExistsInLd(*ld, ln.prefix, ln.lnClass, ln.inst, -1))
		ln.inst = base + "_" + std::to_string(n++);
	ld->lns.push_back(ln);
	buildTree();
	log(tr("粘贴逻辑节点 %1 到 %2").arg(QString::fromUtf8(ln.Name().c_str())).arg(QString::fromUtf8(ld->inst.c_str())));
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
	dlg.setWindowTitle(tr("引用信息"));
	dlg.resize(620, 180);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);
	QFormLayout* form = new QFormLayout;
	QLineEdit* iecEdit = new QLineEdit(QString::fromUtf8(iecRef.c_str()), &dlg);
	QLineEdit* mmsEdit = new QLineEdit(QString::fromUtf8(mmsRef.c_str()), &dlg);
	iecEdit->setReadOnly(true);
	mmsEdit->setReadOnly(true);
	form->addRow(tr("IEC61850 引用"), iecEdit);
	form->addRow(tr("MMS 引用"), mmsEdit);
	vbox->addLayout(form);
	QHBoxLayout* btns = new QHBoxLayout;
	QPushButton* copyIec = new QPushButton(tr("复制 IEC61850 引用"), &dlg);
	QPushButton* copyMms = new QPushButton(tr("复制 MMS 引用"), &dlg);
	btns->addWidget(copyIec);
	btns->addWidget(copyMms);
	btns->addStretch(1);
	vbox->addLayout(btns);
	connect(copyIec, &QPushButton::clicked, this, [this, iecEdit]() {
		QApplication::clipboard()->setText(iecEdit->text());
		log(tr("已复制 IEC61850 引用到剪贴板"));
	});
	connect(copyMms, &QPushButton::clicked, this, [this, mmsEdit]() {
		QApplication::clipboard()->setText(mmsEdit->text());
		log(tr("已复制 MMS 引用到剪贴板"));
	});
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, &dlg);
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	vbox->addWidget(box);
	dlg.exec();
}

void MainWindow::onXmlPreview()
{
	std::string text = SclWriter::BuildSclText(m_model, true);
	QDialog dlg(this);
	dlg.setWindowTitle(tr("XML 源码预览"));
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
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	vbox->addWidget(box);
	dlg.exec();
}

void MainWindow::onSaveCidAs()
{
	QString path = QFileDialog::getSaveFileName(this, tr("保存CID"),
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
	std::string err;
	std::string path = m_currentFile.toUtf8().constData();
	if (!SclWriter::WriteCid(m_model, path, err))
	{
		QMessageBox::warning(this, tr("保存失败"), QString::fromUtf8(err.c_str()));
		return;
	}
	log(tr("已保存 CID：%1").arg(m_currentFile));
	validateAndLog(SclWriter::BuildSclText(m_model, true));
}

void MainWindow::onExportIcd()
{
	QString path = QFileDialog::getSaveFileName(this, tr("导出ICD文件"),
		QDir::currentPath() + "/model.icd", tr("ICD (*.icd)"));
	if (path.isEmpty())
		return;
	std::string err;
	if (!SclWriter::WriteIcd(m_model, path.toUtf8().constData(), err))
	{
		QMessageBox::warning(this, tr("导出失败"), QString::fromUtf8(err.c_str()));
		return;
	}
	log(tr("已导出 ICD：%1").arg(path));
	validateAndLog(SclWriter::BuildSclText(m_model, false));
}

void MainWindow::onExportCid()
{
	QString path = QFileDialog::getSaveFileName(this, tr("导出CID文件"),
		QDir::currentPath() + "/model.cid", tr("CID (*.cid)"));
	if (path.isEmpty())
		return;
	std::string err;
	if (!SclWriter::WriteCid(m_model, path.toUtf8().constData(), err))
	{
		QMessageBox::warning(this, tr("导出失败"), QString::fromUtf8(err.c_str()));
		return;
	}
	log(tr("已导出 CID：%1").arg(path));
	validateAndLog(SclWriter::BuildSclText(m_model, true));
}

void MainWindow::onExportCsv()
{
	QString path = QFileDialog::getSaveFileName(this, tr("导出CSV点表"),
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
		QMessageBox::warning(this, tr("导出失败"),
			tr("loader 未能解析出任何点：%1").arg(QString::fromUtf8(out.errorText.c_str())));
		return;
	}
	std::string err;
	if (!CsvWriter::WriteCsv(m_model, out.table, path.toUtf8().constData(), err))
	{
		QMessageBox::warning(this, tr("导出失败"), QString::fromUtf8(err.c_str()));
		return;
	}
	log(tr("已导出 CSV：%1（DI=%2 AI=%3 DO=%4 AO=%5 SG=%6）").arg(path)
		.arg((int)out.table.diRefs.size()).arg((int)out.table.aiRefs.size())
		.arg((int)out.table.doRefs.size()).arg((int)out.table.aoRefs.size())
		.arg((int)out.table.sgRefs.size()));
}

void MainWindow::onOpenScl()
{
	QString path = QFileDialog::getOpenFileName(this, tr("打开SCL模型"),
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
		QMessageBox::warning(this, tr("打开失败"), tr("文件不存在：%1").arg(path));
		return;
	}
	setBusy(true);
	log(tr("正在打开 %1 …").arg(path));

	QFutureWatcher<SclIedListResult>* listWatcher = new QFutureWatcher<SclIedListResult>(this);
	connect(listWatcher, &QFutureWatcher<SclIedListResult>::finished, this,
		[this, listWatcher, path]() {
		SclIedListResult r = listWatcher->result();
		listWatcher->deleteLater();
		if (r.ieds.empty())
		{
			setBusy(false);
			QMessageBox::warning(this, tr("打开失败"), QString::fromUtf8(r.err.c_str()));
			return;
		}
		QString pick;
		if (r.ieds.size() > 1)
		{
			QStringList items;
			for (size_t i = 0; i < r.ieds.size(); ++i)
				items << QString::fromUtf8(r.ieds[i].c_str());
			pick = QInputDialog::getItem(this, tr("选择IED"),
				tr("文件含多个IED，选择要打开的："), items, 0, false);
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
				QMessageBox::warning(this, tr("打开失败"), QString::fromUtf8(r2.err.c_str()));
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
				for (size_t ni = 0; ni < ld.lns.size(); ++ni)
					doCount += (int)ld.lns[ni].dos.size();
			}
			log(tr("已打开 %1：IED=%2 LD=%3 LN=%4 DO=%5").arg(path)
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
	QString path = QFileDialog::getOpenFileName(this, tr("导入在线JSON"),
		QDir::currentPath(), tr("JSON (*.json);;All files (*.*)"));
	if (path.isEmpty())
		return;
	std::string err;
	SclModel loaded;
	if (!JsonModel::ReadOnlineJsonFile(path.toUtf8().constData(), "",
		m_model.ied.ip, m_model.ied.port, loaded, err))
	{
		QMessageBox::warning(this, tr("导入失败"), QString::fromUtf8(err.c_str()));
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
		for (size_t ni = 0; ni < ld.lns.size(); ++ni)
			doCount += (int)ld.lns[ni].dos.size();
	}
	log(tr("已导入在线JSON %1：IED=%2 LD=%3 LN=%4 DO=%5").arg(path)
		.arg(QString::fromUtf8(m_model.ied.name.c_str()))
		.arg((int)m_model.ied.lds.size()).arg(lnCount).arg(doCount));
}

void MainWindow::onOnlineRead()
{
	if (m_browseThread != 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("在线读取IED"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* ipEdit = new QLineEdit(m_model.ied.ip.c_str(), &dlg);
	QLineEdit* portEdit = new QLineEdit(QString::number(m_model.ied.port), &dlg);
	QLineEdit* iedEdit = new QLineEdit("simpleIO", &dlg);
	form->addRow(tr("IP"), ipEdit);
	form->addRow(tr("端口"), portEdit);
	form->addRow(tr("IED名(可空)"), iedEdit);
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
	log(tr("开始在线读取 %1:%2 …").arg(ipEdit->text()).arg(portEdit->text()));
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
		log(tr("在线读取失败：%1").arg(t->m_error));
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
		for (size_t ni = 0; ni < ld.lns.size(); ++ni)
			doCount += (int)ld.lns[ni].dos.size();
	}
	log(tr("在线读取完成：IED=%1 LD=%2 LN=%3 DO=%4").arg(QString::fromUtf8(m_model.ied.name.c_str()))
		.arg((int)m_model.ied.lds.size()).arg(lnCount).arg(doCount));
	t->deleteLater();
}

// ---------------- 第二期第二批：数据集/控制块/通信段 ----------------

bool MainWindow::parseBoolText(const QString& text, bool& out) const
{
	QString vs = text.trimmed().toLower();
	if (vs == "true" || vs == "1" || vs == tr("是")) { out = true; return true; }
	if (vs == "false" || vs == "0" || vs == tr("否")) { out = false; return true; }
	return false;
}

void MainWindow::addSubNet()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加子网(SubNetwork)"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("SubNetwork%1").arg((int)m_model.subnets.size() + 1), &dlg);
	QComboBox* typeCombo = new QComboBox(&dlg);
	typeCombo->addItem("8-MMS");
	typeCombo->addItem("4-StationBus");
	typeCombo->addItem("10-UDP");
	form->addRow(tr("名称"), nameEdit);
	form->addRow(tr("类型"), typeCombo);
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
	log(tr("添加子网 %1").arg(nameEdit->text()));
}

void MainWindow::deleteSubNet(int idx)
{
	if (idx < 0 || idx >= (int)m_model.subnets.size())
		return;
	if (QMessageBox::question(this, tr("确认"),
		tr("确认删除子网 %1 及其下全部接入点？").arg(QString::fromUtf8(m_model.subnets[idx].name.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(m_model.subnets[idx].name.c_str());
	m_model.subnets.erase(m_model.subnets.begin() + idx);
	buildTree();
	log(tr("删除子网 %1").arg(name));
}

void MainWindow::addConnectedAP(int subnetIdx)
{
	if (subnetIdx < 0 || subnetIdx >= (int)m_model.subnets.size())
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加接入点(ConnectedAP)"));
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
	log(tr("添加接入点 %1 到子网 %2").arg(apEdit->text()).arg(QString::fromUtf8(m_model.subnets[subnetIdx].name.c_str())));
}

void MainWindow::deleteConnectedAP(int subnetIdx, int apIdx)
{
	if (subnetIdx < 0 || subnetIdx >= (int)m_model.subnets.size())
		return;
	if (apIdx < 0 || apIdx >= (int)m_model.subnets[subnetIdx].aps.size())
		return;
	if (QMessageBox::question(this, tr("确认"), tr("确认删除该接入点？")) != QMessageBox::Yes)
		return;
	m_model.subnets[subnetIdx].aps.erase(m_model.subnets[subnetIdx].aps.begin() + apIdx);
	buildTree();
	log(tr("删除接入点"));
}

void MainWindow::addCommAddress(int subnetIdx, int apIdx, bool isGse)
{
	if (subnetIdx < 0 || subnetIdx >= (int)m_model.subnets.size())
		return;
	if (apIdx < 0 || apIdx >= (int)m_model.subnets[subnetIdx].aps.size())
		return;
	SclConnectedAP& cap = m_model.subnets[subnetIdx].aps[apIdx];
	QDialog dlg(this);
	dlg.setWindowTitle(isGse ? tr("添加 GSE 地址") : tr("添加 SMV 地址"));
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
	log(tr("添加%1通信地址 %2/%3").arg(isGse ? tr("GSE") : tr("SMV"))
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
	if (QMessageBox::question(this, tr("确认"), tr("确认删除该通信地址？")) != QMessageBox::Yes)
		return;
	list.erase(list.begin() + addrIdx);
	buildTree();
	log(tr("删除通信地址"));
}

void MainWindow::addDataSet(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加数据集"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("ds%1").arg((int)ln->dataSets.size() + 1), &dlg);
	QLineEdit* descEdit = new QLineEdit(&dlg);
	form->addRow(tr("名称"), nameEdit);
	form->addRow(tr("描述"), descEdit);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	form->addRow(box);
	connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	if (dlg.exec() != QDialog::Accepted)
		return;
	std::string name = nameEdit->text().toUtf8().constData();
	if (!validIdent(name))
	{
		QMessageBox::warning(this, tr("校验失败"), tr("数据集名称须合法"));
		return;
	}
	for (size_t i = 0; i < ln->dataSets.size(); ++i)
	{
		if (ln->dataSets[i].name == name)
		{
			QMessageBox::warning(this, tr("校验失败"), tr("数据集名称在该逻辑节点内须唯一"));
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
	log(tr("添加数据集 %1（%2 个 FCDA）").arg(nameEdit->text()).arg((int)ds.fcdas.size()));
}

bool MainWindow::editDataSetDialog(SclDataSet& ds, int li)
{
	SclLDevice* ld = ldAt(li);
	if (ld == 0)
		return false;

	struct TreeRef { int li; int ni; int di; };
	std::vector<TreeRef> refs;

	QDialog dlg(this);
	dlg.setWindowTitle(tr("编辑数据集：%1").arg(QString::fromUtf8(ds.name.c_str())));
	dlg.resize(880, 560);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);
	vbox->addWidget(new QLabel(tr("勾选 DO 生成 FCDA 成员（顺序与树一致）；取消勾选即移除。"), &dlg));
	QHBoxLayout* mid = new QHBoxLayout;
	QTreeWidget* srcTree = new QTreeWidget(&dlg);
	srcTree->setHeaderLabels(QStringList() << tr("源模型") << tr("CDC"));
	mid->addWidget(srcTree, 3);
	QListWidget* fcdaList = new QListWidget(&dlg);
	fcdaList->setSelectionMode(QAbstractItemView::NoSelection);
	mid->addWidget(fcdaList, 2);
	vbox->addLayout(mid);
	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	vbox->addWidget(box);

	// 构建源树：LD -> LN -> DO(可勾选)
	int refIdx = 0;
	for (size_t l = 0; l < m_model.ied.lds.size(); ++l)
	{
		const SclLDevice& mld = m_model.ied.lds[l];
		QTreeWidgetItem* ldItem = new QTreeWidgetItem(srcTree, QStringList(QString::fromUtf8(mld.inst.c_str())));
		auto addLnNode = [&](const SclLn& mln, int ni) {
			QTreeWidgetItem* lnItem = new QTreeWidgetItem(ldItem, QStringList(QString::fromUtf8(mln.Name().c_str())));
			for (size_t d = 0; d < mln.dos.size(); ++d)
			{
				QTreeWidgetItem* doItem = new QTreeWidgetItem(lnItem,
					QStringList() << QString::fromUtf8(mln.dos[d].name.c_str()) << QString::fromUtf8(mln.dos[d].cdc.c_str()));
				doItem->setCheckState(0, Qt::Unchecked);
				TreeRef ref;
				ref.li = (int)l;
				ref.ni = ni;
				ref.di = (int)d;
				refs.push_back(ref);
			}
		};
		addLnNode(mld.ln0, -1);
		for (size_t n = 0; n < mld.lns.size(); ++n)
			addLnNode(mld.lns[n], (int)n);
	}

	// 按 FCDA 回显勾选状态
	for (size_t k = 0; k < ds.fcdas.size(); ++k)
	{
		const SclFcda& f = ds.fcdas[k];
		for (size_t r = 0; r < refs.size(); ++r)
		{
			SclLDevice* mld = ldAt(refs[r].li);
			SclLn* mln = lnAt(refs[r].li, refs[r].ni);
			SclDo* mdo = doAt(refs[r].li, refs[r].ni, refs[r].di);
			if (mld == 0 || mln == 0 || mdo == 0)
				continue;
			if (mld->inst == f.ldInst && mln->Name() == (f.prefix + f.lnClass + f.lnInst) && mdo->name == f.doName)
			{
				QTreeWidgetItem* top = srcTree->topLevelItem(0);
				for (int t = 0; t < srcTree->topLevelItemCount(); ++t)
				{
					if (srcTree->topLevelItem(t)->text(0) == QString::fromUtf8(mld->inst.c_str()))
						top = srcTree->topLevelItem(t);
				}
				// 逐层定位（按 refs[r] 的结构遍历太繁琐，直接按序号还原）
				break;
			}
		}
	}

	// 简化回显：按 refs 顺序与 ds.fcdas 匹配（由 addFcdaRefs 一致性保证）
	for (size_t r = 0; r < refs.size(); ++r)
	{
		SclLDevice* mld = ldAt(refs[r].li);
		SclLn* mln = lnAt(refs[r].li, refs[r].ni);
		SclDo* mdo = doAt(refs[r].li, refs[r].ni, refs[r].di);
		if (mld == 0 || mln == 0 || mdo == 0)
			continue;
		bool checked = false;
		for (size_t k = 0; k < ds.fcdas.size(); ++k)
		{
			const SclFcda& f = ds.fcdas[k];
			if (mld->inst == f.ldInst && mln->Name() == (f.prefix + f.lnClass + f.lnInst) && mdo->name == f.doName)
			{
				checked = true;
				break;
			}
		}
		if (checked)
		{
			QTreeWidgetItem* ldItem = srcTree->topLevelItem(refs[r].li);
			if (ldItem == 0) continue;
			QTreeWidgetItem* lnItem = (refs[r].ni < 0) ? ldItem->child(0) : ldItem->child(refs[r].ni + 1);
			if (lnItem == 0) continue;
			QTreeWidgetItem* doItem = lnItem->child(refs[r].di);
			if (doItem)
				doItem->setCheckState(0, Qt::Checked);
		}
	}
	Q_UNUSED(refIdx);

	if (dlg.exec() != QDialog::Accepted)
		return false;

	// 按树顺序收集勾选的 DO
	std::vector<SclFcda> newFcdas;
	for (size_t r = 0; r < refs.size(); ++r)
	{
		QTreeWidgetItem* ldItem = srcTree->topLevelItem(refs[r].li);
		if (ldItem == 0) continue;
		QTreeWidgetItem* lnItem = (refs[r].ni < 0) ? ldItem->child(0) : ldItem->child(refs[r].ni + 1);
		if (lnItem == 0) continue;
		QTreeWidgetItem* doItem = lnItem->child(refs[r].di);
		if (doItem == 0 || doItem->checkState(0) != Qt::Checked)
			continue;
		SclLDevice* mld = ldAt(refs[r].li);
		SclLn* mln = lnAt(refs[r].li, refs[r].ni);
		SclDo* mdo = doAt(refs[r].li, refs[r].ni, refs[r].di);
		if (mld == 0 || mln == 0 || mdo == 0)
			continue;
		SclFcda f;
		f.iedName = m_model.ied.name;
		f.ldInst = mld->inst;
		f.prefix = mln->prefix;
		f.lnClass = mln->lnClass;
		f.lnInst = mln->inst;
		f.doName = mdo->name;
		f.fc = cdcToDefaultFc(mdo->cdc);
		newFcdas.push_back(f);
	}
	ds.fcdas = newFcdas;
	log(tr("数据集 %1 更新为 %2 个 FCDA").arg(QString::fromUtf8(ds.name.c_str())).arg((int)ds.fcdas.size()));
	return true;
}

void MainWindow::deleteDataSet(int li, int ni, int di)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0 || di < 0 || di >= (int)ln->dataSets.size())
		return;
	if (QMessageBox::question(this, tr("确认"),
		tr("确认删除数据集 %1？").arg(QString::fromUtf8(ln->dataSets[di].name.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(ln->dataSets[di].name.c_str());
	ln->dataSets.erase(ln->dataSets.begin() + di);
	buildTree();
	log(tr("删除数据集 %1").arg(name));
}

SclCommAddress* MainWindow::findGseAddress(const std::string& ldInst, const std::string& cbName)
{
	for (size_t s = 0; s < m_model.subnets.size(); ++s)
		for (size_t c = 0; c < m_model.subnets[s].aps.size(); ++c)
			for (size_t g = 0; g < m_model.subnets[s].aps[c].gses.size(); ++g)
			{
				SclCommAddress& a = m_model.subnets[s].aps[c].gses[g];
				if (a.ldInst == ldInst && a.cbName == cbName)
					return &a;
			}
	return 0;
}

SclCommAddress* MainWindow::findSmvAddress(const std::string& ldInst, const std::string& cbName)
{
	for (size_t s = 0; s < m_model.subnets.size(); ++s)
		for (size_t c = 0; c < m_model.subnets[s].aps.size(); ++c)
			for (size_t g = 0; g < m_model.subnets[s].aps[c].smvs.size(); ++g)
			{
				SclCommAddress& a = m_model.subnets[s].aps[c].smvs[g];
				if (a.ldInst == ldInst && a.cbName == cbName)
					return &a;
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
		QMessageBox::warning(this, tr("提示"), tr("请先在该逻辑节点下添加数据集"));
		return;
	}
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加报告控制块"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("brcb%1").arg((int)ln->reports.size() + 1), &dlg);
	QComboBox* dsCombo = new QComboBox(&dlg);
	for (size_t i = 0; i < ln->dataSets.size(); ++i)
		dsCombo->addItem(QString::fromUtf8(ln->dataSets[i].name.c_str()));
	QComboBox* bufferedCombo = new QComboBox(&dlg);
	bufferedCombo->addItem("BRCB（缓冲）");
	bufferedCombo->addItem("URCB（非缓冲）");
	QLineEdit* rptIdEdit = new QLineEdit(m_model.ied.name.c_str(), &dlg);
	form->addRow(tr("名称"), nameEdit);
	form->addRow(tr("数据集"), dsCombo);
	form->addRow(tr("类型"), bufferedCombo);
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
		QMessageBox::warning(this, tr("校验失败"), tr("名称须合法"));
		return;
	}
	for (size_t i = 0; i < ln->reports.size(); ++i)
	{
		if (ln->reports[i].name == name)
		{
			QMessageBox::warning(this, tr("校验失败"), tr("控制块名称须唯一"));
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
	log(tr("添加报告控制块 %1（%2，数据集 %3）").arg(QString::fromUtf8(name.c_str()))
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
		QMessageBox::warning(this, tr("提示"), tr("请先在该逻辑节点下添加数据集"));
		return;
	}
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加 GOOSE 控制块"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("gcb%1").arg((int)ln->gses.size() + 1), &dlg);
	QComboBox* dsCombo = new QComboBox(&dlg);
	for (size_t i = 0; i < ln->dataSets.size(); ++i)
		dsCombo->addItem(QString::fromUtf8(ln->dataSets[i].name.c_str()));
	QLineEdit* appIdEdit = new QLineEdit(QString("%1_%2_gcb").arg(QString::fromUtf8(m_model.ied.name.c_str()))
		.arg(QString::fromUtf8(ld->inst.c_str())), &dlg);
	form->addRow(tr("名称"), nameEdit);
	form->addRow(tr("数据集"), dsCombo);
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
		QMessageBox::warning(this, tr("校验失败"), tr("名称须合法"));
		return;
	}
	for (size_t i = 0; i < ln->gses.size(); ++i)
	{
		if (ln->gses[i].name == name)
		{
			QMessageBox::warning(this, tr("校验失败"), tr("控制块名称须唯一"));
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
	log(tr("添加 GOOSE 控制块 %1（数据集 %2），GSE 地址已自动创建，可在通信节点调整")
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
		QMessageBox::warning(this, tr("提示"), tr("请先在该逻辑节点下添加数据集"));
		return;
	}
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加采样值控制块"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("msvcb%1").arg((int)ln->smvs.size() + 1), &dlg);
	QComboBox* dsCombo = new QComboBox(&dlg);
	for (size_t i = 0; i < ln->dataSets.size(); ++i)
		dsCombo->addItem(QString::fromUtf8(ln->dataSets[i].name.c_str()));
	QLineEdit* smvIdEdit = new QLineEdit(QString("%1_%2_msvcb").arg(QString::fromUtf8(m_model.ied.name.c_str()))
		.arg(QString::fromUtf8(ld->inst.c_str())), &dlg);
	QLineEdit* rateEdit = new QLineEdit("80", &dlg);
	form->addRow(tr("名称"), nameEdit);
	form->addRow(tr("数据集"), dsCombo);
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
		QMessageBox::warning(this, tr("校验失败"), tr("名称须合法"));
		return;
	}
	for (size_t i = 0; i < ln->smvs.size(); ++i)
	{
		if (ln->smvs[i].name == name)
		{
			QMessageBox::warning(this, tr("校验失败"), tr("控制块名称须唯一"));
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
	log(tr("添加采样值控制块 %1，SMV 地址已自动创建").arg(QString::fromUtf8(name.c_str())));
}

void MainWindow::addLogControl(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加日志控制块"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("lc%1").arg((int)ln->logs.size() + 1), &dlg);
	QLineEdit* logNameEdit = new QLineEdit(&dlg);
	form->addRow(tr("名称"), nameEdit);
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
		QMessageBox::warning(this, tr("校验失败"), tr("名称须合法"));
		return;
	}
	SclLogControl lc;
	lc.name = name;
	lc.logName = logNameEdit->text().toUtf8().constData();
	lc.trgOps.present = true;
	ln->logs.push_back(lc);
	buildTree();
	log(tr("添加日志控制块 %1").arg(QString::fromUtf8(name.c_str())));
}

void MainWindow::addSettingControl(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加定值控制块"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString("scb%1").arg((int)ln->settings.size() + 1), &dlg);
	QLineEdit* sgEdit = new QLineEdit("2", &dlg);
	form->addRow(tr("名称"), nameEdit);
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
		QMessageBox::warning(this, tr("校验失败"), tr("名称须合法"));
		return;
	}
	SclSettingControl sc;
	sc.name = name;
	sc.numOfSG = sgEdit->text().toUtf8().constData();
	ln->settings.push_back(sc);
	buildTree();
	log(tr("添加定值控制块 %1").arg(QString::fromUtf8(name.c_str())));
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
		if (QMessageBox::question(this, tr("确认"), tr("确认删除报告控制块 %1？").arg(name)) != QMessageBox::Yes) return;
		ln->reports.erase(ln->reports.begin() + idx);
	}
	else if (kind == "gcb")
	{
		if (idx < 0 || idx >= (int)ln->gses.size()) return;
		name = QString::fromUtf8(ln->gses[idx].name.c_str());
		if (QMessageBox::question(this, tr("确认"), tr("确认删除 GOOSE 控制块 %1？").arg(name)) != QMessageBox::Yes) return;
		ln->gses.erase(ln->gses.begin() + idx);
	}
	else if (kind == "smcb")
	{
		if (idx < 0 || idx >= (int)ln->smvs.size()) return;
		name = QString::fromUtf8(ln->smvs[idx].name.c_str());
		if (QMessageBox::question(this, tr("确认"), tr("确认删除采样值控制块 %1？").arg(name)) != QMessageBox::Yes) return;
		ln->smvs.erase(ln->smvs.begin() + idx);
	}
	else if (kind == "logcb")
	{
		if (idx < 0 || idx >= (int)ln->logs.size()) return;
		name = QString::fromUtf8(ln->logs[idx].name.c_str());
		if (QMessageBox::question(this, tr("确认"), tr("确认删除日志控制块 %1？").arg(name)) != QMessageBox::Yes) return;
		ln->logs.erase(ln->logs.begin() + idx);
	}
	else if (kind == "setcb")
	{
		if (idx < 0 || idx >= (int)ln->settings.size()) return;
		name = QString::fromUtf8(ln->settings[idx].name.c_str());
		if (QMessageBox::question(this, tr("确认"), tr("确认删除定值控制块 %1？").arg(name)) != QMessageBox::Yes) return;
		ln->settings.erase(ln->settings.begin() + idx);
	}
	buildTree();
	log(tr("删除控制块 %1").arg(name));
}

// ---------------- 第二期：DO/DAI 编辑与模板库 ----------------

std::vector<SclCdcTemplate> MainWindow::mergedCdcTemplates() const
{
	std::vector<SclCdcTemplate> list = SclTemplates::BuiltinCdcTemplates();
	for (size_t i = 0; i < m_userCdc.size(); ++i)
		list.push_back(m_userCdc[i]);
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
	for (size_t i = 0; i < das.size(); ++i)
	{
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
		if (vs == "true" || vs == "1" || vs == tr("是"))
		{
			normalized = "true";
			return true;
		}
		if (vs == "false" || vs == "0" || vs == tr("否"))
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
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(QJsonDocument(arr).toJson());
}

void MainWindow::addDo(int li, int ni)
{
	SclLn* ln = lnAt(li, ni);
	if (ln == 0)
		return;
	std::vector<SclCdcTemplate> templates = mergedCdcTemplates();

	QDialog dlg(this);
	dlg.setWindowTitle(tr("添加 DO"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(&dlg);
	QComboBox* cdcCombo = new QComboBox(&dlg);
	for (size_t i = 0; i < templates.size(); ++i)
	{
		QString label = QString::fromUtf8(templates[i].cdc.c_str()) +
			(templates[i].isCustom ? tr("（自定义）") : QString());
		if (!templates[i].desc.empty())
			label += " - " + QString::fromUtf8(templates[i].desc.c_str());
		cdcCombo->addItem(label, QString::fromUtf8(templates[i].cdc.c_str()));
	}
	QComboBox* enumCombo = new QComboBox(&dlg);
	enumCombo->addItems(enumIdList());
	QLineEdit* descEdit = new QLineEdit(&dlg);
	form->addRow(tr("DO 名称"), nameEdit);
	form->addRow(tr("CDC 模板"), cdcCombo);
	form->addRow(tr("枚举类型(stVal)"), enumCombo);
	form->addRow(tr("描述"), descEdit);
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
			QMessageBox::warning(&dlg, tr("校验失败"), tr("DO 名称须为 1-64 位、字母开头、仅含字母数字下划线"));
			continue;
		}
		bool dup = false;
		for (size_t i = 0; i < ln->dos.size(); ++i)
			if (ln->dos[i].name == name) dup = true;
		if (dup)
		{
			QMessageBox::warning(&dlg, tr("校验失败"), tr("DO 名称在该逻辑节点内须唯一"));
			continue;
		}
		bool found = false;
		SclCdcTemplate tpl = findCdcTemplate(cdc, found);
		if (!found)
		{
			QMessageBox::warning(&dlg, tr("校验失败"), tr("CDC 模板不存在"));
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
		log(tr("添加 DO %1（CDC=%2）到 %3").arg(nameEdit->text())
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
	dlg.setWindowTitle(tr("编辑 DO 名称"));
	QFormLayout* form = new QFormLayout(&dlg);
	QLineEdit* nameEdit = new QLineEdit(QString::fromUtf8(dobj->name.c_str()), &dlg);
	form->addRow(tr("DO 名称"), nameEdit);
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
			QMessageBox::warning(this, tr("校验失败"), tr("DO 名称在该逻辑节点内须唯一"));
			return;
		}
	}
	QString old = QString::fromUtf8(dobj->name.c_str());
	dobj->name = name;
	buildTree();
	log(tr("DO 重命名：%1 -> %2").arg(old).arg(QString::fromUtf8(name.c_str())));
}

void MainWindow::deleteDo(int li, int ni, int di)
{
	SclLn* ln = lnAt(li, ni);
	SclDo* dobj = doAt(li, ni, di);
	if (ln == 0 || dobj == 0)
		return;
	if (QMessageBox::question(this, tr("确认"),
		tr("确认删除 DO %1？").arg(QString::fromUtf8(dobj->name.c_str()))) != QMessageBox::Yes)
		return;
	QString name = QString::fromUtf8(dobj->name.c_str());
	ln->dos.erase(ln->dos.begin() + di);
	buildTree();
	log(tr("删除 DO %1").arg(name));
}

void MainWindow::manageTemplates()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("模板库管理"));
	dlg.resize(820, 560);
	QVBoxLayout* vbox = new QVBoxLayout(&dlg);

	QTabWidget* tabs = new QTabWidget(&dlg);
	vbox->addWidget(tabs);

	// Tab1: CDC 模板
	QWidget* cdcTab = new QWidget(&dlg);
	QVBoxLayout* cdcLay = new QVBoxLayout(cdcTab);
	QHBoxLayout* searchLay = new QHBoxLayout;
	searchLay->addWidget(new QLabel(tr("筛选："), &dlg));
	QLineEdit* searchEdit = new QLineEdit(&dlg);
	searchLay->addWidget(searchEdit);
	cdcLay->addLayout(searchLay);
	QTableWidget* table = new QTableWidget(0, 4, &dlg);
	table->setHorizontalHeaderLabels(QStringList() << tr("类型") << tr("CDC") << tr("DA 数") << tr("DA 列表"));
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
			table->setItem(row, 0, new QTableWidgetItem(all[i].isCustom ? tr("自定义") : tr("内置")));
			table->setItem(row, 1, new QTableWidgetItem(cdc));
			table->setItem(row, 2, new QTableWidgetItem(QString::number((int)all[i].das.size())));
			QString das;
			for (size_t d = 0; d < all[i].das.size(); ++d)
				das += (d ? "、" : "") + QString::fromUtf8(all[i].das[d].name.c_str());
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
	QPushButton* addBtn = new QPushButton(tr("添加模板"), &dlg);
	QPushButton* editBtn = new QPushButton(tr("编辑模板"), &dlg);
	QPushButton* delBtn = new QPushButton(tr("删除模板"), &dlg);
	cdcBtns->addWidget(addBtn);
	cdcBtns->addWidget(editBtn);
	cdcBtns->addWidget(delBtn);
	cdcBtns->addStretch(1);
	cdcLay->addLayout(cdcBtns);
	tabs->addTab(cdcTab, tr("公共数据类(CDC)"));

	// Tab2: LNClass 模板（只读）
	QWidget* lnTab = new QWidget(&dlg);
	QVBoxLayout* lnLay = new QVBoxLayout(lnTab);
	QTableWidget* lnTable = new QTableWidget(0, 3, &dlg);
	lnTable->setHorizontalHeaderLabels(QStringList() << tr("LnClass") << tr("描述") << tr("DO 列表"));
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
	lnLay->addWidget(new QLabel(tr("逻辑节点类模板为内置标准模板（只读）。添加逻辑节点时从此表选择。"), &dlg));
	lnLay->addWidget(lnTable);
	tabs->addTab(lnTab, tr("逻辑节点类(LNClass)"));

	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, &dlg);
	connect(box, SIGNAL(rejected()), &dlg, SLOT(reject()));
	vbox->addWidget(box);

	// 编辑/新增 CDC 模板的内嵌对话框
	auto editCdcDialog = [&](SclCdcTemplate& t, bool isNew) -> bool {
		QDialog ed(&dlg);
		ed.setWindowTitle(isNew ? tr("添加 CDC 模板") : tr("编辑 CDC 模板"));
		ed.resize(560, 420);
		QVBoxLayout* lay = new QVBoxLayout(&ed);
		QFormLayout* form = new QFormLayout;
		QLineEdit* cdcEdit = new QLineEdit(QString::fromUtf8(t.cdc.c_str()), &ed);
		QLineEdit* descEdit = new QLineEdit(QString::fromUtf8(t.desc.c_str()), &ed);
		form->addRow(tr("CDC 名称"), cdcEdit);
		form->addRow(tr("描述"), descEdit);
		lay->addLayout(form);
		QTableWidget* daTable = new QTableWidget(0, 4, &ed);
		daTable->setHorizontalHeaderLabels(QStringList() << tr("name") << tr("bType") << tr("fc") << tr("type(Struct 时)"));
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
		for (size_t i = 0; i < t.das.size(); ++i)
			addDaRow(t.das[i]);
		lay->addWidget(daTable);
		QHBoxLayout* daBtns = new QHBoxLayout;
		QPushButton* rowAdd = new QPushButton(tr("添加 DA"), &ed);
		QPushButton* rowDel = new QPushButton(tr("删除选中 DA"), &ed);
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
		if (ed.exec() != QDialog::Accepted)
			return false;
		std::string cdc = cdcEdit->text().toUtf8().constData();
		if (!validIdent(cdc))
		{
			QMessageBox::warning(&ed, tr("校验失败"), tr("CDC 名称须合法"));
			return false;
		}
		if (isNew)
		{
			std::vector<SclCdcTemplate> all = mergedCdcTemplates();
			for (size_t i = 0; i < all.size(); ++i)
			{
				if (all[i].cdc == cdc)
				{
					QMessageBox::warning(&ed, tr("校验失败"), tr("CDC 名称与现有模板重复"));
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
					QMessageBox::warning(&ed, tr("校验失败"), tr("Struct 类型的 DA 必须填写 type（如 AnalogueValue_1）"));
					return false;
				}
				t.das.push_back(da);
			}
		}
		if (t.das.empty())
		{
			QMessageBox::warning(&ed, tr("校验失败"), tr("至少需要一个 DA"));
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
		log(tr("添加自定义 CDC 模板：%1").arg(QString::fromUtf8(t.cdc.c_str())));
	});
	connect(editBtn, &QPushButton::clicked, this, [&]() {
		int r = table->currentRow();
		if (r < 0) return;
		int idx = table->item(r, 0)->data(Qt::UserRole).toInt();
		std::vector<SclCdcTemplate> all = mergedCdcTemplates();
		if (idx < 0 || idx >= (int)all.size()) return;
		if (!all[idx].isCustom)
		{
			QMessageBox::information(&dlg, tr("提示"), tr("内置模板不可编辑，可新建自定义模板替代"));
			return;
		}
		SclCdcTemplate t = all[idx];
		if (!editCdcDialog(t, false))
			return;
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
		log(tr("修改自定义 CDC 模板：%1").arg(QString::fromUtf8(t.cdc.c_str())));
	});
	connect(delBtn, &QPushButton::clicked, this, [&]() {
		int r = table->currentRow();
		if (r < 0) return;
		int idx = table->item(r, 0)->data(Qt::UserRole).toInt();
		std::vector<SclCdcTemplate> all = mergedCdcTemplates();
		if (idx < 0 || idx >= (int)all.size()) return;
		if (!all[idx].isCustom)
		{
			QMessageBox::information(&dlg, tr("提示"), tr("内置模板不可删除"));
			return;
		}
		if (QMessageBox::question(&dlg, tr("确认"), tr("确认删除模板 %1？").arg(QString::fromUtf8(all[idx].cdc.c_str()))) != QMessageBox::Yes)
			return;
		for (size_t u = 0; u < m_userCdc.size(); ++u)
		{
			if (m_userCdc[u].cdc == all[idx].cdc)
			{
				QString name = QString::fromUtf8(m_userCdc[u].cdc.c_str());
				m_userCdc.erase(m_userCdc.begin() + u);
				saveUserTemplates();
				fillTable(searchEdit->text());
				log(tr("删除自定义 CDC 模板：%1").arg(name));
				break;
			}
		}
	});

	dlg.exec();
}
