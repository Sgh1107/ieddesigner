#include <QApplication>
#include <QTextCodec>
#include <QString>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "mainwindow.h"
#include "sclmodel.h"
#include "sclwriter.h"
#include "sclreader.h"
#include "jsonmodel.h"
#include "browsemodel.h"
#include "onlineclient.h"
#include "csvwriter.h"
#include "modelvalidate.h"

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
	printf("SELFTEST csv=%s\n", csvOk ? "ok" : err.c_str());
	remove(temp.c_str());
	return (out.refsParsed && csvOk) ? 0 : 1;
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

	bool same = orig.table.diRefs.size() == rt.table.diRefs.size() &&
		orig.table.aiRefs.size() == rt.table.aiRefs.size() &&
		orig.table.doRefs.size() == rt.table.doRefs.size() &&
		orig.table.aoRefs.size() == rt.table.aoRefs.size() &&
		orig.table.sgRefs.size() == rt.table.sgRefs.size();
	printf("ROUNDTRIP same=%d\n", same ? 1 : 0);
	return (same && rereadOk && orig.refsParsed) ? 0 : 1;
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

int main(int argc, char** argv)
{
	QTextCodec* gbk = QTextCodec::codecForName("GBK");
	QTextCodec::setCodecForLocale(gbk);
	QTextCodec::setCodecForTr(gbk);
	QTextCodec::setCodecForCStrings(gbk);

	if (argc >= 4 && strcmp(argv[1], "--selftest") == 0)
		return RunSelfTest(argv[2], argv[3]);
	if (argc >= 3 && strcmp(argv[1], "--roundtrip") == 0)
		return RunRoundTrip(argv[2]);
	if (argc >= 3 && strcmp(argv[1], "--jsonimport") == 0)
		return RunJsonImport(argv[2]);
	if (argc >= 4 && strcmp(argv[1], "--online") == 0)
		return RunOnline(argv[2], atoi(argv[3]), argc >= 5 ? argv[4] : "");

	QApplication app(argc, argv);
	FreeConsole();
	MainWindow w;
	w.show();
	return app.exec();
}
