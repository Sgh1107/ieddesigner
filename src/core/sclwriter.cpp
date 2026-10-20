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

	// 源码为 GBK（分支约定）；注释中的全角替换字符需转 UTF-8 以匹配输出文本编码
	static std::string Utf8FromGbk(const char* gbk)
	{
		return std::string(QString::fromLocal8Bit(gbk).toUtf8().constData());
	}

	// 注释文本清理：剔除 XML 注释中不允许的字符，避免干扰简易解析器
	static std::string SanitizeComment(const std::string& c)
	{
		std::string out;
		for (size_t i = 0; i < c.size(); ++i)
		{
			char ch = c[i];
			if (ch == '<') out += Utf8FromGbk("（");
			else if (ch == '>') out += Utf8FromGbk("）");
			else if (ch == '&') out += Utf8FromGbk("＆");
			else if (ch == '-' && i + 1 < c.size() && c[i + 1] == '-') { out += Utf8FromGbk("－－"); ++i; }
			else out += ch;
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

	// 输出单个 DAI：点分路径重建 SDI 嵌套（如 "mag.f" -> <DAI name="mag"><SDI name="f"><Val>..</Val></SDI></DAI>）
	static void EmitDai(const SclDai& dai, int indentBase, std::string& text, const std::string& nl)
	{
		std::vector<std::string> parts;
		std::string cur;
		for (size_t i = 0; i <= dai.name.size(); ++i)
		{
			if (i == dai.name.size() || dai.name[i] == '.')
			{
				if (!cur.empty())
					parts.push_back(cur);
				cur.clear();
			}
			else
			{
				cur += dai.name[i];
			}
		}
		if (parts.empty())
			return;
		std::string pad(indentBase, ' ');
		text += pad + "<DAI name=\"" + XmlEscape(parts[0]) + "\">" + nl;
		int level = 1;
		for (size_t i = 1; i + 1 < parts.size(); ++i, ++level)
			text += std::string(indentBase + 2 * level, ' ') + "<SDI name=\"" + XmlEscape(parts[i]) + "\">" + nl;
		text += std::string(indentBase + 2 * level, ' ') + "<Val>" + XmlEscape(dai.val) + "</Val>" + nl;
		for (size_t i = parts.size() - 1; i >= 1; --i)
		{
			--level;
			text += std::string(indentBase + 2 * level, ' ') + "</SDI>" + nl;
		}
		text += pad + "</DAI>" + nl;
	}

	static const char* BoolAttr(bool b)
	{
		return b ? "true" : "false";
	}

	std::string BuildSclText(const SclModel& model, bool withCommunication)
	{
		const SclIed& ied = model.ied;
		std::string nl = "\r\n";
		std::string text;
		text += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + nl;
		text += "<SCL xmlns=\"http://www.iec.ch/61850/2003/SCL\">" + nl;
		text += "  <Header id=\"" + XmlEscape(model.headerId) + "\" nameStructure=\"IEDName\">" + nl;
		if (!model.history.empty())
		{
			text += "    <History>" + nl;
			for (size_t h = 0; h < model.history.size(); ++h)
			{
				const SclHistoryItem& item = model.history[h];
				text += "      <Hitem version=\"" + XmlEscape(item.version) + "\" what=\"" + XmlEscape(item.what) +
					"\" who=\"" + XmlEscape(item.who) + "\" when=\"" + XmlEscape(item.when) +
					"\" why=\"" + XmlEscape(item.why) + "\" />" + nl;
			}
			text += "    </History>" + nl;
		}
		text += "  </Header>" + nl;

		if (withCommunication)
		{
			char portBuf[32];
			sprintf(portBuf, "%d", ied.port);
			text += "  <Communication>" + nl;
			if (!model.subnets.empty())
			{
				// 完整通信段（多子网/多接入点/GSE/SMV 地址）
				for (size_t s = 0; s < model.subnets.size(); ++s)
				{
					const SclSubNetwork& sn = model.subnets[s];
					text += "    <SubNetwork name=\"" + XmlEscape(sn.name) + "\" type=\"" + XmlEscape(sn.type) + "\">" + nl;
					for (size_t c = 0; c < sn.aps.size(); ++c)
					{
						const SclConnectedAP& cap = sn.aps[c];
						text += "      <ConnectedAP iedName=\"" + XmlEscape(cap.iedName) + "\" apName=\"" + XmlEscape(cap.apName) + "\">" + nl;
						if (!cap.pParams.empty())
						{
							text += "        <Address>" + nl;
							for (size_t p = 0; p < cap.pParams.size(); ++p)
								text += "          <P type=\"" + XmlEscape(cap.pParams[p].first) + "\">" +
									XmlEscape(cap.pParams[p].second) + "</P>" + nl;
							text += "        </Address>" + nl;
						}
						for (size_t g = 0; g < cap.gses.size(); ++g)
						{
							const SclCommAddress& ga = cap.gses[g];
							text += "      <GSE ldInst=\"" + XmlEscape(ga.ldInst) + "\" cbName=\"" + XmlEscape(ga.cbName) + "\">" + nl;
							if (!ga.pParams.empty())
							{
								text += "        <Address>" + nl;
								for (size_t p = 0; p < ga.pParams.size(); ++p)
									text += "          <P type=\"" + XmlEscape(ga.pParams[p].first) + "\">" +
										XmlEscape(ga.pParams[p].second) + "</P>" + nl;
								text += "        </Address>" + nl;
							}
							if (!ga.minTime.empty())
								text += "        <MinTime unit=\"s\" multiplier=\"m\">" + XmlEscape(ga.minTime) + "</MinTime>" + nl;
							if (!ga.maxTime.empty())
								text += "        <MaxTime unit=\"s\" multiplier=\"m\">" + XmlEscape(ga.maxTime) + "</MaxTime>" + nl;
							text += "      </GSE>" + nl;
						}
						for (size_t g = 0; g < cap.smvs.size(); ++g)
						{
							const SclCommAddress& sa = cap.smvs[g];
							text += "      <SMV ldInst=\"" + XmlEscape(sa.ldInst) + "\" cbName=\"" + XmlEscape(sa.cbName) + "\">" + nl;
							if (!sa.pParams.empty())
							{
								text += "        <Address>" + nl;
								for (size_t p = 0; p < sa.pParams.size(); ++p)
									text += "          <P type=\"" + XmlEscape(sa.pParams[p].first) + "\">" +
										XmlEscape(sa.pParams[p].second) + "</P>" + nl;
								text += "        </Address>" + nl;
							}
							text += "      </SMV>" + nl;
						}
						text += "      </ConnectedAP>" + nl;
					}
					text += "    </SubNetwork>" + nl;
				}
			}
			else
			{
				// 兼容格式：单子网
				text += "    <SubNetwork name=\"subnetwork1\" type=\"8-MMS\">" + nl;
				text += "      <ConnectedAP iedName=\"" + XmlEscape(ied.name) + "\" apName=\"" + XmlEscape(ied.apName) + "\">" + nl;
				text += "        <Address>" + nl;
				if (!ied.pParams.empty())
				{
					// 读取文件时保序保存的全部地址参数
					for (size_t p = 0; p < ied.pParams.size(); ++p)
						text += "          <P type=\"" + XmlEscape(ied.pParams[p].first) + "\">" +
							XmlEscape(ied.pParams[p].second) + "</P>" + nl;
				}
				else
				{
					text += "          <P type=\"IP\">" + XmlEscape(ied.ip) + "</P>" + nl;
					text += "          <P type=\"IP-SUBNET\">" + XmlEscape(ied.ipSubnet) + "</P>" + nl;
					text += "          <P type=\"IP-GATEWAY\">" + XmlEscape(ied.ipGateway) + "</P>" + nl;
					text += "          <P type=\"MMS-Port\">" + std::string(portBuf) + "</P>" + nl;
				}
				text += "        </Address>" + nl;
				text += "      </ConnectedAP>" + nl;
				text += "    </SubNetwork>" + nl;
			}
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

		text += "  <IED name=\"" + XmlEscape(ied.name) + "\"";
		if (!ied.desc.empty())
			text += " desc=\"" + XmlEscape(ied.desc) + "\"";
		if (!ied.manufacturer.empty())
			text += " manufacturer=\"" + XmlEscape(ied.manufacturer) + "\"";
		if (!ied.type.empty())
			text += " type=\"" + XmlEscape(ied.type) + "\"";
		text += ">" + nl;

		if (!ied.services.empty())
		{
			text += "    <Services";
			if (!ied.servicesNameLength.empty())
				text += " nameLength=\"" + XmlEscape(ied.servicesNameLength) + "\"";
			text += ">" + nl;
			for (size_t s = 0; s < ied.services.size(); ++s)
				text += "      <" + ied.services[s] + "/>" + nl;
			text += "    </Services>" + nl;
		}

		text += "    <AccessPoint name=\"" + XmlEscape(ied.apName) + "\">" + nl;
		{
			std::string serverOpen = "      <Server";
			if (ied.serverTimeout >= 0)
			{
				char timeoutBuf[32];
				sprintf(timeoutBuf, "%d", ied.serverTimeout);
				serverOpen += " timeout=\"" + std::string(timeoutBuf) + "\"";
			}
			serverOpen += ">" + nl;
			text += serverOpen;
			if (ied.authentication)
				text += "        <Authentication/>" + nl;
		}

		for (size_t li = 0; li < ied.lds.size(); ++li)
		{
			const SclLDevice& ld = ied.lds[li];
			// LDevice 的 desc 属性（空则不输出）
			std::string ldDescAttr;
			if (!ld.desc.empty())
				ldDescAttr = " desc=\"" + XmlEscape(ld.desc) + "\"";
			text += "        <LDevice inst=\"" + XmlEscape(ld.inst) + "\"" + ldDescAttr + ">" + nl;
			if (!ld.comment.empty())
				text += "          <!--" + SanitizeComment(ld.comment) + "-->" + nl;

			std::vector<const SclLn*> allLns;
			allLns.push_back(&ld.ln0);
			for (size_t i = 0; i < ld.lns.size(); ++i)
				allLns.push_back(&ld.lns[i]);

			for (size_t i = 0; i < allLns.size(); ++i)
			{
				const SclLn& ln = *allLns[i];
				std::string lnTypeId = lnTypeIdByClass[ln.lnClass];
				// LN/LN0 的 desc 属性（空则不输出，保持与旧格式兼容）
				std::string descAttr;
				if (!ln.desc.empty())
					descAttr = " desc=\"" + XmlEscape(ln.desc) + "\"";
				std::string open;
				if (i == 0)
					open = "          <LN0 lnClass=\"" + XmlEscape(ln.lnClass) + "\" inst=\"\" lnType=\"" + lnTypeId + "\"" + descAttr + ">";
				else
					open = "          <LN lnClass=\"" + XmlEscape(ln.lnClass) + "\" inst=\"" + XmlEscape(ln.inst) +
					       "\" prefix=\"" + XmlEscape(ln.prefix) + "\" lnType=\"" + lnTypeId + "\"" + descAttr + ">";
				text += open + nl;
				if (!ln.comment.empty())
					text += "            <!--" + SanitizeComment(ln.comment) + "-->" + nl;

				// 数据集（schema 顺序：DataSet 在 tAnyLN 最前）
				for (size_t s = 0; s < ln.dataSets.size(); ++s)
				{
					const SclDataSet& ds = ln.dataSets[s];
					std::string dsOpen = "            <DataSet name=\"" + XmlEscape(ds.name) + "\"";
					if (!ds.desc.empty())
						dsOpen += " desc=\"" + XmlEscape(ds.desc) + "\"";
					text += dsOpen + ">" + nl;
					for (size_t k = 0; k < ds.fcdas.size(); ++k)
					{
						const SclFcda& f = ds.fcdas[k];
						// Ed1 schema：FCDA 无 iedName 属性；daName 不允许空串（仅 DA 级成员输出）
						text += "              <FCDA ldInst=\"" + XmlEscape(f.ldInst) + "\" prefix=\"" +
							XmlEscape(f.prefix) + "\" lnClass=\"" + XmlEscape(f.lnClass) + "\" lnInst=\"" +
							XmlEscape(f.lnInst) + "\" doName=\"" + XmlEscape(f.doName) + "\"";
						if (!f.daName.empty())
							text += " daName=\"" + XmlEscape(f.daName) + "\"";
						text += " fc=\"" + XmlEscape(f.fc) + "\" />" + nl;
					}
					text += "            </DataSet>" + nl;
				}

				// 报告控制块
				for (size_t s = 0; s < ln.reports.size(); ++s)
				{
					const SclReportControl& rc = ln.reports[s];
					std::string open = "            <ReportControl name=\"" + XmlEscape(rc.name) + "\"";
					if (!rc.rptId.empty())
						open += " rptID=\"" + XmlEscape(rc.rptId) + "\"";
					if (rc.buffered)
						open += " buffered=\"true\"";
					if (!rc.bufTime.empty())
						open += " bufTime=\"" + XmlEscape(rc.bufTime) + "\"";
					if (!rc.indexed)
						open += " indexed=\"false\"";
					if (!rc.dataSet.empty())
						open += " datSet=\"" + XmlEscape(rc.dataSet) + "\"";
					if (!rc.intgPd.empty())
						open += " intgPd=\"" + XmlEscape(rc.intgPd) + "\"";
					// Ed1 schema：confRev 为必填属性（非子元素），缺省按 "1"
					open += " confRev=\"" + XmlEscape(rc.confRev.empty() ? std::string("1") : rc.confRev) + "\"";
					open += ">";
					text += open + nl;
					if (rc.trgOps.present)
						text += "              <TrgOps dchg=\"" + std::string(BoolAttr(rc.trgOps.dchg)) +
							"\" qchg=\"" + BoolAttr(rc.trgOps.qchg) + "\" dupd=\"" + BoolAttr(rc.trgOps.dupd) +
							"\" period=\"" + BoolAttr(rc.trgOps.period) + "\" />" + nl;
					if (rc.optFields.present)
						text += "              <OptFields seqNum=\"" + std::string(BoolAttr(rc.optFields.seqNum)) +
							"\" timeStamp=\"" + BoolAttr(rc.optFields.timeStamp) + "\" dataSet=\"" + BoolAttr(rc.optFields.dataSet) +
							"\" reasonCode=\"" + BoolAttr(rc.optFields.reasonCode) + "\" dataRef=\"" + BoolAttr(rc.optFields.dataRef) +
							"\" entryID=\"" + BoolAttr(rc.optFields.entryID) + "\" />" + nl;
					if (rc.maxClients >= 0)
					{
						char mb[32];
						sprintf(mb, "%d", rc.maxClients);
						text += "              <RptEnabled max=\"" + std::string(mb) + "\" />" + nl;
					}
					text += "            </ReportControl>" + nl;
				}

				// 日志控制块（schema 顺序：ReportControl 之后、DOI 之前；Ed1 TrgOps 无 gi）
				for (size_t s = 0; s < ln.logs.size(); ++s)
				{
					const SclLogControl& lc = ln.logs[s];
					std::string open = "            <LogControl name=\"" + XmlEscape(lc.name) + "\"";
					if (!lc.dataSet.empty())
						open += " datSet=\"" + XmlEscape(lc.dataSet) + "\"";
					if (!lc.logName.empty())
						open += " logName=\"" + XmlEscape(lc.logName) + "\"";
					if (!lc.intgPd.empty())
						open += " intgPd=\"" + XmlEscape(lc.intgPd) + "\"";
					open += ">";
					text += open + nl;
					if (lc.trgOps.present)
						text += "              <TrgOps dchg=\"" + std::string(BoolAttr(lc.trgOps.dchg)) +
							"\" qchg=\"" + BoolAttr(lc.trgOps.qchg) + "\" dupd=\"" + BoolAttr(lc.trgOps.dupd) +
							"\" period=\"" + BoolAttr(lc.trgOps.period) + "\" />" + nl;
					text += "            </LogControl>" + nl;
				}

				// DOI（数据对象实例；schema 顺序：Inputs 之前）
				for (size_t d = 0; d < ln.dos.size(); ++d)
				{
					const SclDo& dobj = ln.dos[d];
					if (dobj.desc.empty() && dobj.dais.empty())
						continue;
					text += "            <DOI name=\"" + XmlEscape(dobj.name) + "\">" + nl;
					if (!dobj.desc.empty())
					{
						text += "              <DAI name=\"d\">" + nl;
						text += "                <Val>" + XmlEscape(dobj.desc) + "</Val>" + nl;
						text += "              </DAI>" + nl;
					}
					for (size_t k = 0; k < dobj.dais.size(); ++k)
						EmitDai(dobj.dais[k], 14, text, nl);
					text += "            </DOI>" + nl;
				}

				// 外部信号引用（LN0/LN 通用；schema tExtRef 无 desc 属性，desc 仅存于工程文件本地）
				if (!ln.extRefs.empty())
				{
					text += "            <Inputs>" + nl;
					for (size_t e = 0; e < ln.extRefs.size(); ++e)
					{
						const SclExtRef& er = ln.extRefs[e];
						std::string attrs = " iedName=\"" + XmlEscape(er.iedName) + "\"";
						if (!er.ldInst.empty())
							attrs += " ldInst=\"" + XmlEscape(er.ldInst) + "\"";
						if (!er.prefix.empty())
							attrs += " prefix=\"" + XmlEscape(er.prefix) + "\"";
						if (!er.lnClass.empty())
							attrs += " lnClass=\"" + XmlEscape(er.lnClass) + "\"";
						if (!er.lnInst.empty())
							attrs += " lnInst=\"" + XmlEscape(er.lnInst) + "\"";
						attrs += " doName=\"" + XmlEscape(er.doName) + "\"";
						if (!er.daName.empty())
							attrs += " daName=\"" + XmlEscape(er.daName) + "\"";
						if (!er.intAddr.empty())
							attrs += " intAddr=\"" + XmlEscape(er.intAddr) + "\"";
						text += "              <ExtRef" + attrs + " />" + nl;
					}
					text += "            </Inputs>" + nl;
				}

				// GOOSE 控制块（schema：confRev 为属性，非子元素）
				for (size_t s = 0; s < ln.gses.size(); ++s)
				{
					const SclGseControl& gc = ln.gses[s];
					std::string open = "            <GSEControl name=\"" + XmlEscape(gc.name) + "\"";
					if (!gc.appId.empty())
						open += " appID=\"" + XmlEscape(gc.appId) + "\"";
					if (gc.fixedOffs)
						open += " fixedOffs=\"true\"";
					if (!gc.dataSet.empty())
						open += " datSet=\"" + XmlEscape(gc.dataSet) + "\"";
					if (!gc.confRev.empty())
						open += " confRev=\"" + XmlEscape(gc.confRev) + "\"";
					open += ">";
					text += open + nl;
					text += "            </GSEControl>" + nl;
				}

				// 采样值控制块（schema：confRev 属性、无 smpMod、SmvOpts 必备）
				for (size_t s = 0; s < ln.smvs.size(); ++s)
				{
					const SclSampledValueControl& sc = ln.smvs[s];
					std::string open = "            <SampledValueControl name=\"" + XmlEscape(sc.name) + "\"";
					if (!sc.smvId.empty())
						open += " smvID=\"" + XmlEscape(sc.smvId) + "\"";
					if (!sc.dataSet.empty())
						open += " datSet=\"" + XmlEscape(sc.dataSet) + "\"";
					open += " smpRate=\"" + XmlEscape(sc.smpRate.empty() ? std::string("80") : sc.smpRate) + "\"";
					open += " nofASDU=\"" + XmlEscape(sc.nofASDU.empty() ? std::string("1") : sc.nofASDU) + "\"";
					if (!sc.confRev.empty())
						open += " confRev=\"" + XmlEscape(sc.confRev) + "\"";
					open += ">";
					text += open + nl;
					text += "              <SmvOpts />" + nl;
					text += "            </SampledValueControl>" + nl;
				}

				// 定值控制块（Ed1 schema：仅 numOfSGs/actSG 属性；name/datSet 仅存于工程文件本地）
				for (size_t s = 0; s < ln.settings.size(); ++s)
				{
					const SclSettingControl& sc = ln.settings[s];
					text += "            <SettingControl numOfSGs=\"" +
						XmlEscape(sc.numOfSG.empty() ? std::string("1") : sc.numOfSG) + "\" />" + nl;
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

		// 附加接入点
		for (size_t a = 0; a < ied.extraAps.size(); ++a)
		{
			const SclAccessPoint& extra = ied.extraAps[a];
			if (extra.name.empty())
				continue;
			if (!extra.serverAt.empty())
			{
				text += "    <AccessPoint name=\"" + XmlEscape(extra.name) + "\">" + nl;
				text += "      <ServerAt apName=\"" + XmlEscape(extra.serverAt) + "\" />" + nl;
				text += "    </AccessPoint>" + nl;
			}
			else
			{
				text += "    <AccessPoint name=\"" + XmlEscape(extra.name) + "\" />" + nl;
			}
		}

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
		QFile f(QString::fromLocal8Bit(path.c_str()));
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
