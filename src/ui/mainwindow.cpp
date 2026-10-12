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
#include <QStringList>
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
	setWindowTitle(tr("newfert IED设计器"));
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
	setupMenus();

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

void MainWindow::setupMenus()
{
	QMenu* fileMenu = menuBar()->addMenu(tr("文件"));
	fileMenu->addAction(tr("新建"), this, SLOT(onNewModel()));
	m_openAct = fileMenu->addAction(tr("打开SCL模型"));
	connect(m_openAct, SIGNAL(triggered()), this, SLOT(onOpenScl()));
	m_recentMenu = fileMenu->addMenu(tr("最近打开"));
	rebuildRecentMenu();
	fileMenu->addAction(tr("保存"), this, SLOT(onSaveCid()));
	fileMenu->addAction(tr("另存为"), this, SLOT(onSaveCidAs()));
	QAction* closeAct = fileMenu->addAction(tr("关闭"));
	closeAct->setEnabled(false);
	fileMenu->addSeparator();
	fileMenu->addAction(tr("退出"), this, SLOT(close()));

	QMenu* toolMenu = menuBar()->addMenu(tr("工具"));
	toolMenu->addAction(tr("导出ICD文件"), this, SLOT(onExportIcd()));
	toolMenu->addAction(tr("导出CID文件"), this, SLOT(onExportCid()));
	toolMenu->addAction(tr("导出CSV点表"), this, SLOT(onExportCsv()));
	toolMenu->addSeparator();
	toolMenu->addAction(tr("XML 源码预览"), this, SLOT(onXmlPreview()));
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

QTreeWidgetItem* MainWindow::addNode(QTreeWidgetItem* parent, const QString& text,
	const QString& detail, const QString& path)
{
	QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
	item->setText(0, text);
	item->setText(1, detail);
	item->setData(0, Qt::UserRole, path);
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
			addNode(ln0Item, tr("DO ") + QString::fromUtf8(dobj.name.c_str()),
				QString::fromUtf8(dobj.cdc.c_str()) + " " + QString::fromUtf8(dobj.desc.c_str()),
				QString("do:%1:-1:%2").arg((int)li).arg((int)d));
		}
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
				addNode(lnItem, tr("DO ") + QString::fromUtf8(dobj.name.c_str()),
					QString::fromUtf8(dobj.cdc.c_str()) + " " + QString::fromUtf8(dobj.desc.c_str()),
					QString("do:%1:%2:%3").arg((int)li).arg((int)ni).arg((int)d));
			}
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
			editable << false << false << true;
		}
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
		if (row == 2) dobj->desc = v;
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
		copyLnAct = menu.addAction(tr("复制逻辑节点"));
		if (g_hasClipLn)
			pasteLnAct = menu.addAction(tr("粘贴逻辑节点"));
		delLnAct = menu.addAction(tr("删除逻辑节点"));
		menu.addSeparator();
		refAct = menu.addAction(tr("获取引用"));
	}
	else if (parts.size() == 4 && parts[0] == "do")
	{
		refAct = menu.addAction(tr("获取引用"));
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
