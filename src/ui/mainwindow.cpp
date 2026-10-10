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
#include <QInputDialog>
#include <QLabel>
#include <QDir>
#include <QTime>
#include <QStringList>

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
	, m_loadingProps(false)
	, m_jsonAct(0)
	, m_onlineAct(0)
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

	QMenu* fileMenu = menuBar()->addMenu(tr("文件"));
	fileMenu->addAction(tr("新建"), this, SLOT(onNewModel()));
	QAction* openAct = fileMenu->addAction(tr("打开SCL模型"));
	connect(openAct, SIGNAL(triggered()), this, SLOT(onOpenScl()));
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
	m_jsonAct = toolMenu->addAction(tr("导入在线JSON"));
	connect(m_jsonAct, SIGNAL(triggered()), this, SLOT(onImportJson()));
	m_onlineAct = toolMenu->addAction(tr("在线读取IED"));
	connect(m_onlineAct, SIGNAL(triggered()), this, SLOT(onOnlineRead()));

	connect(m_tree, SIGNAL(itemSelectionChanged()), this, SLOT(onTreeSelectionChanged()));
	connect(m_tree, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(onTreeContextMenu(const QPoint&)));
	connect(m_props, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(onPropChanged(QTableWidgetItem*)));

	m_model = SclModel();
	m_model.ied.name = "TEMPLATE";
	SclLDevice ld;
	ld.inst = "LD1";
	ld.ldName = "LD1";
	ld.ln0 = SclTemplates::MakeLn("LLN0");
	SclLn lphd = SclTemplates::MakeLn("LPHD");
	lphd.inst = "1";
	ld.lns.push_back(lphd);
	SclLn mmxu = SclTemplates::MakeLn("MMXU");
	mmxu.inst = "1";
	ld.lns.push_back(mmxu);
	m_model.ied.lds.push_back(ld);

	buildTree();
	log(tr("就绪。新建模型已载入：IED=%1").arg(QString::fromLocal8Bit(m_model.ied.name.c_str())));
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
	QTreeWidgetItem* iedItem = addNode(0, tr("IED ") + QString::fromLocal8Bit(ied.name.c_str()),
		QString::fromLocal8Bit(ied.desc.c_str()), "ied");
	QTreeWidgetItem* apItem = addNode(iedItem, tr("AccessPoint ") + QString::fromLocal8Bit(ied.apName.c_str()),
		QString(), "ap");
	QTreeWidgetItem* svrItem = addNode(apItem, tr("Server"), QString(), "server");
	for (size_t li = 0; li < ied.lds.size(); ++li)
	{
		const SclLDevice& ld = ied.lds[li];
		QString ldDetail = QString::fromLocal8Bit(ld.ldName.c_str());
		if (!ld.desc.empty())
			ldDetail += " " + QString::fromLocal8Bit(ld.desc.c_str());
		QTreeWidgetItem* ldItem = addNode(svrItem, tr("LDevice ") + QString::fromLocal8Bit(ld.inst.c_str()),
			ldDetail, QString("ld:%1").arg((int)li));
		QTreeWidgetItem* ln0Item = addNode(ldItem, tr("LN0 ") + QString::fromLocal8Bit(ld.ln0.lnClass.c_str()),
			tr("逻辑节点零"), QString("ln0:%1").arg((int)li));
		for (size_t d = 0; d < ld.ln0.dos.size(); ++d)
		{
			const SclDo& dobj = ld.ln0.dos[d];
			addNode(ln0Item, tr("DO ") + QString::fromLocal8Bit(dobj.name.c_str()),
				QString::fromLocal8Bit(dobj.cdc.c_str()) + " " + QString::fromLocal8Bit(dobj.desc.c_str()),
				QString("do:%1:-1:%2").arg((int)li).arg((int)d));
		}
		for (size_t ni = 0; ni < ld.lns.size(); ++ni)
		{
			const SclLn& ln = ld.lns[ni];
			QTreeWidgetItem* lnItem = addNode(ldItem, tr("LN ") + QString::fromLocal8Bit(ln.Name().c_str()),
				QString::fromLocal8Bit(ln.desc.c_str()), QString("ln:%1:%2").arg((int)li).arg((int)ni));
			for (size_t d = 0; d < ln.dos.size(); ++d)
			{
				const SclDo& dobj = ln.dos[d];
				addNode(lnItem, tr("DO ") + QString::fromLocal8Bit(dobj.name.c_str()),
					QString::fromLocal8Bit(dobj.cdc.c_str()) + " " + QString::fromLocal8Bit(dobj.desc.c_str()),
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
		keys << tr("IED名称") << tr("描述") << tr("AccessPoint") << tr("IP") << tr("子网掩码") << tr("网关") << tr("端口");
		values << QString::fromLocal8Bit(ied.name.c_str()) << QString::fromLocal8Bit(ied.desc.c_str())
			<< QString::fromLocal8Bit(ied.apName.c_str()) << QString::fromLocal8Bit(ied.ip.c_str())
			<< QString::fromLocal8Bit(ied.ipSubnet.c_str()) << QString::fromLocal8Bit(ied.ipGateway.c_str())
			<< QString::number(ied.port);
		for (int i = 0; i < 7; ++i) editable << true;
	}
	else if (parts.size() == 2 && parts[0] == "ld")
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		if (ld)
		{
			keys << tr("Instance") << tr("LDName") << tr("描述");
			values << QString::fromLocal8Bit(ld->inst.c_str()) << QString::fromLocal8Bit(ld->ldName.c_str())
				<< QString::fromLocal8Bit(ld->desc.c_str());
			editable << true << true << true;
		}
	}
	else if (parts[0] == "ln0" || (parts[0] == "ln" && parts.size() == 3))
	{
		SclLn* ln = 0;
		if (parts[0] == "ln0")
			ln = lnAt(parts[1].toInt(), -1);
		else
			ln = lnAt(parts[1].toInt(), parts[2].toInt());
		if (ln)
		{
			keys << tr("Prefix") << tr("LnClass") << tr("Inst") << tr("描述");
			values << QString::fromLocal8Bit(ln->prefix.c_str()) << QString::fromLocal8Bit(ln->lnClass.c_str())
				<< QString::fromLocal8Bit(ln->inst.c_str()) << QString::fromLocal8Bit(ln->desc.c_str());
			editable << true << false << true << true;
		}
	}
	else if (parts.size() == 4 && parts[0] == "do")
	{
		SclDo* dobj = doAt(parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
		if (dobj)
		{
			keys << tr("名称") << tr("CDC") << tr("描述");
			values << QString::fromLocal8Bit(dobj->name.c_str()) << QString::fromLocal8Bit(dobj->cdc.c_str())
				<< QString::fromLocal8Bit(dobj->desc.c_str());
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
	std::string v = value.toLocal8Bit().constData();
	QStringList parts = path.split(':');
	if (path == "ied")
	{
		SclIed& ied = m_model.ied;
		switch (row)
		{
		case 0: ied.name = v; break;
		case 1: ied.desc = v; break;
		case 2: ied.apName = v; break;
		case 3: ied.ip = v; break;
		case 4: ied.ipSubnet = v; break;
		case 5: ied.ipGateway = v; break;
		case 6: ied.port = value.toInt(); break;
		default: return;
		}
	}
	else if (parts.size() == 2 && parts[0] == "ld")
	{
		SclLDevice* ld = ldAt(parts[1].toInt());
		if (ld == 0) return;
		if (row == 0) ld->inst = v;
		else if (row == 1) ld->ldName = v;
		else if (row == 2) ld->desc = v;
		else return;
	}
	else if (parts[0] == "ln0" || parts[0] == "ln")
	{
		int ni = parts[0] == "ln0" ? -1 : parts[2].toInt();
		SclLn* ln = lnAt(parts[1].toInt(), ni);
		if (ln == 0) return;
		if (row == 0) ln->prefix = v;
		else if (row == 2) ln->inst = v;
		else if (row == 3) ln->desc = v;
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
}

void MainWindow::validateAndLog(const std::string& sclText)
{
	ValidateOutcome out;
	std::string temp = QDir::tempPath().toLocal8Bit().constData();
	temp += "/ieddesigner_validate.scl";
	ValidateSclText(sclText, m_model.ied.name, temp, out);
	log(tr("loader 试载：DI=%1 AI=%2 DO=%3 AO=%4 SG=%5 ied=%6")
		.arg((int)out.table.diRefs.size()).arg((int)out.table.aiRefs.size())
		.arg((int)out.table.doRefs.size()).arg((int)out.table.aoRefs.size())
		.arg((int)out.table.sgRefs.size())
		.arg(QString::fromLocal8Bit(out.detectedIed.c_str())));
	if (out.sgQuirk)
		log(tr("注意：loader 校验报 SG contains an invalid point（loader 已知缺陷，ucType=2 被 ValidateRefs 拒绝），点表仍已解析"));
	else if (!out.loaderOk)
		log(tr("loader 校验失败：%1").arg(QString::fromLocal8Bit(out.errorText.c_str())));
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

	m_model = SclModel();
	m_model.ied.name = iedEdit->text().toLocal8Bit().constData();
	m_model.ied.apName = apEdit->text().toLocal8Bit().constData();
	SclLDevice ld;
	ld.inst = ldEdit->text().toLocal8Bit().constData();
	ld.ldName = ld.inst;
	ld.ln0 = SclTemplates::MakeLn("LLN0");
	SclLn lphd = SclTemplates::MakeLn("LPHD");
	lphd.inst = "1";
	ld.lns.push_back(lphd);
	m_model.ied.lds.push_back(ld);
	m_currentFile.clear();
	buildTree();
	log(tr("新建模型：IED=%1").arg(iedEdit->text()));
}

void MainWindow::onTreeContextMenu(const QPoint& pos)
{
	QTreeWidgetItem* item = m_tree->itemAt(pos);
	if (item == 0)
		return;
	QString path = item->data(0, Qt::UserRole).toString();
	QStringList parts = path.split(':');
	QMenu menu(this);
	QAction* addAct = 0;
	QAction* delAct = 0;
	if (parts.size() == 2 && parts[0] == "ld")
		addAct = menu.addAction(tr("添加逻辑节点"));
	if (parts.size() == 3 && parts[0] == "ln")
		delAct = menu.addAction(tr("删除逻辑节点"));
	QAction* picked = menu.exec(m_tree->mapToGlobal(pos));
	if (picked == 0)
		return;

	int li = parts[1].toInt();
	SclLDevice* ld = ldAt(li);
	if (ld == 0)
		return;

	if (picked == addAct)
	{
		QDialog dlg(this);
		dlg.setWindowTitle(tr("添加逻辑节点"));
		QVBoxLayout* vbox = new QVBoxLayout(&dlg);
		QListWidget* list = new QListWidget(&dlg);
		const std::vector<SclLnT>& table = SclTemplates::LnTypes();
		for (size_t i = 0; i < table.size(); ++i)
		{
			if (table[i].lnClass == std::string("LLN0"))
				continue;
			QString label = QString::fromLocal8Bit(table[i].lnClass) + " - " + QString::fromLocal8Bit(table[i].desc);
			new QListWidgetItem(label, list);
			list->item(list->count() - 1)->setData(Qt::UserRole, QString::fromLocal8Bit(table[i].lnClass));
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
		if (dlg.exec() != QDialog::Accepted || list->currentItem() == 0)
			return;
		std::string lnClass = list->currentItem()->data(Qt::UserRole).toString().toLocal8Bit().constData();
		SclLn ln = SclTemplates::MakeLn(lnClass);
		ln.inst = instEdit->text().toLocal8Bit().constData();
		ln.prefix = prefixEdit->text().toLocal8Bit().constData();
		ld->lns.push_back(ln);
		buildTree();
		log(tr("添加逻辑节点 %1%2 到 %3").arg(QString::fromLocal8Bit(lnClass.c_str())).arg(instEdit->text()).arg(QString::fromLocal8Bit(ld->inst.c_str())));
	}
	else if (picked == delAct)
	{
		int ni = parts[2].toInt();
		if (ni < 0 || ni >= (int)ld->lns.size())
			return;
		QString name = QString::fromLocal8Bit(ld->lns[ni].Name().c_str());
		ld->lns.erase(ld->lns.begin() + ni);
		buildTree();
		log(tr("删除逻辑节点 %1").arg(name));
	}
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
	std::string path = m_currentFile.toLocal8Bit().constData();
	if (!SclWriter::WriteCid(m_model, path, err))
	{
		QMessageBox::warning(this, tr("保存失败"), QString::fromLocal8Bit(err.c_str()));
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
	if (!SclWriter::WriteIcd(m_model, path.toLocal8Bit().constData(), err))
	{
		QMessageBox::warning(this, tr("导出失败"), QString::fromLocal8Bit(err.c_str()));
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
	if (!SclWriter::WriteCid(m_model, path.toLocal8Bit().constData(), err))
	{
		QMessageBox::warning(this, tr("导出失败"), QString::fromLocal8Bit(err.c_str()));
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
	std::string temp = QDir::tempPath().toLocal8Bit().constData();
	temp += "/ieddesigner_validate.scl";
	ValidateSclText(sclText, m_model.ied.name, temp, out);
	if (!out.refsParsed)
	{
		QMessageBox::warning(this, tr("导出失败"),
			tr("loader 未能解析出任何点：%1").arg(QString::fromLocal8Bit(out.errorText.c_str())));
		return;
	}
	std::string err;
	if (!CsvWriter::WriteCsv(m_model, out.table, path.toLocal8Bit().constData(), err))
	{
		QMessageBox::warning(this, tr("导出失败"), QString::fromLocal8Bit(err.c_str()));
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
	std::string err;
	std::string npath = path.toLocal8Bit().constData();
	std::vector<std::string> ieds = SclReader::ListIeds(npath, err);
	if (ieds.empty())
	{
		QMessageBox::warning(this, tr("打开失败"), QString::fromLocal8Bit(err.c_str()));
		return;
	}
	std::string pick = ieds[0];
	if (ieds.size() > 1)
	{
		QStringList items;
		for (size_t i = 0; i < ieds.size(); ++i)
			items << QString::fromLocal8Bit(ieds[i].c_str());
		QString sel = QInputDialog::getItem(this, tr("选择IED"),
			tr("文件含多个IED，选择要打开的："), items, 0, false);
		if (sel.isEmpty())
			return;
		pick = sel.toLocal8Bit().constData();
	}
	SclModel loaded;
	if (!SclReader::ReadSclFile(npath, pick, loaded, err))
	{
		QMessageBox::warning(this, tr("打开失败"), QString::fromLocal8Bit(err.c_str()));
		return;
	}
	m_model = loaded;
	m_currentFile = path;
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
	log(tr("已打开 %1：IED=%2 LD=%3 LN=%4 DO=%5").arg(path)
		.arg(QString::fromLocal8Bit(pick.c_str()))
		.arg((int)m_model.ied.lds.size()).arg(lnCount).arg(doCount));
}

void MainWindow::onImportJson()
{
	QString path = QFileDialog::getOpenFileName(this, tr("导入在线JSON"),
		QDir::currentPath(), tr("JSON (*.json);;All files (*.*)"));
	if (path.isEmpty())
		return;
	std::string err;
	SclModel loaded;
	if (!JsonModel::ReadOnlineJsonFile(path.toLocal8Bit().constData(), "",
		m_model.ied.ip, m_model.ied.port, loaded, err))
	{
		QMessageBox::warning(this, tr("导入失败"), QString::fromLocal8Bit(err.c_str()));
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
		.arg(QString::fromLocal8Bit(m_model.ied.name.c_str()))
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
	log(tr("在线读取完成：IED=%1 LD=%2 LN=%3 DO=%4").arg(QString::fromLocal8Bit(m_model.ied.name.c_str()))
		.arg((int)m_model.ied.lds.size()).arg(lnCount).arg(doCount));
	t->deleteLater();
}

void MainWindow::applyInitialColumnRatio()
{
	m_tree->header()->setStretchLastSection(true);
	m_tree->setColumnWidth(0, m_tree->viewport()->width() * 4 / 10);
}
