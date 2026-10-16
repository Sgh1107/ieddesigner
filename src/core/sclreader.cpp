#include "sclreader.h"
#include "scltemplates.h"

#include <QFile>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QString>
#include <QStringList>
#include <map>
#include <set>
#include <stdlib.h>

namespace SclReader
{
	struct TypeTables
	{
		std::map<std::string, std::vector<std::pair<std::string, std::string> > > lnTypeDos;
		std::map<std::string, std::string> doTypeCdc;
		std::map<std::string, std::vector<SclDa> > doTypeDas;
		std::map<std::string, std::vector<SclDa> > daTypeBdas;
		std::map<std::string, std::vector<std::pair<int, std::string> > > enumVals;
	};

	static QDomElement ChildByLocalName(const QDomElement& parent, const char* localName)
	{
		QDomNode node = parent.firstChild();
		while (!node.isNull())
		{
			if (node.isElement() && node.toElement().localName() == QLatin1String(localName))
				return node.toElement();
			node = node.nextSibling();
		}
		return QDomElement();
	}

	static std::vector<QDomElement> ChildrenByLocalName(const QDomElement& parent, const char* localName)
	{
		std::vector<QDomElement> list;
		QDomNode node = parent.firstChild();
		while (!node.isNull())
		{
			if (node.isElement() && node.toElement().localName() == QLatin1String(localName))
				list.push_back(node.toElement());
			node = node.nextSibling();
		}
		return list;
	}

	static bool LoadDoc(const std::string& path, QDomDocument& doc, std::string& errorText)
	{
		QFile f(QString::fromUtf8(path.c_str()));
		if (!f.open(QIODevice::ReadOnly))
		{
			errorText = "open scl file failed: " + path;
			return false;
		}
		QByteArray data = f.readAll();
		f.close();
		QString err;
		int line = 0;
		if (!doc.setContent(data, true, &err, &line))
		{
			errorText = std::string("xml parse failed at line ") + QString::number(line).toUtf8().constData() + ": " + err.toUtf8().constData();
			return false;
		}
		return true;
	}

	static void ParseTypeTables(const QDomElement& root, TypeTables& tables)
	{
		QDomElement templates = ChildByLocalName(root, "DataTypeTemplates");
		if (templates.isNull())
			return;

		std::vector<QDomElement> lnTypes = ChildrenByLocalName(templates, "LNodeType");
		for (size_t i = 0; i < lnTypes.size(); ++i)
		{
			std::string id = lnTypes[i].attribute("id").toUtf8().constData();
			std::vector<QDomElement> dos = ChildrenByLocalName(lnTypes[i], "DO");
			for (size_t d = 0; d < dos.size(); ++d)
			{
				std::string name = dos[d].attribute("name").toUtf8().constData();
				std::string type = dos[d].attribute("type").toUtf8().constData();
				if (!name.empty() && !type.empty())
					tables.lnTypeDos[id].push_back(std::make_pair(name, type));
			}
		}

		std::vector<QDomElement> doTypes = ChildrenByLocalName(templates, "DOType");
		for (size_t i = 0; i < doTypes.size(); ++i)
		{
			std::string id = doTypes[i].attribute("id").toUtf8().constData();
			tables.doTypeCdc[id] = doTypes[i].attribute("cdc").toUtf8().constData();
			std::vector<QDomElement> das = ChildrenByLocalName(doTypes[i], "DA");
			for (size_t d = 0; d < das.size(); ++d)
			{
				SclDa da;
				da.name = das[d].attribute("name").toUtf8().constData();
				da.bType = das[d].attribute("bType").toUtf8().constData();
				da.fc = das[d].attribute("fc").toUtf8().constData();
				da.structType = das[d].attribute("type").toUtf8().constData();
				if (!da.name.empty())
					tables.doTypeDas[id].push_back(da);
			}
		}

		std::vector<QDomElement> daTypes = ChildrenByLocalName(templates, "DAType");
		for (size_t i = 0; i < daTypes.size(); ++i)
		{
			std::string id = daTypes[i].attribute("id").toUtf8().constData();
			std::vector<QDomElement> bdas = ChildrenByLocalName(daTypes[i], "BDA");
			for (size_t d = 0; d < bdas.size(); ++d)
			{
				SclDa bda;
				bda.name = bdas[d].attribute("name").toUtf8().constData();
				bda.bType = bdas[d].attribute("bType").toUtf8().constData();
				bda.structType = bdas[d].attribute("type").toUtf8().constData();
				if (!bda.name.empty())
					tables.daTypeBdas[id].push_back(bda);
			}
		}

		std::vector<QDomElement> enums = ChildrenByLocalName(templates, "EnumType");
		for (size_t i = 0; i < enums.size(); ++i)
		{
			std::string id = enums[i].attribute("id").toUtf8().constData();
			std::vector<QDomElement> vals = ChildrenByLocalName(enums[i], "EnumVal");
			for (size_t v = 0; v < vals.size(); ++v)
			{
				int ord = vals[v].attribute("ord").toInt();
				tables.enumVals[id].push_back(std::make_pair(ord, vals[v].text().toUtf8().constData()));
			}
		}
	}

	static std::vector<std::string> CollectIedNames(const QDomElement& root)
	{
		std::vector<std::string> names;
		std::vector<QDomElement> ieds = ChildrenByLocalName(root, "IED");
		for (size_t i = 0; i < ieds.size(); ++i)
			names.push_back(ieds[i].attribute("name").toUtf8().constData());
		return names;
	}

	std::vector<std::string> ListIeds(const std::string& path, std::string& errorText)
	{
		QDomDocument doc;
		if (!LoadDoc(path, doc, errorText))
			return std::vector<std::string>();
		return CollectIedNames(doc.documentElement());
	}

	struct ReadContext
	{
		const TypeTables* types;
		SclModel* model;
		std::set<std::string> extraDoTypeIds;
		std::set<std::string> extraDaTypeIds;
		std::set<std::string> extraEnumIds;
	};

	static void RequireEnum(ReadContext& ctx, const std::string& id)
	{
		if (id.empty() || ctx.extraEnumIds.count(id))
			return;
		ctx.extraEnumIds.insert(id);
		std::map<std::string, std::vector<std::pair<int, std::string> > >::const_iterator it =
			ctx.types->enumVals.find(id);
		if (it == ctx.types->enumVals.end())
			return;
		SclExtraEnumType e;
		e.id = id;
		e.vals = it->second;
		ctx.model->extraEnumTypes.push_back(e);
	}

	static void RequireDaType(ReadContext& ctx, const std::string& id)
	{
		if (id.empty() || ctx.extraDaTypeIds.count(id))
			return;
		ctx.extraDaTypeIds.insert(id);
		std::map<std::string, std::vector<SclDa> >::const_iterator it = ctx.types->daTypeBdas.find(id);
		if (it == ctx.types->daTypeBdas.end())
			return;
		SclExtraDaType t;
		t.id = id;
		t.bdas = it->second;
		ctx.model->extraDaTypes.push_back(t);
		for (size_t i = 0; i < it->second.size(); ++i)
		{
			const SclDa& bda = it->second[i];
			if (bda.bType == "Enum")
				RequireEnum(ctx, bda.structType);
		}
	}

	static void RequireDoType(ReadContext& ctx, const std::string& id)
	{
		if (id.empty() || ctx.extraDoTypeIds.count(id))
			return;
		ctx.extraDoTypeIds.insert(id);
		std::map<std::string, std::string>::const_iterator itCdc = ctx.types->doTypeCdc.find(id);
		std::map<std::string, std::vector<SclDa> >::const_iterator itDas = ctx.types->doTypeDas.find(id);
		if (itCdc == ctx.types->doTypeCdc.end() || itDas == ctx.types->doTypeDas.end())
			return;
		SclExtraDoType t;
		t.id = id;
		t.cdc = itCdc->second;
		t.das = itDas->second;
		ctx.model->extraDoTypes.push_back(t);
		for (size_t i = 0; i < t.das.size(); ++i)
		{
			const SclDa& da = t.das[i];
			if (da.bType == "Struct")
			{
				if (ctx.types->doTypeCdc.count(da.structType))
					RequireDoType(ctx, da.structType);
				else
					RequireDaType(ctx, da.structType);
			}
			else if (da.bType == "Enum")
			{
				RequireEnum(ctx, da.structType);
			}
		}
	}

	static void RequireDaRefs(ReadContext& ctx, const std::vector<SclDa>& das)
	{
		for (size_t i = 0; i < das.size(); ++i)
		{
			const SclDa& da = das[i];
			if (da.bType == "Struct")
			{
				if (ctx.types->doTypeCdc.count(da.structType))
					RequireDoType(ctx, da.structType);
				else
					RequireDaType(ctx, da.structType);
			}
			else if (da.bType == "Enum")
			{
				RequireEnum(ctx, da.structType);
			}
		}
	}

	// 递归收集 DAI/SDI 下的 Val，Struct 展开为点分路径（如 "mag.f"）
	static void CollectDaiVals(const QDomElement& container, const std::string& prefix,
		std::vector<SclDai>& out)
	{
		std::vector<QDomElement> vals = ChildrenByLocalName(container, "Val");
		if (!vals.empty())
		{
			out.push_back(SclDai(prefix, vals[0].text().toUtf8().constData()));
			return;
		}
		std::vector<QDomElement> sdis = ChildrenByLocalName(container, "SDI");
		for (size_t i = 0; i < sdis.size(); ++i)
		{
			std::string sdiName = sdis[i].attribute("name").toUtf8().constData();
			std::string path = prefix.empty() ? sdiName : prefix + "." + sdiName;
			CollectDaiVals(sdis[i], path, out);
		}
	}

	static SclTrgOps ParseTrgOps(const QDomElement& parent)
	{
		SclTrgOps ops;
		QDomElement t = ChildByLocalName(parent, "TrgOps");
		if (!t.isNull())
		{
			ops.present = true;
			ops.dchg = t.attribute("dchg") == QLatin1String("true");
			ops.qchg = t.attribute("qchg") == QLatin1String("true");
			ops.dupd = t.attribute("dupd") == QLatin1String("true");
			ops.period = t.attribute("period") == QLatin1String("true");
			ops.gi = t.attribute("gi") == QLatin1String("true");
		}
		return ops;
	}

	static SclOptFields ParseOptFields(const QDomElement& parent)
	{
		SclOptFields opt;
		QDomElement t = ChildByLocalName(parent, "OptFields");
		if (!t.isNull())
		{
			opt.present = true;
			opt.seqNum = t.attribute("seqNum") == QLatin1String("true");
			opt.timeStamp = t.attribute("timeStamp") == QLatin1String("true");
			opt.dataSet = t.attribute("dataSet") == QLatin1String("true");
			opt.reasonCode = t.attribute("reasonCode") == QLatin1String("true");
			opt.dataRef = t.attribute("dataRef") == QLatin1String("true");
			opt.bufOvfl = t.attribute("bufOvfl") == QLatin1String("true");
			opt.entryID = t.attribute("entryID") == QLatin1String("true");
			opt.config = t.attribute("config") == QLatin1String("true");
		}
		return opt;
	}

	// 解析 LN 下的数据集与各类控制块
	static void ParseLnControlBlocks(const QDomElement& lnElem, SclLn& ln)
	{
		std::vector<QDomElement> dss = ChildrenByLocalName(lnElem, "DataSet");
		for (size_t i = 0; i < dss.size(); ++i)
		{
			SclDataSet ds;
			ds.name = dss[i].attribute("name").toUtf8().constData();
			ds.desc = dss[i].attribute("desc").toUtf8().constData();
			std::vector<QDomElement> fcdas = ChildrenByLocalName(dss[i], "FCDA");
			for (size_t k = 0; k < fcdas.size(); ++k)
			{
				SclFcda f;
				f.iedName = fcdas[k].attribute("iedName").toUtf8().constData();
				f.ldInst = fcdas[k].attribute("ldInst").toUtf8().constData();
				f.prefix = fcdas[k].attribute("prefix").toUtf8().constData();
				f.lnClass = fcdas[k].attribute("lnClass").toUtf8().constData();
				f.lnInst = fcdas[k].attribute("lnInst").toUtf8().constData();
				f.doName = fcdas[k].attribute("doName").toUtf8().constData();
				f.daName = fcdas[k].attribute("daName").toUtf8().constData();
				f.fc = fcdas[k].attribute("fc").toUtf8().constData();
				ds.fcdas.push_back(f);
			}
			ln.dataSets.push_back(ds);
		}

		std::vector<QDomElement> rcs = ChildrenByLocalName(lnElem, "ReportControl");
		for (size_t i = 0; i < rcs.size(); ++i)
		{
			SclReportControl rc;
			rc.name = rcs[i].attribute("name").toUtf8().constData();
			rc.rptId = rcs[i].attribute("rptID").toUtf8().constData();
			rc.dataSet = rcs[i].attribute("datSet").toUtf8().constData();
			rc.buffered = rcs[i].attribute("buffered") == QLatin1String("true");
			rc.bufTime = rcs[i].attribute("bufTime").toUtf8().constData();
			rc.intgPd = rcs[i].attribute("intgPd").toUtf8().constData();
			rc.indexed = rcs[i].attribute("indexed") != QLatin1String("false");
			rc.maxClients = -1;
			QDomElement rptEn = ChildByLocalName(rcs[i], "RptEnabled");
			if (!rptEn.isNull())
			{
				std::string maxAttr = rptEn.attribute("max").toUtf8().constData();
				if (!maxAttr.empty())
					rc.maxClients = atoi(maxAttr.c_str());
			}
			QDomElement confRev = ChildByLocalName(rcs[i], "ConfRev");
			if (!confRev.isNull())
				rc.confRev = confRev.text().toUtf8().constData();
			else
				rc.confRev = rcs[i].attribute("confRev").toUtf8().constData();
			rc.trgOps = ParseTrgOps(rcs[i]);
			rc.optFields = ParseOptFields(rcs[i]);
			ln.reports.push_back(rc);
		}

		std::vector<QDomElement> gcs = ChildrenByLocalName(lnElem, "GSEControl");
		for (size_t i = 0; i < gcs.size(); ++i)
		{
			SclGseControl gc;
			gc.name = gcs[i].attribute("name").toUtf8().constData();
			gc.appId = gcs[i].attribute("appID").toUtf8().constData();
			gc.dataSet = gcs[i].attribute("datSet").toUtf8().constData();
			gc.fixedOffs = gcs[i].attribute("fixedOffs") == QLatin1String("true");
			QDomElement confRev = ChildByLocalName(gcs[i], "ConfRev");
			if (!confRev.isNull())
				gc.confRev = confRev.text().toUtf8().constData();
			else
				gc.confRev = gcs[i].attribute("confRev").toUtf8().constData();
			ln.gses.push_back(gc);
		}

		std::vector<QDomElement> scs = ChildrenByLocalName(lnElem, "SampledValueControl");
		for (size_t i = 0; i < scs.size(); ++i)
		{
			SclSampledValueControl sc;
			sc.name = scs[i].attribute("name").toUtf8().constData();
			sc.smvId = scs[i].attribute("smvID").toUtf8().constData();
			sc.dataSet = scs[i].attribute("datSet").toUtf8().constData();
			sc.smpRate = scs[i].attribute("smpRate").toUtf8().constData();
			sc.nofASDU = scs[i].attribute("nofASDU").toUtf8().constData();
			sc.smpMod = scs[i].attribute("smpMod").toUtf8().constData();
			QDomElement confRev = ChildByLocalName(scs[i], "ConfRev");
			if (!confRev.isNull())
				sc.confRev = confRev.text().toUtf8().constData();
			else
				sc.confRev = scs[i].attribute("confRev").toUtf8().constData();
			if (sc.smpRate.empty()) sc.smpRate = "80";
			if (sc.nofASDU.empty()) sc.nofASDU = "1";
			if (sc.smpMod.empty()) sc.smpMod = "S";
			ln.smvs.push_back(sc);
		}

		std::vector<QDomElement> lcs = ChildrenByLocalName(lnElem, "LogControl");
		for (size_t i = 0; i < lcs.size(); ++i)
		{
			SclLogControl lc;
			lc.name = lcs[i].attribute("name").toUtf8().constData();
			lc.dataSet = lcs[i].attribute("datSet").toUtf8().constData();
			lc.logName = lcs[i].attribute("logName").toUtf8().constData();
			lc.intgPd = lcs[i].attribute("intgPd").toUtf8().constData();
			lc.trgOps = ParseTrgOps(lcs[i]);
			ln.logs.push_back(lc);
		}

		std::vector<QDomElement> scbs = ChildrenByLocalName(lnElem, "SettingControl");
		for (size_t i = 0; i < scbs.size(); ++i)
		{
			SclSettingControl scb;
			scb.name = scbs[i].attribute("name").toUtf8().constData();
			scb.dataSet = scbs[i].attribute("datSet").toUtf8().constData();
			scb.numOfSG = scbs[i].attribute("numOfSGs").toUtf8().constData();
			ln.settings.push_back(scb);
		}
	}

	static SclLn BuildLn(ReadContext& ctx, const QDomElement& lnElem, bool isLn0)
	{
		SclLn ln;
		ln.lnClass = lnElem.attribute("lnClass").toUtf8().constData();
		ln.inst = lnElem.attribute("inst").toUtf8().constData();
		ln.prefix = lnElem.attribute("prefix").toUtf8().constData();
		ln.desc = lnElem.attribute("desc").toUtf8().constData();
		std::string lnType = lnElem.attribute("lnType").toUtf8().constData();
		if (isLn0)
			ln.inst = "";

		// LN 注释
		{
			QDomNode cnode = lnElem.firstChild();
			while (!cnode.isNull())
			{
				if (cnode.isComment())
				{
					ln.comment = cnode.toComment().data().toUtf8().constData();
					break;
				}
				cnode = cnode.nextSibling();
			}
		}

		// 外部信号引用 <Inputs><ExtRef/></Inputs>（LN0/LN 通用）
		{
			std::vector<QDomElement> inputsList = ChildrenByLocalName(lnElem, "Inputs");
			for (size_t i = 0; i < inputsList.size(); ++i)
			{
				std::vector<QDomElement> ers = ChildrenByLocalName(inputsList[i], "ExtRef");
				for (size_t k = 0; k < ers.size(); ++k)
				{
					SclExtRef er;
					er.iedName = ers[k].attribute("iedName").toUtf8().constData();
					er.ldInst = ers[k].attribute("ldInst").toUtf8().constData();
					er.prefix = ers[k].attribute("prefix").toUtf8().constData();
					er.lnClass = ers[k].attribute("lnClass").toUtf8().constData();
					er.lnInst = ers[k].attribute("lnInst").toUtf8().constData();
					er.doName = ers[k].attribute("doName").toUtf8().constData();
					er.daName = ers[k].attribute("daName").toUtf8().constData();
					er.desc = ers[k].attribute("desc").toUtf8().constData();
					er.intAddr = ers[k].attribute("intAddr").toUtf8().constData();
					ln.extRefs.push_back(er);
				}
			}
		}

		std::map<std::string, std::string> doiDesc;
		std::map<std::string, std::vector<SclDai> > doiDais;
		std::vector<std::string> doiOrder;
		std::vector<QDomElement> dois = ChildrenByLocalName(lnElem, "DOI");
		for (size_t i = 0; i < dois.size(); ++i)
		{
			std::string doName = dois[i].attribute("name").toUtf8().constData();
			doiOrder.push_back(doName);
			std::vector<QDomElement> dais = ChildrenByLocalName(dois[i], "DAI");
			for (size_t d = 0; d < dais.size(); ++d)
			{
				std::string daiName = dais[d].attribute("name").toUtf8().constData();
				if (daiName == "d")
				{
					QDomElement val = ChildByLocalName(dais[d], "Val");
					if (!val.isNull())
						doiDesc[doName] = val.text().toUtf8().constData();
					continue;
				}
				CollectDaiVals(dais[d], daiName, doiDais[doName]);
			}
		}

		std::map<std::string, std::vector<std::pair<std::string, std::string> > >::const_iterator itDos =
			ctx.types->lnTypeDos.find(lnType);
		std::set<std::string> added;
		if (itDos != ctx.types->lnTypeDos.end())
		{
			for (size_t i = 0; i < itDos->second.size(); ++i)
			{
				const std::string& doName = itDos->second[i].first;
				const std::string& doTypeId = itDos->second[i].second;
				std::string cdc;
				std::map<std::string, std::string>::const_iterator itCdc = ctx.types->doTypeCdc.find(doTypeId);
				if (itCdc != ctx.types->doTypeCdc.end())
					cdc = itCdc->second;
				std::vector<SclDa> das;
				std::map<std::string, std::vector<SclDa> >::const_iterator itDas = ctx.types->doTypeDas.find(doTypeId);
				if (itDas != ctx.types->doTypeDas.end())
					das = itDas->second;

				std::string stValEnum;
				for (size_t d = 0; d < das.size(); ++d)
				{
					if (das[d].name == "stVal" && das[d].bType == "Enum")
						stValEnum = das[d].structType;
				}

				SclDo dobj(doName, cdc, stValEnum);
				if (!stValEnum.empty())
					RequireEnum(ctx, stValEnum);
				bool inSubset = !SclTemplates::DasForCdc(cdc, stValEnum).empty();
				if (!inSubset && !das.empty())
				{
					dobj.hasCustomDas = true;
					dobj.customDoTypeId = doTypeId;
					dobj.customDas = das;
					RequireDaRefs(ctx, das);
				}
				std::map<std::string, std::string>::const_iterator itDesc = doiDesc.find(doName);
				if (itDesc != doiDesc.end())
					dobj.desc = itDesc->second;
				std::map<std::string, std::vector<SclDai> >::const_iterator itDais = doiDais.find(doName);
				if (itDais != doiDais.end())
					dobj.dais = itDais->second;
				ln.dos.push_back(dobj);
				added.insert(doName);
			}
		}

		for (size_t i = 0; i < doiOrder.size(); ++i)
		{
			if (added.count(doiOrder[i]))
				continue;
			SclDo dobj(doiOrder[i], "", "");
			dobj.desc = doiDesc[doiOrder[i]];
			std::map<std::string, std::vector<SclDai> >::const_iterator itDais2 = doiDais.find(doiOrder[i]);
			if (itDais2 != doiDais.end())
				dobj.dais = itDais2->second;
			ln.dos.push_back(dobj);
		}

		ParseLnControlBlocks(lnElem, ln);
		return ln;
	}

	bool ReadSclFile(const std::string& path, const std::string& iedName,
		SclModel& model, std::string& errorText)
	{
		QDomDocument doc;
		if (!LoadDoc(path, doc, errorText))
			return false;
		QDomElement root = doc.documentElement();

		TypeTables tables;
		ParseTypeTables(root, tables);
		// 把文件自定义 DAType 注入模板库：Struct DA 展开时才能查到 BDA 层级
		SclTemplates::RegisterFileDaTypes(tables.daTypeBdas);

		std::vector<QDomElement> ieds = ChildrenByLocalName(root, "IED");
		QDomElement iedElem;
		for (size_t i = 0; i < ieds.size(); ++i)
		{
			if (ieds[i].attribute("name").toUtf8().constData() == iedName)
				iedElem = ieds[i];
		}
		if (iedElem.isNull())
		{
			errorText = "ied not found in scl: " + iedName;
			return false;
		}

		model = SclModel();
		ReadContext ctx;
		ctx.types = &tables;
		ctx.model = &model;

		model.ied.name = iedName;

		// Header 修订历史（id 一并读回，避免保存时被重置为默认值）
		QDomElement headerElem = ChildByLocalName(root, "Header");
		if (!headerElem.isNull())
		{
			std::string headerIdAttr = headerElem.attribute("id").toUtf8().constData();
			if (!headerIdAttr.empty())
				model.headerId = headerIdAttr;
			QDomElement hist = ChildByLocalName(headerElem, "History");
			if (!hist.isNull())
			{
				std::vector<QDomElement> hitems = ChildrenByLocalName(hist, "Hitem");
				for (size_t i = 0; i < hitems.size(); ++i)
				{
					SclHistoryItem item;
					item.version = hitems[i].attribute("version").toUtf8().constData();
					item.what = hitems[i].attribute("what").toUtf8().constData();
					item.who = hitems[i].attribute("who").toUtf8().constData();
					item.when = hitems[i].attribute("when").toUtf8().constData();
					item.why = hitems[i].attribute("why").toUtf8().constData();
					model.history.push_back(item);
				}
			}
		}

		// Communication 全量解析（子网/接入点/GSE/SMV 地址）
		QDomElement comm = ChildByLocalName(root, "Communication");
		bool apFound = false;
		if (!comm.isNull())
		{
			std::vector<QDomElement> subnets = ChildrenByLocalName(comm, "SubNetwork");
			for (size_t s = 0; s < subnets.size(); ++s)
			{
				SclSubNetwork sn;
				sn.name = subnets[s].attribute("name").toUtf8().constData();
				sn.type = subnets[s].attribute("type").toUtf8().constData();
				std::vector<QDomElement> caps = ChildrenByLocalName(subnets[s], "ConnectedAP");
				for (size_t c = 0; c < caps.size(); ++c)
				{
					SclConnectedAP cap;
					cap.apName = caps[c].attribute("apName").toUtf8().constData();
					cap.iedName = caps[c].attribute("iedName").toUtf8().constData();
					QDomElement addr = ChildByLocalName(caps[c], "Address");
					if (!addr.isNull())
					{
						std::vector<QDomElement> ps = ChildrenByLocalName(addr, "P");
						for (size_t p = 0; p < ps.size(); ++p)
						{
							std::string type = ps[p].attribute("type").toUtf8().constData();
							std::string value = ps[p].text().toUtf8().constData();
							cap.pParams.push_back(std::make_pair(type, value));
						}
					}
					std::vector<QDomElement> gses = ChildrenByLocalName(caps[c], "GSE");
					for (size_t g = 0; g < gses.size(); ++g)
					{
						SclCommAddress ga;
						ga.ldInst = gses[g].attribute("ldInst").toUtf8().constData();
						ga.cbName = gses[g].attribute("cbName").toUtf8().constData();
						QDomElement gaddr = ChildByLocalName(gses[g], "Address");
						if (!gaddr.isNull())
						{
							std::vector<QDomElement> gps = ChildrenByLocalName(gaddr, "P");
							for (size_t p = 0; p < gps.size(); ++p)
								ga.pParams.push_back(std::make_pair(
									gps[p].attribute("type").toUtf8().constData(),
									gps[p].text().toUtf8().constData()));
						}
						QDomElement minT = ChildByLocalName(gses[g], "MinTime");
						if (!minT.isNull())
							ga.minTime = minT.text().toUtf8().constData();
						QDomElement maxT = ChildByLocalName(gses[g], "MaxTime");
						if (!maxT.isNull())
							ga.maxTime = maxT.text().toUtf8().constData();
						cap.gses.push_back(ga);
					}
					std::vector<QDomElement> smvs = ChildrenByLocalName(caps[c], "SMV");
					for (size_t g = 0; g < smvs.size(); ++g)
					{
						SclCommAddress sa;
						sa.ldInst = smvs[g].attribute("ldInst").toUtf8().constData();
						sa.cbName = smvs[g].attribute("cbName").toUtf8().constData();
						QDomElement saddr = ChildByLocalName(smvs[g], "Address");
						if (!saddr.isNull())
						{
							std::vector<QDomElement> sps = ChildrenByLocalName(saddr, "P");
							for (size_t p = 0; p < sps.size(); ++p)
								sa.pParams.push_back(std::make_pair(
									sps[p].attribute("type").toUtf8().constData(),
									sps[p].text().toUtf8().constData()));
						}
						cap.smvs.push_back(sa);
					}
					sn.aps.push_back(cap);

					// 主接入点匹配（与既有逻辑一致）
					if (!apFound && cap.iedName == iedName)
					{
						model.ied.apName = cap.apName;
						apFound = true;
						for (size_t p = 0; p < cap.pParams.size(); ++p)
						{
							const std::string& type = cap.pParams[p].first;
							const std::string& value = cap.pParams[p].second;
							model.ied.pParams.push_back(std::make_pair(type, value));
							if (type == "IP") model.ied.ip = value;
							else if (type == "IP-SUBNET") model.ied.ipSubnet = value;
							else if (type == "IP-GATEWAY") model.ied.ipGateway = value;
							else if (type == "MMS-Port") model.ied.port = atoi(value.c_str());
						}
					}
				}
				model.subnets.push_back(sn);
			}
		}
		if (model.ied.apName.empty())
			model.ied.apName = "accessPoint1";

		// IED 属性
		model.ied.desc = iedElem.attribute("desc").toUtf8().constData();
		model.ied.manufacturer = iedElem.attribute("manufacturer").toUtf8().constData();
		model.ied.type = iedElem.attribute("type").toUtf8().constData();

		// IED 服务能力声明
		QDomElement services = ChildByLocalName(iedElem, "Services");
		if (!services.isNull())
		{
			model.ied.servicesNameLength = services.attribute("nameLength").toUtf8().constData();
			QDomNode snode = services.firstChild();
			while (!snode.isNull())
			{
				if (snode.isElement())
					model.ied.services.push_back(snode.toElement().tagName().toUtf8().constData());
				snode = snode.nextSibling();
			}
		}

		// 接入点：第一个含 Server 的 AccessPoint 为主接入点，其余进入 extraAps
		std::vector<QDomElement> aps = ChildrenByLocalName(iedElem, "AccessPoint");
		QDomElement ap;
		QDomElement server;
		for (size_t i = 0; i < aps.size(); ++i)
		{
			std::string apNameAttr = aps[i].attribute("name").toUtf8().constData();
			QDomElement apServer = ChildByLocalName(aps[i], "Server");
			if (server.isNull() && !apServer.isNull())
			{
				ap = aps[i];
				server = apServer;
				if (!apNameAttr.empty())
					model.ied.apName = apNameAttr;
			}
			else
			{
				SclAccessPoint extra;
				extra.name = apNameAttr;
				QDomElement serverAt = ChildByLocalName(aps[i], "ServerAt");
				if (!serverAt.isNull())
					extra.serverAt = serverAt.attribute("apName").toUtf8().constData();
				model.ied.extraAps.push_back(extra);
			}
		}
		if (ap.isNull())
		{
			errorText = "access point not found in ied";
			return false;
		}
		if (server.isNull())
		{
			errorText = "server not found in access point";
			return false;
		}

		// Server 属性
		{
			std::string timeoutAttr = server.attribute("timeout").toUtf8().constData();
			if (!timeoutAttr.empty())
				model.ied.serverTimeout = atoi(timeoutAttr.c_str());
			if (!ChildByLocalName(server, "Authentication").isNull())
				model.ied.authentication = true;
		}

		std::vector<QDomElement> lds = ChildrenByLocalName(server, "LDevice");
		for (size_t i = 0; i < lds.size(); ++i)
		{
			SclLDevice ld;
			ld.inst = lds[i].attribute("inst").toUtf8().constData();
			ld.desc = lds[i].attribute("desc").toUtf8().constData();
			ld.ldName = lds[i].attribute("ldName").toUtf8().constData();
			if (ld.ldName.empty())
				ld.ldName = ld.inst;

			// LDevice 注释
			QDomNode cnode = lds[i].firstChild();
			while (!cnode.isNull())
			{
				if (cnode.isComment())
				{
					ld.comment = cnode.toComment().data().toUtf8().constData();
					break;
				}
				cnode = cnode.nextSibling();
			}

			QDomElement ln0Elem = ChildByLocalName(lds[i], "LN0");
			if (!ln0Elem.isNull())
				ld.ln0 = BuildLn(ctx, ln0Elem, true);
			else
				ld.ln0 = SclTemplates::MakeLn("LLN0");

			std::vector<QDomElement> lns = ChildrenByLocalName(lds[i], "LN");
			for (size_t n = 0; n < lns.size(); ++n)
				ld.lns.push_back(BuildLn(ctx, lns[n], false));

			model.ied.lds.push_back(ld);
		}

		if (model.ied.lds.empty())
		{
			errorText = "no logical device found in ied";
			return false;
		}
		return true;
	}
}
