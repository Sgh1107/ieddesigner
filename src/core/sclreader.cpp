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

	static SclLn BuildLn(ReadContext& ctx, const QDomElement& lnElem, bool isLn0)
	{
		SclLn ln;
		ln.lnClass = lnElem.attribute("lnClass").toUtf8().constData();
		ln.inst = lnElem.attribute("inst").toUtf8().constData();
		ln.prefix = lnElem.attribute("prefix").toUtf8().constData();
		std::string lnType = lnElem.attribute("lnType").toUtf8().constData();
		if (isLn0)
			ln.inst = "";

		std::map<std::string, std::string> doiDesc;
		std::vector<std::string> doiOrder;
		std::vector<QDomElement> dois = ChildrenByLocalName(lnElem, "DOI");
		for (size_t i = 0; i < dois.size(); ++i)
		{
			std::string doName = dois[i].attribute("name").toUtf8().constData();
			doiOrder.push_back(doName);
			std::vector<QDomElement> dais = ChildrenByLocalName(dois[i], "DAI");
			for (size_t d = 0; d < dais.size(); ++d)
			{
				if (dais[d].attribute("name") != QLatin1String("d"))
					continue;
			QDomElement val = ChildByLocalName(dais[d], "Val");
			if (!val.isNull())
				doiDesc[doName] = val.text().toUtf8().constData();
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
			ln.dos.push_back(dobj);
		}
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

		QDomElement comm = ChildByLocalName(root, "Communication");
		bool apFound = false;
		if (!comm.isNull())
		{
			std::vector<QDomElement> subnets = ChildrenByLocalName(comm, "SubNetwork");
			for (size_t s = 0; s < subnets.size() && !apFound; ++s)
			{
				std::vector<QDomElement> caps = ChildrenByLocalName(subnets[s], "ConnectedAP");
				for (size_t c = 0; c < caps.size(); ++c)
				{
					if (caps[c].attribute("iedName").toUtf8().constData() != iedName)
						continue;
					model.ied.apName = caps[c].attribute("apName").toUtf8().constData();
					apFound = true;
					QDomElement addr = ChildByLocalName(caps[c], "Address");
					if (addr.isNull())
						break;
					std::vector<QDomElement> ps = ChildrenByLocalName(addr, "P");
					for (size_t p = 0; p < ps.size(); ++p)
					{
						std::string type = ps[p].attribute("type").toUtf8().constData();
						std::string value = ps[p].text().toUtf8().constData();
						if (type == "IP") model.ied.ip = value;
						else if (type == "IP-SUBNET") model.ied.ipSubnet = value;
						else if (type == "IP-GATEWAY") model.ied.ipGateway = value;
						else if (type == "MMS-Port") model.ied.port = atoi(value.c_str());
					}
					break;
				}
			}
		}
		if (model.ied.apName.empty())
			model.ied.apName = "accessPoint1";

		QDomElement ap = ChildByLocalName(iedElem, "AccessPoint");
		if (!ap.isNull() && !ap.attribute("name").isEmpty())
			model.ied.apName = ap.attribute("name").toUtf8().constData();
		if (ap.isNull())
		{
			errorText = "access point not found in ied";
			return false;
		}
		QDomElement server = ChildByLocalName(ap, "Server");
		if (server.isNull())
		{
			errorText = "server not found in access point";
			return false;
		}

		std::vector<QDomElement> lds = ChildrenByLocalName(server, "LDevice");
		for (size_t i = 0; i < lds.size(); ++i)
		{
			SclLDevice ld;
			ld.inst = lds[i].attribute("inst").toUtf8().constData();
			ld.ldName = lds[i].attribute("ldName").toUtf8().constData();
			if (ld.ldName.empty())
				ld.ldName = ld.inst;

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
