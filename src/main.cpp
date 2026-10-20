#include <QApplication>
#include <QTextCodec>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QCoreApplication>
#include <QSettings>
#include <QTreeWidget>
#include <QGridLayout>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QThread>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "mainwindow.h"
#include "sclmodel.h"
#include "scltemplates.h"
#include "sclwriter.h"
#include "sclreader.h"
#include "jsonmodel.h"
#include "browsemodel.h"
#include "onlineclient.h"
#include "csvwriter.h"
#include "modelvalidate.h"
#include "xsdvalidator.h"

// 第三期任务 3：打印 loader 侧数据集/控制块语义校验结果
static void PrintSemantics(const char* tag, const ValidateOutcome& v);

// 源码为 GBK（本分支约定）：中文窄字符串需经 GBK 编解码；QSS/翻译文件为 UTF-8 走 fromUtf8
static void SetupCodecs()
{
	QTextCodec* gbk = QTextCodec::codecForName("GBK");
	if (gbk == 0)
		gbk = QTextCodec::codecForName("System");
	if (gbk != 0)
	{
		QTextCodec::setCodecForLocale(gbk);
		QTextCodec::setCodecForTr(gbk);
		QTextCodec::setCodecForCStrings(gbk);
	}
}

static int RunSelfTest(const char* icdPath, const char* csvPath)
{
	SclModel model = MakeDemoModel();
	std::string err;
	std::string text = SclWriter::BuildSclText(model, false);
	if (!SclWriter::WriteFile(text, icdPath, err))
	{
		printf("SELFTEST FAIL write icd: %s\n", err.c_str());
		return 1;
	}

	ValidateOutcome out;
	std::string temp = std::string(icdPath) + ".validate.tmp";
	ValidateSclText(text, model.ied.name, temp, out);

	bool csvOk = CsvWriter::WriteCsv(model, out.table, csvPath, err);

	printf("SELFTEST ied=%s\n", out.detectedIed.c_str());
	printf("SELFTEST counts DI=%d AI=%d DO=%d AO=%d SG=%d\n",
		(int)out.table.diRefs.size(), (int)out.table.aiRefs.size(),
		(int)out.table.doRefs.size(), (int)out.table.aoRefs.size(),
		(int)out.table.sgRefs.size());
	printf("SELFTEST loaderOk=%d sgQuirk=%d err=%s\n",
		out.loaderOk ? 1 : 0, out.sgQuirk ? 1 : 0, out.errorText.c_str());
	PrintSemantics("SELFTEST", out);
	printf("SELFTEST csv=%s\n", csvOk ? "ok" : err.c_str());
	remove(temp.c_str());
	return (out.refsParsed && out.sgPoints > 0 && csvOk) ? 0 : 1;
}

static int RunRoundTrip(const char* srcPath)
{
	std::string err;
	std::vector<std::string> ieds = SclReader::ListIeds(srcPath, err);
	if (ieds.empty())
	{
		printf("ROUNDTRIP FAIL list: %s\n", err.c_str());
		return 1;
	}
	SclModel model;
	if (!SclReader::ReadSclFile(srcPath, ieds[0], model, err))
	{
		printf("ROUNDTRIP FAIL read: %s\n", err.c_str());
		return 1;
	}
	ValidateOutcome orig = ValidateSclFile(srcPath, model.ied.name);

	std::string rtPath = std::string(srcPath) + ".rt.cid";
	std::string text = SclWriter::BuildSclText(model, true);
	if (!SclWriter::WriteFile(text, rtPath, err))
	{
		printf("ROUNDTRIP FAIL write: %s\n", err.c_str());
		return 1;
	}
	ValidateOutcome rt = ValidateSclFile(rtPath, model.ied.name);

	SclModel reloaded;
	bool rereadOk = SclReader::ReadSclFile(rtPath, model.ied.name, reloaded, err);

	printf("ROUNDTRIP ied=%s\n", model.ied.name.c_str());
	printf("ROUNDTRIP orig  DI=%d AI=%d DO=%d AO=%d SG=%d ok=%d\n",
		(int)orig.table.diRefs.size(), (int)orig.table.aiRefs.size(),
		(int)orig.table.doRefs.size(), (int)orig.table.aoRefs.size(),
		(int)orig.table.sgRefs.size(), orig.loaderOk ? 1 : 0);
	printf("ROUNDTRIP rt    DI=%d AI=%d DO=%d AO=%d SG=%d ok=%d\n",
		(int)rt.table.diRefs.size(), (int)rt.table.aiRefs.size(),
		(int)rt.table.doRefs.size(), (int)rt.table.aoRefs.size(),
		(int)rt.table.sgRefs.size(), rt.loaderOk ? 1 : 0);
	printf("ROUNDTRIP reread=%d lds=%d err=%s\n",
		rereadOk ? 1 : 0, (int)reloaded.ied.lds.size(), err.c_str());
	PrintSemantics("ROUNDTRIP orig", orig);
	PrintSemantics("ROUNDTRIP rt", rt);

	// ExtRef 外部引用计数比对（第三期任务 1 回归）
	int extSrc = 0, extDst = 0;
	for (size_t i = 0; i < model.ied.lds.size(); ++i)
	{
		extSrc += (int)model.ied.lds[i].ln0.extRefs.size();
		for (size_t n = 0; n < model.ied.lds[i].lns.size(); ++n)
			extSrc += (int)model.ied.lds[i].lns[n].extRefs.size();
	}
	for (size_t i = 0; i < reloaded.ied.lds.size(); ++i)
	{
		extDst += (int)reloaded.ied.lds[i].ln0.extRefs.size();
		for (size_t n = 0; n < reloaded.ied.lds[i].lns.size(); ++n)
			extDst += (int)reloaded.ied.lds[i].lns[n].extRefs.size();
	}
	printf("ROUNDTRIP extRefs=%d->%d\n", extSrc, extDst);

	bool same = orig.table.diRefs.size() == rt.table.diRefs.size() &&
		orig.table.aiRefs.size() == rt.table.aiRefs.size() &&
		orig.table.doRefs.size() == rt.table.doRefs.size() &&
		orig.table.aoRefs.size() == rt.table.aoRefs.size() &&
		orig.table.sgRefs.size() == rt.table.sgRefs.size() &&
		extSrc == extDst;
	printf("ROUNDTRIP same=%d\n", same ? 1 : 0);
	return (same && rereadOk && orig.refsParsed) ? 0 : 1;
}

// 第三期任务 3：打印 loader 侧数据集/控制块语义校验结果
static void PrintSemantics(const char* tag, const ValidateOutcome& v)
{
	printf("%s semantics ds=%d report=%d gse=%d sv=%d log=%d setting=%d gseAddr=%d smvAddr=%d errors=%d\n",
		tag, v.datasetCount, v.reportControlCount, v.gseControlCount,
		v.sampledValueControlCount, v.logControlCount, v.settingControlCount,
		v.gseAddressCount, v.smvAddressCount, (int)v.semanticErrors.size());
	for (size_t i = 0; i < v.semanticErrors.size(); ++i)
		printf("%s semantic warning: %s\n", tag, v.semanticErrors[i].c_str());
}

static void PrintModelStats(const char* tag, const SclModel& model, const ValidateOutcome& v)
{
	int lnCount = 0;
	int doCount = 0;
	for (size_t li = 0; li < model.ied.lds.size(); ++li)
	{
		const SclLDevice& ld = model.ied.lds[li];
		lnCount += (int)ld.lns.size();
		doCount += (int)ld.ln0.dos.size();
		for (size_t ni = 0; ni < ld.lns.size(); ++ni)
			doCount += (int)ld.lns[ni].dos.size();
	}
	printf("%s ied=%s LD=%d LN=%d DO=%d\n", tag, model.ied.name.c_str(),
		(int)model.ied.lds.size(), lnCount, doCount);
	printf("%s points DI=%d AI=%d DO=%d AO=%d SG=%d ok=%d\n", tag,
		(int)v.table.diRefs.size(), (int)v.table.aiRefs.size(),
		(int)v.table.doRefs.size(), (int)v.table.aoRefs.size(),
		(int)v.table.sgRefs.size(), v.loaderOk ? 1 : 0);
	PrintSemantics(tag, v);
}

static int RunJsonImport(const char* jsonPath)
{
	std::string err;
	SclModel model;
	if (!JsonModel::ReadOnlineJsonFile(jsonPath, "", "192.168.31.68", 102, model, err))
	{
		printf("JSONIMPORT FAIL: %s\n", err.c_str());
		return 1;
	}
	std::string outPath = std::string(jsonPath) + ".icd";
	std::string text = SclWriter::BuildSclText(model, false);
	if (!SclWriter::WriteFile(text, outPath, err))
	{
		printf("JSONIMPORT FAIL write: %s\n", err.c_str());
		return 1;
	}
	ValidateOutcome v = ValidateSclFile(outPath, model.ied.name);
	PrintModelStats("JSONIMPORT", model, v);
	return v.refsParsed ? 0 : 1;
}

static int RunOnline(const char* ip, int port, const char* ied)
{
	std::string err;
	std::string detected;
	std::vector<BLd> lds;
	if (!OnlineClient::BrowseIedModel(ip, port, ied ? ied : "", detected, lds, err))
	{
		printf("ONLINE FAIL: %s\n", err.c_str());
		return 1;
	}
	SclModel model;
	std::string iedName = (ied != 0 && ied[0] != '\0') ? ied : detected;
	BrowseModel::BuildModelFromBrowse(iedName, ip, port, lds, model);
	std::string text = SclWriter::BuildSclText(model, false);
	std::string outPath = "online_browse.icd";
	if (!SclWriter::WriteFile(text, outPath, err))
	{
		printf("ONLINE FAIL write: %s\n", err.c_str());
		return 1;
	}
	ValidateOutcome v = ValidateSclFile(outPath, model.ied.name);
	PrintModelStats("ONLINE", model, v);
	return v.refsParsed ? 0 : 1;
}

// 界面文案源码为英文，中文经 translate/ieddesigner_zh_CN.qm 加载
static void SetupTranslation(QCoreApplication& app, QTranslator& translator)
{
	// 语言设置
	{
		QSettings settings("newfert", "ieddesigner");
		if (settings.value("language", "zh").toString() == "en")
			return;
	}
	QString appDir = QCoreApplication::applicationDirPath();
	QStringList dirs;
	dirs << appDir << appDir + "/.." << QDir::currentPath();
	for (int i = 0; i < dirs.size(); ++i)
	{
		if (translator.load(QString("ieddesigner_zh_CN"), dirs[i] + "/translate", QString("_")))
		{
			app.installTranslator(&translator);
			return;
		}
	}
	// 回退：使用编译进资源的翻译
	if (translator.load(QString("ieddesigner_zh_CN"), QString(":/i18n"), QString("_")))
		app.installTranslator(&translator);
}

// --autopen：创建真实主窗口并定时触发打开流程（自动化测试使用）
// Qt4 无 lambda/QTimer 函子重载，改用事件循环按时间点驱动同一序列
static int RunAutoOpen(const char* filePath)
{
	int argc = 1;
	char appname[] = "ieddesigner-autopen";
	char* argv[] = { appname, 0 };
	QApplication app(argc, argv);
	SetupCodecs();
	QTranslator translator;
	SetupTranslation(app, translator);
	MainWindow w;
	w.show();
	w.raise();
	w.activateWindow();
	QString file = QString::fromLocal8Bit(filePath);

	QElapsedTimer timer;
	timer.start();
	bool didOpen = false, didRaise2 = false, didRaise3 = false;
	bool didSelDo = false, didSelDai = false;
	while (timer.elapsed() < 8000)
	{
		qint64 e = timer.elapsed();
		if (!didOpen && e >= 800) { w.openSclPath(file); didOpen = true; }
		if (!didRaise2 && e >= 1500) { w.raise(); w.activateWindow(); didRaise2 = true; }
		if (!didSelDo && e >= 2000)
		{
			// 2.0s 选中 DO（3 行属性）
			didSelDo = true;
			QTreeWidget* t = w.findChild<QTreeWidget*>();
			if (t)
			{
				QTreeWidgetItem* it = t->topLevelItem(0);
				if (it) it = it->child(0);                                   // ap:0
				if (it) it = it->child(0);                                   // server
				if (it) it = it->child(0);                                   // ld:0
				if (it) it = it->child(0);                                   // ln0
				if (it && it->childCount() > 0) it = it->child(0);           // DO
				if (it) t->setCurrentItem(it);
			}
		}
		if (!didSelDai && e >= 3200)
		{
			// 3.2s 选中 DAI（4 行属性）——复现“先 DO 后 DAI”的切换序列
			didSelDai = true;
			QTreeWidget* t = w.findChild<QTreeWidget*>();
			if (t)
			{
				QTreeWidgetItem* it = t->topLevelItem(0);
				if (it) it = it->child(0);                                   // ap:0
				if (it) it = it->child(0);                                   // server
				if (it) it = it->child(0);                                   // ld:0
				if (it) it = it->child(0);                                   // ln0
				if (it && it->childCount() > 0) it = it->child(0);           // DO
				if (it && it->childCount() > 0) it = it->child(0);           // DAI
				if (it) t->setCurrentItem(it);
			}
			QGridLayout* g = w.findChild<QGridLayout*>();
			if (g)
			{
				printf("GRID rows=%d\n", g->rowCount());
				for (int r = 0; r < g->rowCount(); ++r)
					printf("  row %d h=%d stretch=%d\n", r, g->cellRect(r, 0).height(), g->rowStretch(r));
			}
		}
		if (!didRaise3 && e >= 3500) { w.raise(); w.activateWindow(); didRaise3 = true; }
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
	return 0;
}

// --opentest：复刻 GUI 打开路径（两段式异步 + 事件循环 + 树构建），用于闪退排查
struct OpenIedList
{
	std::vector<std::string> ieds;
	std::string err;
};

struct OpenLoadResult
{
	bool ok;
	std::string err;
	SclModel model;

	OpenLoadResult() : ok(false) {}
};

// QtConcurrent::run 在 Qt4 只接受函数指针（不支持捕获 lambda），故用自由函数承载任务
static OpenIedList OpenTestListTask(const std::string path)
{
	OpenIedList r;
	r.ieds = SclReader::ListIeds(path.c_str(), r.err);
	return r;
}

static OpenLoadResult OpenTestLoadTask(const std::string path, const std::string ied)
{
	OpenLoadResult r;
	r.ok = SclReader::ReadSclFile(path.c_str(), ied, r.model, r.err);
	return r;
}

// 复刻 buildTree 的 LN 遍历（QTreeWidget + daiSpecs + 数据集/控制块分组）
static void BuildOpenTestLn(QTreeWidgetItem* ldItem, const SclLn& ln)
{
	QTreeWidgetItem* lnItem = new QTreeWidgetItem(ldItem, QStringList(QString::fromUtf8(ln.Name().c_str())));
	for (size_t d = 0; d < ln.dos.size(); ++d)
	{
		const SclDo& dobj = ln.dos[d];
		QTreeWidgetItem* doItem = new QTreeWidgetItem(lnItem, QStringList(QString::fromUtf8(dobj.name.c_str())));
		std::vector<SclDaiSpec> specs = SclTemplates::DaiSpecsForDo(dobj.cdc, dobj.stValEnum, dobj.customDas, dobj.hasCustomDas);
		for (size_t k = 0; k < specs.size(); ++k)
		{
			if (specs[k].path == "d") continue;
			QTreeWidgetItem* daiItem = new QTreeWidgetItem(doItem, QStringList(QString::fromUtf8(specs[k].path.c_str())));
			const std::string* val = dobj.FindDaiVal(specs[k].path);
			daiItem->setText(1, val ? QString::fromUtf8(val->c_str()) : QString());
		}
	}
	if (!ln.dataSets.empty())
	{
		QTreeWidgetItem* sec = new QTreeWidgetItem(lnItem, QStringList(QString("ds-sec")));
		for (size_t s = 0; s < ln.dataSets.size(); ++s)
			new QTreeWidgetItem(sec, QStringList(QString::fromUtf8(ln.dataSets[s].name.c_str())));
	}
	if (!ln.reports.empty() || !ln.gses.empty() || !ln.smvs.empty() || !ln.logs.empty() || !ln.settings.empty())
	{
		QTreeWidgetItem* sec = new QTreeWidgetItem(lnItem, QStringList(QString("cb-sec")));
		for (size_t s = 0; s < ln.reports.size(); ++s)
			new QTreeWidgetItem(sec, QStringList(QString::fromUtf8(ln.reports[s].name.c_str())));
		for (size_t s = 0; s < ln.gses.size(); ++s)
			new QTreeWidgetItem(sec, QStringList(QString::fromUtf8(ln.gses[s].name.c_str())));
		for (size_t s = 0; s < ln.smvs.size(); ++s)
			new QTreeWidgetItem(sec, QStringList(QString::fromUtf8(ln.smvs[s].name.c_str())));
	}
}

static int RunOpenTest(const char* filePath)
{
	int argc = 1;
	char appname[] = "ieddesigner-opentest";
	char* argv[] = { appname, 0 };
	QApplication app(argc, argv);
	SetupCodecs();

	QEventLoop loop;

	QFutureWatcher<OpenIedList> listWatcher;
	QObject::connect(&listWatcher, SIGNAL(finished()), &loop, SLOT(quit()));
	listWatcher.setFuture(QtConcurrent::run(OpenTestListTask, std::string(filePath)));
	loop.exec();
	OpenIedList listResult = listWatcher.result();
	printf("OPENTEST stage1 ieds=%d err=%s\n",
		(int)listResult.ieds.size(), listResult.err.c_str());
	if (listResult.ieds.empty())
		return 1;

	QFutureWatcher<OpenLoadResult> loadWatcher;
	QObject::connect(&loadWatcher, SIGNAL(finished()), &loop, SLOT(quit()));
	loadWatcher.setFuture(QtConcurrent::run(OpenTestLoadTask,
		std::string(filePath), listResult.ieds[0]));
	loop.exec();
	OpenLoadResult loadResult = loadWatcher.result();
	printf("OPENTEST stage2 ok=%d err=%s\n", loadResult.ok ? 1 : 0, loadResult.err.c_str());
	if (!loadResult.ok)
		return 1;

	QTreeWidget tree;
	tree.setColumnCount(2);
	const SclIed& ied = loadResult.model.ied;
	QTreeWidgetItem* iedItem = new QTreeWidgetItem(&tree, QStringList(QString::fromUtf8(ied.name.c_str())));
	iedItem->setData(0, Qt::UserRole, "ied");
	for (size_t li = 0; li < ied.lds.size(); ++li)
	{
		const SclLDevice& ld = ied.lds[li];
		QTreeWidgetItem* ldItem = new QTreeWidgetItem(iedItem, QStringList(QString::fromUtf8(ld.inst.c_str())));
		BuildOpenTestLn(ldItem, ld.ln0);
		for (size_t n = 0; n < ld.lns.size(); ++n)
			BuildOpenTestLn(ldItem, ld.lns[n]);
	}
	QTreeWidgetItem* commItem = new QTreeWidgetItem(&tree, QStringList(QString("comm")));
	for (size_t s = 0; s < loadResult.model.subnets.size(); ++s)
	{
		QTreeWidgetItem* snItem = new QTreeWidgetItem(commItem, QStringList(QString::fromUtf8(loadResult.model.subnets[s].name.c_str())));
		for (size_t c = 0; c < loadResult.model.subnets[s].aps.size(); ++c)
		{
			QTreeWidgetItem* capItem = new QTreeWidgetItem(snItem, QStringList(QString::fromUtf8(loadResult.model.subnets[s].aps[c].apName.c_str())));
			for (size_t g = 0; g < loadResult.model.subnets[s].aps[c].gses.size(); ++g)
				new QTreeWidgetItem(capItem, QStringList(QString::fromUtf8(loadResult.model.subnets[s].aps[c].gses[g].cbName.c_str())));
			for (size_t v = 0; v < loadResult.model.subnets[s].aps[c].smvs.size(); ++v)
				new QTreeWidgetItem(capItem, QStringList(QString::fromUtf8(loadResult.model.subnets[s].aps[c].smvs[v].cbName.c_str())));
		}
	}
	tree.expandToDepth(3);
	for (int i = 0; i < 20; ++i)
	{
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
	printf("OPENTEST tree nodes=%d PASS\n", tree.topLevelItemCount());
	return 0;
}

static int RunSkeleton(const char* outPath)
{
	SclModel model = MakeNewModel();

	// 设置DAI值 验证DAI读写链路
	SclLDevice& ld = model.ied.lds[0];
	for (size_t i = 0; i < ld.ln0.dos.size(); ++i)
	{
		if (ld.ln0.dos[i].name == "NamPlt")
			ld.ln0.dos[i].SetDaiVal("vendor", "newfert-tech");
	}
	for (size_t i = 0; i < ld.lns.size(); ++i)
	{
		for (size_t d = 0; d < ld.lns[i].dos.size(); ++d)
		{
			if (ld.lns[i].dos[d].name == "PhyNam")
				ld.lns[i].dos[d].SetDaiVal("vendor", "newfert");
		}
	}

	// 定值组逻辑节点（第三期任务 3：验证 SG 定值点表非空）
	SclLn sgcb = SclTemplates::MakeLn("SGCB");
	sgcb.inst = "1";
	sgcb.desc = "Setting group control";
	ld.lns.push_back(sgcb);

	// 数据集 + 控制块 + GSE 通信地址（验证完整链路与加载器兼容性）
	SclDataSet ds;
	ds.name = "dsRelay";
	SclFcda f;
	f.iedName = "TEMPLATE";
	f.ldInst = "LD1";
	f.lnClass = "LPHD";
	f.lnInst = "1";
	f.doName = "Proxy";
	f.fc = "ST";
	ds.fcdas.push_back(f);
	ld.ln0.dataSets.push_back(ds);

	SclReportControl rc;
	rc.name = "brcb1";
	rc.rptId = "TEMPLATE_LD1_brcb1";
	rc.dataSet = "dsRelay";
	rc.buffered = true;
	rc.confRev = "1";
	rc.intgPd = "60000";
	rc.trgOps.present = true;
	rc.trgOps.dchg = true;
	rc.trgOps.qchg = true;
	rc.optFields.present = true;
	rc.optFields.seqNum = true;
	rc.optFields.timeStamp = true;
	rc.optFields.entryID = true;
	ld.ln0.reports.push_back(rc);

	SclGseControl gc;
	gc.name = "gcb1";
	gc.appId = "TEMPLATE_LD1_gcb1";
	gc.dataSet = "dsRelay";
	gc.confRev = "1";
	ld.ln0.gses.push_back(gc);

	SclSampledValueControl sc;
	sc.name = "msvcb1";
	sc.smvId = "TEMPLATE_LD1_msvcb1";
	sc.dataSet = "dsRelay";
	sc.confRev = "1";
	ld.ln0.smvs.push_back(sc);

	SclLogControl lc;
	lc.name = "lc1";
	lc.dataSet = "dsRelay";
	lc.logName = "LD1";   // Ed1 schema keyref：logName 须为 IED 内某个 LD 实例名
	lc.trgOps.present = true;
	ld.ln0.logs.push_back(lc);

	SclSettingControl scb;
	scb.name = "scb1";
	scb.dataSet = "dsRelay";
	scb.numOfSG = "2";
	ld.ln0.settings.push_back(scb);

	// ExtRef 外部引用（GOOSE 订阅声明）：DA 级 + DO 级各一条，验证回环
	SclExtRef er;
	er.iedName = "REMOTE1";
	er.ldInst = "LD2";
	er.lnClass = "GGIO";
	er.lnInst = "1";
	er.doName = "Ind1";
	er.daName = "stVal";
	er.desc = "sample external reference";
	er.intAddr = "IN/1";
	ld.ln0.extRefs.push_back(er);
	SclExtRef er2;
	er2.iedName = "REMOTE2";
	er2.ldInst = "LD1";
	er2.lnClass = "MMXU";
	er2.lnInst = "1";
	er2.doName = "TotW";
	ld.ln0.extRefs.push_back(er2);

	if (!model.subnets.empty())
	{
		SclCommAddress ga;
		ga.ldInst = "LD1";
		ga.cbName = "gcb1";
		ga.pParams.push_back(std::make_pair("MAC-Address", "01-0C-CD-01-00-01"));
		ga.pParams.push_back(std::make_pair("APPID", "0x0001"));
		ga.pParams.push_back(std::make_pair("VLAN-ID", "0x001"));
		ga.pParams.push_back(std::make_pair("VLAN-PRIORITY", "4"));
		ga.minTime = "10";
		ga.maxTime = "1000";
		model.subnets[0].aps[0].gses.push_back(ga);
	}

	std::string err;
	std::string text = SclWriter::BuildSclText(model, true);
	if (!SclWriter::WriteFile(text, outPath, err))
	{
		printf("SKELETON FAIL write: %s\n", err.c_str());
		return 1;
	}
	ValidateOutcome v = ValidateSclFile(outPath, model.ied.name);
	PrintModelStats("SKELETON", model, v);
	printf("SKELETON loaderOk=%d err=%s\n", v.loaderOk ? 1 : 0, v.errorText.c_str());

	// 读写回环：再次解析生成的文件，确认新增字段与 DAI 值完整保留
	SclModel reloaded;
	if (!SclReader::ReadSclFile(outPath, model.ied.name, reloaded, err))
	{
		printf("SKELETON FAIL reread: %s\n", err.c_str());
		return 1;
	}
	int diff = 0;
	if (reloaded.ied.manufacturer != model.ied.manufacturer) diff++;
	if (reloaded.ied.type != model.ied.type) diff++;
	if (reloaded.ied.services.size() != model.ied.services.size()) diff++;
	if (reloaded.ied.pParams.size() != model.ied.pParams.size()) diff++;
	if (reloaded.ied.serverTimeout != model.ied.serverTimeout) diff++;
	if (reloaded.ied.authentication != model.ied.authentication) diff++;
	if (!reloaded.ied.lds.empty() && reloaded.ied.lds[0].comment != model.ied.lds[0].comment) diff++;

	// DAI 回读比对
	for (size_t i = 0; i < model.ied.lds.size(); ++i)
	{
		const SclLDevice& src = model.ied.lds[i];
		if (i >= reloaded.ied.lds.size()) { diff++; break; }
		const SclLDevice& dst = reloaded.ied.lds[i];
		for (size_t d = 0; d < src.ln0.dos.size(); ++d)
		{
			const SclDo& s = src.ln0.dos[d];
			const SclDo* t = 0;
			for (size_t k = 0; k < dst.ln0.dos.size(); ++k)
				if (dst.ln0.dos[k].name == s.name) t = &dst.ln0.dos[k];
			if (!t) { diff++; continue; }
			for (size_t k = 0; k < s.dais.size(); ++k)
			{
				const std::string* tv = t->FindDaiVal(s.dais[k].name);
				if (!tv || *tv != s.dais[k].val)
				{
					printf("SKELETON DAI diff: %s.%s\n", s.name.c_str(), s.dais[k].name.c_str());
					diff++;
				}
			}
		}
		// 数据集/控制块回读比对
		const SclLn& s0 = src.ln0;
		const SclLn& d0 = dst.ln0;
		if (d0.dataSets.size() != s0.dataSets.size()) { printf("SKELETON diff: dataSet count\n"); diff++; }
		else if (!s0.dataSets.empty() && (d0.dataSets[0].fcdas.size() != s0.dataSets[0].fcdas.size() ||
			d0.dataSets[0].fcdas[0].ldInst != s0.dataSets[0].fcdas[0].ldInst ||
			d0.dataSets[0].fcdas[0].lnClass != s0.dataSets[0].fcdas[0].lnClass ||
			d0.dataSets[0].fcdas[0].doName != s0.dataSets[0].fcdas[0].doName ||
			d0.dataSets[0].fcdas[0].daName != s0.dataSets[0].fcdas[0].daName ||
			d0.dataSets[0].fcdas[0].fc != s0.dataSets[0].fcdas[0].fc))
		{ printf("SKELETON diff: FCDA\n"); diff++; }   // Ed1 FCDA 无 iedName 属性，回读 iedName 为空不比对
		if (d0.extRefs.size() != s0.extRefs.size())
		{ printf("SKELETON diff: extRef count\n"); diff++; }
		else
		{
			for (size_t e = 0; e < s0.extRefs.size(); ++e)
			{
				// Ed1 schema 的 ExtRef 无 desc 属性（仅存于工程文件本地），回读 desc 为空不比对
				if (d0.extRefs[e].RefText() != s0.extRefs[e].RefText() ||
					d0.extRefs[e].intAddr != s0.extRefs[e].intAddr)
				{ printf("SKELETON extRef diff: %s\n", s0.extRefs[e].RefText().c_str()); diff++; }
			}
		}
		if (d0.reports.size() != s0.reports.size() ||
			(!s0.reports.empty() && (d0.reports[0].name != s0.reports[0].name ||
				d0.reports[0].confRev != s0.reports[0].confRev || d0.reports[0].trgOps.present != s0.reports[0].trgOps.present ||
				d0.reports[0].optFields.present != s0.reports[0].optFields.present)))
		{ printf("SKELETON diff: report\n"); diff++; }
		if (d0.gses.size() != s0.gses.size() ||
			(!s0.gses.empty() && (d0.gses[0].appId != s0.gses[0].appId || d0.gses[0].dataSet != s0.gses[0].dataSet)))
		{ printf("SKELETON diff: gseControl\n"); diff++; }
		if (d0.smvs.size() != s0.smvs.size() ||
			(!s0.smvs.empty() && (d0.smvs[0].smvId != s0.smvs[0].smvId || d0.smvs[0].smpRate != s0.smvs[0].smpRate ||
				d0.smvs[0].smpMod != s0.smvs[0].smpMod)))
		{ printf("SKELETON diff: smvControl\n"); diff++; }
		if (d0.logs.size() != s0.logs.size() ||
			(!s0.logs.empty() && (d0.logs[0].logName != s0.logs[0].logName || d0.logs[0].trgOps.present != s0.logs[0].trgOps.present)))
		{ printf("SKELETON diff: logControl\n"); diff++; }
		if (d0.settings.size() != s0.settings.size() ||
			(!s0.settings.empty() && d0.settings[0].numOfSG != s0.settings[0].numOfSG))
		{ printf("SKELETON diff: settingControl\n"); diff++; }   // Ed1 仅 numOfSGs 落盘，name/datSet 为工程文件本地字段
	}
	// 通信段 GSE 地址回读比对
	if (reloaded.subnets.size() != model.subnets.size() ||
		reloaded.subnets[0].aps[0].gses.size() != model.subnets[0].aps[0].gses.size() ||
		reloaded.subnets[0].aps[0].gses[0].minTime != model.subnets[0].aps[0].gses[0].minTime ||
		reloaded.subnets[0].aps[0].pParams.size() != model.subnets[0].aps[0].pParams.size())
	{ printf("SKELETON diff: subnets/gse address\n"); diff++; }
	printf("SKELETON reread diff=%d services=%d pParams=%d\n", diff,
		(int)reloaded.ied.services.size(), (int)reloaded.ied.pParams.size());
	return (v.refsParsed && v.sgPoints > 0 && v.semanticErrors.empty() && diff == 0) ? 0 : 1;
}

// 界面主题：加载独立 QSS 文件（应用级，所有窗口含对话框统一生效）
// 查找顺序：exe 目录 → 上级目录（MSVC 构建 Debug/Release 布局）→ qrc 内置兜底。
// 修改 theme.qss 后重启程序即可生效，无需重新编译。
static void ApplyTheme(QApplication& app)
{
	app.setStyle("Fusion");
	QString qss;
	QStringList candidates;
	candidates << QCoreApplication::applicationDirPath() + "/theme.qss"
		<< QCoreApplication::applicationDirPath() + "/../theme.qss";
	for (int i = 0; i < candidates.size(); ++i)
	{
		QFile f(candidates[i]);
		if (f.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			qss = QString::fromUtf8(f.readAll());
			break;
		}
	}
	if (qss.isEmpty())
	{
		QFile f(":/ui/theme.qss");
		if (f.open(QIODevice::ReadOnly | QIODevice::Text))
			qss = QString::fromUtf8(f.readAll());
	}
	app.setStyleSheet(qss);
}

// --xsd：对指定 SCL 文件执行 XSD 校验
// @return 0=通过，1=流程错误，2=存在违规
static int RunXsd(const char* filePath)
{
	int argc = 1;
	char appname[] = "ieddesigner-xsd";
	char* argv[] = { appname, 0 };
	QCoreApplication app(argc, argv);

	FILE* f = fopen(filePath, "rb");
	if (!f) {
		printf("XSD FAIL open file: %s\n", filePath);
		return 1;
	}
	std::string text;
	char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		text.append(buf, n);
	fclose(f);

	std::string dir = XsdValidator::EnsureSchemasExtracted();
	if (dir.empty()) {
		printf("XSD FAIL extract schemas\n");
		return 1;
	}

	std::vector<XsdValidator::XsdError> errors;
	std::string errText;
	if (!XsdValidator::Validate(text, dir, errors, errText)) {
		printf("XSD FAIL: %s\n", errText.c_str());
		return 1;
	}
	if (errors.empty()) {
		printf("XSD PASS: no schema violations\n");
		return 0;
	}
	printf("XSD VIOLATIONS: %d\n", (int)errors.size());
	for (size_t i = 0; i < errors.size(); ++i)
		printf("  line %d: %s\n", errors[i].line, errors[i].message.c_str());
	return 2;
}

int main(int argc, char** argv)
{
	// xsd文件检查
	if (argc >= 3 && strcmp(argv[1], "--xsd") == 0)
		return RunXsd(argv[2]);
	if (argc >= 4 && strcmp(argv[1], "--selftest") == 0)
		return RunSelfTest(argv[2], argv[3]);
	if (argc >= 3 && strcmp(argv[1], "--skeleton") == 0)
		return RunSkeleton(argv[2]);
	if (argc >= 3 && strcmp(argv[1], "--opentest") == 0)
		return RunOpenTest(argv[2]);
	if (argc >= 3 && strcmp(argv[1], "--autopen") == 0)
		return RunAutoOpen(argv[2]);
	if (argc >= 3 && strcmp(argv[1], "--roundtrip") == 0)
		return RunRoundTrip(argv[2]);
	if (argc >= 3 && strcmp(argv[1], "--jsonimport") == 0)
		return RunJsonImport(argv[2]);
	if (argc >= 4 && strcmp(argv[1], "--online") == 0)
		return RunOnline(argv[2], atoi(argv[3]), argc >= 5 ? argv[4] : "");

	QApplication app(argc, argv);
	SetupCodecs();
	QApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/plugins");
	ApplyTheme(app);
	QTranslator translator;
	SetupTranslation(app, translator);

	FreeConsole();
	MainWindow w;
	w.show();
	return app.exec();
}
