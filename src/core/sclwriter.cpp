#include "sclwriter.h"
#include "scltemplates.h"

#include <stdio.h>
#include <map>
#include <set>
#include <vector>
#include <QString>
#include <QFile>

namespace SclWriter
{
	static std::string XmlEscape(const std::string& utf8Text)
	{
		std::string out;
		for (size_t i = 0; i < utf8Text.size(); ++i)
		{
			char c = utf8Text[i];
			switch (c)
			{
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			default: out += c; break;
			}
		}
		return out;
	}

	static std::string DoTypeId(const std::string& cdc, const std::string& stValEnum)
	{
		std::string id = cdc + "_";
		if (!stValEnum.empty())
			id += stValEnum + "_";
		id += "1";
		return id;
	}

	struct UsedDoType
	{
		std::string id;
		std::string cdc;
		std::string stValEnum;
		std::vector<SclDa> das;
		bool custom;
	};

	std::string BuildSclText(const SclModel& model, bool withCommunication)
	{
		const SclIed& ied = model.ied;
		std::string nl = "\r\n";
		std::string text;
		text += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + nl;
		text += "<SCL xmlns=\"http://www.iec.ch/61850/2003/SCL\">" + nl;
		text += "  <Header id=\"" + XmlEscape(model.headerId) + "\" nameStructure=\"IEDName\">" + nl;
		text += "  </Header>" + nl;

		if (withCommunication)
		{
			char portBuf[32];
			sprintf(portBuf, "%d", ied.port);
			text += "  <Communication>" + nl;
			text += "    <SubNetwork name=\"subnetwork1\" type=\"8-MMS\">" + nl;
			text += "      <ConnectedAP iedName=\"" + XmlEscape(ied.name) + "\" apName=\"" + XmlEscape(ied.apName) + "\">" + nl;
			text += "        <Address>" + nl;
			text += "          <P type=\"IP\">" + XmlEscape(ied.ip) + "</P>" + nl;
			text += "          <P type=\"IP-SUBNET\">" + XmlEscape(ied.ipSubnet) + "</P>" + nl;
			text += "          <P type=\"IP-GATEWAY\">" + XmlEscape(ied.ipGateway) + "</P>" + nl;
			text += "          <P type=\"MMS-Port\">" + std::string(portBuf) + "</P>" + nl;
			text += "        </Address>" + nl;
			text += "      </ConnectedAP>" + nl;
			text += "    </SubNetwork>" + nl;
			text += "  </Communication>" + nl;
		}

		std::map<std::string, std::string> lnTypeIdByClass;
		std::map<std::string, UsedDoType> usedDoTypes;
		std::map<std::string, std::vector<const SclDo*> > dosByClass;

		for (size_t li = 0; li < ied.lds.size(); ++li)
		{
			const SclLDevice& ld = ied.lds[li];
			std::vector<const SclLn*> allLns;
			allLns.push_back(&ld.ln0);
			for (size_t i = 0; i < ld.lns.size(); ++i)
				allLns.push_back(&ld.lns[i]);
			for (size_t i = 0; i < allLns.size(); ++i)
			{
				const SclLn& ln = *allLns[i];
				if (lnTypeIdByClass.find(ln.lnClass) == lnTypeIdByClass.end())
					lnTypeIdByClass[ln.lnClass] = ln.lnClass + "_1";
				std::vector<const SclDo*>& classDos = dosByClass[ln.lnClass];
				for (size_t d = 0; d < ln.dos.size(); ++d)
				{
					const SclDo& dobj = ln.dos[d];
					bool dup = false;
					for (size_t k = 0; k < classDos.size(); ++k)
					{
						if (classDos[k]->name == dobj.name)
							dup = true;
					}
					if (!dup)
						classDos.push_back(&dobj);

					std::string id = dobj.hasCustomDas ? dobj.customDoTypeId : DoTypeId(dobj.cdc, dobj.stValEnum);
					if (usedDoTypes.find(id) == usedDoTypes.end())
					{
						UsedDoType u;
						u.id = id;
						u.cdc = dobj.cdc;
						u.stValEnum = dobj.stValEnum;
						u.custom = dobj.hasCustomDas;
						u.das = dobj.customDas;
						usedDoTypes[id] = u;
					}
				}
			}
		}

		text += "  <IED name=\"" + XmlEscape(ied.name) + "\">" + nl;
		text += "    <AccessPoint name=\"" + XmlEscape(ied.apName) + "\">" + nl;
		text += "      <Server>" + nl;

		for (size_t li = 0; li < ied.lds.size(); ++li)
		{
			const SclLDevice& ld = ied.lds[li];
			text += "        <LDevice inst=\"" + XmlEscape(ld.inst) + "\">" + nl;

			std::vector<const SclLn*> allLns;
			allLns.push_back(&ld.ln0);
			for (size_t i = 0; i < ld.lns.size(); ++i)
				allLns.push_back(&ld.lns[i]);

			for (size_t i = 0; i < allLns.size(); ++i)
			{
				const SclLn& ln = *allLns[i];
				std::string lnTypeId = lnTypeIdByClass[ln.lnClass];
				std::string open;
				if (i == 0)
					open = "          <LN0 lnClass=\"" + XmlEscape(ln.lnClass) + "\" inst=\"\" lnType=\"" + lnTypeId + "\">";
				else
					open = "          <LN lnClass=\"" + XmlEscape(ln.lnClass) + "\" inst=\"" + XmlEscape(ln.inst) +
					       "\" prefix=\"" + XmlEscape(ln.prefix) + "\" lnType=\"" + lnTypeId + "\">";
				text += open + nl;
				for (size_t d = 0; d < ln.dos.size(); ++d)
				{
					const SclDo& dobj = ln.dos[d];
					if (dobj.desc.empty())
						continue;
					text += "            <DOI name=\"" + XmlEscape(dobj.name) + "\">" + nl;
					text += "              <DAI name=\"d\">" + nl;
					text += "                <Val>" + XmlEscape(dobj.desc) + "</Val>" + nl;
					text += "              </DAI>" + nl;
					text += "            </DOI>" + nl;
				}
				if (i == 0)
					text += "          </LN0>" + nl;
				else
					text += "          </LN>" + nl;
			}
			text += "        </LDevice>" + nl;
		}

		text += "      </Server>" + nl;
		text += "    </AccessPoint>" + nl;
		text += "  </IED>" + nl;

		text += "  <DataTypeTemplates>" + nl;
		for (std::map<std::string, std::string>::const_iterator it = lnTypeIdByClass.begin();
			it != lnTypeIdByClass.end(); ++it)
		{
			text += "    <LNodeType id=\"" + it->second + "\" lnClass=\"" + XmlEscape(it->first) + "\">" + nl;
			const std::vector<const SclDo*>& classDos = dosByClass[it->first];
			for (size_t d = 0; d < classDos.size(); ++d)
			{
				const SclDo& dobj = *classDos[d];
				std::string doTypeId = dobj.hasCustomDas ? dobj.customDoTypeId : DoTypeId(dobj.cdc, dobj.stValEnum);
				text += "      <DO name=\"" + XmlEscape(dobj.name) + "\" type=\"" + doTypeId + "\" />" + nl;
			}
			text += "    </LNodeType>" + nl;
		}

		for (std::map<std::string, UsedDoType>::const_iterator it = usedDoTypes.begin();
			it != usedDoTypes.end(); ++it)
		{
			const UsedDoType& u = it->second;
			text += "    <DOType id=\"" + u.id + "\" cdc=\"" + XmlEscape(u.cdc) + "\">" + nl;
			std::vector<SclDa> das = u.custom ? u.das : SclTemplates::DasForCdc(u.cdc, u.stValEnum);
			for (size_t d = 0; d < das.size(); ++d)
			{
				const SclDa& da = das[d];
				text += "      <DA name=\"" + da.name + "\" bType=\"" + da.bType + "\"";
				if (!da.structType.empty())
					text += " type=\"" + da.structType + "\"";
				text += " fc=\"" + da.fc + "\" />" + nl;
			}
			text += "    </DOType>" + nl;
		}

		text += "    <DAType id=\"AnalogueValue_1\">" + nl;
		text += "      <BDA name=\"f\" bType=\"FLOAT32\" />" + nl;
		text += "    </DAType>" + nl;
		text += "    <DAType id=\"SPCOperate_1\">" + nl;
		text += "      <BDA name=\"ctlVal\" bType=\"BOOLEAN\" />" + nl;
		text += "      <BDA name=\"T\" bType=\"Timestamp\" />" + nl;
		text += "      <BDA name=\"Test\" bType=\"BOOLEAN\" />" + nl;
		text += "    </DAType>" + nl;
		text += "    <DAType id=\"DPCOperate_1\">" + nl;
		text += "      <BDA name=\"ctlVal\" bType=\"Enum\" type=\"Dbpos\" />" + nl;
		text += "      <BDA name=\"T\" bType=\"Timestamp\" />" + nl;
		text += "      <BDA name=\"Test\" bType=\"BOOLEAN\" />" + nl;
		text += "    </DAType>" + nl;

		text += "    <EnumType id=\"Beh\">" + nl;
		text += "      <EnumVal ord=\"1\">on</EnumVal>" + nl;
		text += "      <EnumVal ord=\"2\">blocked</EnumVal>" + nl;
		text += "      <EnumVal ord=\"3\">test</EnumVal>" + nl;
		text += "      <EnumVal ord=\"4\">test/blocked</EnumVal>" + nl;
		text += "      <EnumVal ord=\"5\">off</EnumVal>" + nl;
		text += "    </EnumType>" + nl;
		text += "    <EnumType id=\"HealthKind\">" + nl;
		text += "      <EnumVal ord=\"1\">ok</EnumVal>" + nl;
		text += "      <EnumVal ord=\"2\">warning</EnumVal>" + nl;
		text += "      <EnumVal ord=\"3\">alarm</EnumVal>" + nl;
		text += "    </EnumType>" + nl;
		text += "    <EnumType id=\"ModKind\">" + nl;
		text += "      <EnumVal ord=\"1\">on</EnumVal>" + nl;
		text += "      <EnumVal ord=\"2\">blocked</EnumVal>" + nl;
		text += "      <EnumVal ord=\"3\">test</EnumVal>" + nl;
		text += "      <EnumVal ord=\"5\">off</EnumVal>" + nl;
		text += "    </EnumType>" + nl;
		text += "    <EnumType id=\"Dbpos\">" + nl;
		text += "      <EnumVal ord=\"0\">intermediate-state</EnumVal>" + nl;
		text += "      <EnumVal ord=\"1\">off</EnumVal>" + nl;
		text += "      <EnumVal ord=\"2\">on</EnumVal>" + nl;
		text += "      <EnumVal ord=\"3\">bad-state</EnumVal>" + nl;
		text += "    </EnumType>" + nl;
		text += "    <EnumType id=\"CtlModels\">" + nl;
		text += "      <EnumVal ord=\"0\">status-only</EnumVal>" + nl;
		text += "      <EnumVal ord=\"1\">direct-with-normal-security</EnumVal>" + nl;
		text += "      <EnumVal ord=\"2\">sbo-with-normal-security</EnumVal>" + nl;
		text += "      <EnumVal ord=\"3\">direct-with-enhanced-security</EnumVal>" + nl;
		text += "      <EnumVal ord=\"4\">sbo-with-enhanced-security</EnumVal>" + nl;
		text += "    </EnumType>" + nl;

		{
			std::set<std::string> fixedDaTypeIds;
			fixedDaTypeIds.insert("AnalogueValue_1");
			fixedDaTypeIds.insert("SPCOperate_1");
			fixedDaTypeIds.insert("DPCOperate_1");
			std::set<std::string> fixedEnumIds;
			fixedEnumIds.insert("Beh");
			fixedEnumIds.insert("HealthKind");
			fixedEnumIds.insert("ModKind");
			fixedEnumIds.insert("Dbpos");
			fixedEnumIds.insert("CtlModels");

			for (size_t i = 0; i < model.extraDoTypes.size(); ++i)
			{
				const SclExtraDoType& t = model.extraDoTypes[i];
				if (usedDoTypes.count(t.id))
					continue;
				text += "    <DOType id=\"" + t.id + "\" cdc=\"" + XmlEscape(t.cdc) + "\">" + nl;
				for (size_t d = 0; d < t.das.size(); ++d)
				{
					const SclDa& da = t.das[d];
					text += "      <DA name=\"" + da.name + "\" bType=\"" + da.bType + "\"";
					if (!da.structType.empty())
						text += " type=\"" + da.structType + "\"";
					text += " fc=\"" + da.fc + "\" />" + nl;
				}
				text += "    </DOType>" + nl;
			}
			for (size_t i = 0; i < model.extraDaTypes.size(); ++i)
			{
				const SclExtraDaType& t = model.extraDaTypes[i];
				if (fixedDaTypeIds.count(t.id))
					continue;
				text += "    <DAType id=\"" + t.id + "\">" + nl;
				for (size_t d = 0; d < t.bdas.size(); ++d)
				{
					const SclDa& bda = t.bdas[d];
					text += "      <BDA name=\"" + bda.name + "\" bType=\"" + bda.bType + "\"";
					if (!bda.structType.empty())
						text += " type=\"" + bda.structType + "\"";
					text += " />" + nl;
				}
				text += "    </DAType>" + nl;
			}
			for (size_t i = 0; i < model.extraEnumTypes.size(); ++i)
			{
				const SclExtraEnumType& t = model.extraEnumTypes[i];
				if (fixedEnumIds.count(t.id))
					continue;
				text += "    <EnumType id=\"" + t.id + "\">" + nl;
				for (size_t v = 0; v < t.vals.size(); ++v)
				{
					char ordBuf[32];
					sprintf(ordBuf, "%d", t.vals[v].first);
					text += "      <EnumVal ord=\"" + std::string(ordBuf) + "\">" + XmlEscape(t.vals[v].second) + "</EnumVal>" + nl;
				}
				text += "    </EnumType>" + nl;
			}
		}
		text += "  </DataTypeTemplates>" + nl;
		text += "</SCL>" + nl;
		return text;
	}

	bool WriteFile(const std::string& text, const std::string& path, std::string& errorText)
	{
		QFile f(QString::fromUtf8(path.c_str()));
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			errorText = "open file failed: " + path;
			return false;
		}
		f.write(text.data(), (qint64)text.size());
		f.close();
		return true;
	}

	bool WriteIcd(const SclModel& model, const std::string& path, std::string& errorText)
	{
		return WriteFile(BuildSclText(model, false), path, errorText);
	}

	bool WriteCid(const SclModel& model, const std::string& path, std::string& errorText)
	{
		return WriteFile(BuildSclText(model, true), path, errorText);
	}
}
