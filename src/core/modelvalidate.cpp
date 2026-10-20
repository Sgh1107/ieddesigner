#include "modelvalidate.h"
#include "iec61850_model_loader.h"
#include "sclreader.h"

#include <stdio.h>
#include <set>
#include <string>
#include <vector>

// ==================================================
//  语义统计（数据集 / 报告 / GOOSE / SV / 日志 / 定值控制块 + 通信地址）
//
//  为什么放在设计器侧而不是 loader：运行期 loader 只负责"引用能不能读写"，
//  本仓库的 LoadResult 已按精简契约收敛，不含数据集/控制块信息。
//  设计器的 SclReader 已把这些节完整解析进 SclModel，所以统计与交叉校验在这里做，
//  既给出真数据，也不用动 libprot61850 的结构与 ABI。
//
//  计数口径：控制块按 LN0 与各 LN 下的声明逐个累加；通信地址只统计属于本 IED 的
//  ConnectedAP。校验结果只提示，不阻断导出（导出是否可用仍由 loader 判定）。
// ==================================================
namespace
{
	struct LnKey
	{
		std::string ldInst;
		std::string lnName;
		LnKey(const std::string& ld, const std::string& ln) : ldInst(ld), lnName(ln) {}
		bool operator<(const LnKey& o) const
		{
			if (ldInst != o.ldInst) return ldInst < o.ldInst;
			return lnName < o.lnName;
		}
	};

	typedef std::set<std::pair<LnKey, std::string> > NameSet;

	std::string LnPath(const LnKey& k) { return k.ldInst + "/" + k.lnName; }

	std::string FcdaText(const SclFcda& f)
	{
		std::string ln = f.prefix + f.lnClass + f.lnInst;
		std::string s = f.iedName + f.ldInst + "/" + ln + "." + f.doName;
		if (!f.daName.empty())
			s += "." + f.daName;
		return s;
	}

	// 无名或同 LN 重名都记一条问题串；返回 false 表示这一条不计入总数
	bool CheckUniqueName(NameSet& pool, const LnKey& ln, const std::string& name,
						 const char* kind, std::vector<std::string>& errors)
	{
		if (name.empty())
		{
			errors.push_back(std::string(kind) + " without name at " + LnPath(ln));
			return false;
		}
		if (!pool.insert(std::make_pair(ln, name)).second)
			errors.push_back(std::string(kind) + " '" + name + "' duplicated at " + LnPath(ln));
		return true;
	}

	// 本模型内的 DO/DA 与 DataSet 全集，供 FCDA / DatSet 交叉引用校验
	void CollectModelKeys(const SclModel& m, NameSet& doKeys, NameSet& daKeys, NameSet& dataSets)
	{
		for (size_t ldi = 0; ldi < m.ied.lds.size(); ++ldi)
		{
			const SclLDevice& ld = m.ied.lds[ldi];
			for (size_t k = 0; k <= ld.lns.size(); ++k)
			{
				const SclLn* ln = (k == 0) ? &ld.ln0 : &ld.lns[k - 1];
				LnKey key(ld.inst, ln->Name());
				for (size_t d = 0; d < ln->dos.size(); ++d)
				{
					doKeys.insert(std::make_pair(key, ln->dos[d].name));
					for (size_t a = 0; a < ln->dos[d].customDas.size(); ++a)
						daKeys.insert(std::make_pair(key, ln->dos[d].customDas[a].name));
				}
				for (size_t s = 0; s < ln->dataSets.size(); ++s)
					dataSets.insert(std::make_pair(key, ln->dataSets[s].name));
			}
		}
	}

	// 控制块引用的数据集必须与它挂在同一个 LN 下（SCL 的 datSet 是 LN 内相对名）
	bool DataSetExists(const NameSet& dataSets, const std::string& ldInst,
					   const std::string& lnName, const std::string& dsName)
	{
		return dataSets.count(std::make_pair(LnKey(ldInst, lnName), dsName)) > 0;
	}

	void CheckControl(const NameSet& dataSets, const LnKey& ln, const std::string& cbName,
					  const std::string& dsName, const char* kind, std::vector<std::string>& errors)
	{
		if (cbName.empty() || dsName.empty())
			return;
		if (!DataSetExists(dataSets, ln.ldInst, ln.lnName, dsName))
			errors.push_back(std::string(kind) + " '" + cbName + "' at " + LnPath(ln) +
							 " refers unknown DataSet '" + dsName + "'");
	}

	void AnalyzeSemantics(const SclModel& m, ValidateOutcome& out)
	{
		NameSet doKeys, daKeys, dataSets;
		CollectModelKeys(m, doKeys, daKeys, dataSets);

		// 每个 SCL 元素类型单独一张名字表：同名数据集与同名控制块是允许共存的，共用一张表会误报重名
	NameSet dataSetNames, reportNames, gseNames, smvNames, logNames, settingNames;
		NameSet gseCb, smvCb;     // 供 Communication 里的 <GSE>/<SMV> 地址反查

		for (size_t ldi = 0; ldi < m.ied.lds.size(); ++ldi)
		{
			const SclLDevice& ld = m.ied.lds[ldi];
			for (size_t k = 0; k <= ld.lns.size(); ++k)
			{
				const SclLn* ln = (k == 0) ? &ld.ln0 : &ld.lns[k - 1];
				LnKey key(ld.inst, ln->Name());

				for (size_t i = 0; i < ln->dataSets.size(); ++i)
				{
					const SclDataSet& ds = ln->dataSets[i];
					if (CheckUniqueName(dataSetNames, key, ds.name, "DataSet", out.semanticErrors))
						++out.datasetCount;
					if (ds.fcdas.empty())
						out.semanticErrors.push_back("DataSet '" + ds.name + "' at " + LnPath(key) +
													 " has no FCDA");
					for (size_t j = 0; j < ds.fcdas.size(); ++j)
					{
						const SclFcda& f = ds.fcdas[j];
						LnKey fk(f.ldInst, f.prefix + f.lnClass + f.lnInst);
						bool hit = doKeys.count(std::make_pair(fk, f.doName)) > 0;
						if (!hit && !f.daName.empty())
							hit = daKeys.count(std::make_pair(fk, f.daName)) > 0;
						if (!hit)
							out.semanticErrors.push_back("FCDA '" + FcdaText(f) + "' in DataSet '" +
														 ds.name + "' matches no DO/DA of this model");
					}
				}
				for (size_t i = 0; i < ln->reports.size(); ++i)
				{
					if (CheckUniqueName(reportNames, key, ln->reports[i].name, "ReportControl", out.semanticErrors))
						++out.reportControlCount;
					CheckControl(dataSets, key, ln->reports[i].name, ln->reports[i].dataSet,
								 "ReportControl", out.semanticErrors);
				}
				for (size_t i = 0; i < ln->gses.size(); ++i)
				{
					if (CheckUniqueName(gseNames, key, ln->gses[i].name, "GSEControl", out.semanticErrors))
					{
						++out.gseControlCount;
						gseCb.insert(std::make_pair(key, ln->gses[i].name));
					}
					CheckControl(dataSets, key, ln->gses[i].name, ln->gses[i].dataSet,
								 "GSEControl", out.semanticErrors);
				}
				for (size_t i = 0; i < ln->smvs.size(); ++i)
				{
					if (CheckUniqueName(smvNames, key, ln->smvs[i].name, "SampledValueControl", out.semanticErrors))
					{
						++out.sampledValueControlCount;
						smvCb.insert(std::make_pair(key, ln->smvs[i].name));
					}
					CheckControl(dataSets, key, ln->smvs[i].name, ln->smvs[i].dataSet,
								 "SampledValueControl", out.semanticErrors);
				}
				for (size_t i = 0; i < ln->logs.size(); ++i)
				{
					if (CheckUniqueName(logNames, key, ln->logs[i].name, "LogControl", out.semanticErrors))
						++out.logControlCount;
					CheckControl(dataSets, key, ln->logs[i].name, ln->logs[i].dataSet,
								 "LogControl", out.semanticErrors);
				}
				for (size_t i = 0; i < ln->settings.size(); ++i)
				{
					if (CheckUniqueName(settingNames, key, ln->settings[i].name, "SettingControl", out.semanticErrors))
						++out.settingControlCount;
					CheckControl(dataSets, key, ln->settings[i].name, ln->settings[i].dataSet,
								 "SettingControl", out.semanticErrors);
				}
			}
		}

		// 通信段：只数属于本 IED 的 ConnectedAP，并用控制块全集反查地址
		for (size_t s = 0; s < m.subnets.size(); ++s)
		{
			const SclSubNetwork& sn = m.subnets[s];
			for (size_t a = 0; a < sn.aps.size(); ++a)
			{
				const SclConnectedAP& ap = sn.aps[a];
				if (!ap.iedName.empty() && ap.iedName != m.ied.name)
					continue;
				for (size_t g = 0; g < ap.gses.size(); ++g)
				{
					++out.gseAddressCount;
					const SclCommAddress& ca = ap.gses[g];
					bool found = false;
					for (NameSet::const_iterator it = gseCb.begin(); it != gseCb.end(); ++it)
					{
						if (it->first.ldInst == ca.ldInst && it->second == ca.cbName) { found = true; break; }
					}
					if (!found)
						out.semanticErrors.push_back("GSE address cbName '" + ca.cbName + "' at ldInst '" +
													 ca.ldInst + "' matches no GSEControl");
				}
				for (size_t v = 0; v < ap.smvs.size(); ++v)
				{
					++out.smvAddressCount;
					const SclCommAddress& ca = ap.smvs[v];
					bool found = false;
					for (NameSet::const_iterator it = smvCb.begin(); it != smvCb.end(); ++it)
					{
						if (it->first.ldInst == ca.ldInst && it->second == ca.cbName) { found = true; break; }
					}
					if (!found)
						out.semanticErrors.push_back("SMV address cbName '" + ca.cbName + "' at ldInst '" +
													 ca.ldInst + "' matches no SampledValueControl");
				}
			}
		}
	}
}

ValidateOutcome ValidateSclFile(const std::string& sclPath, const std::string& iedName)
{
	ValidateOutcome outcome;

	// 1) 运行期 loader：引用能不能读写、点表数量，仍以它为准
	CIec61850ModelLoader loader;
	LoadOptions options;
	options.targetIedName = iedName;

	LoadResult result = loader.LoadFromFile(sclPath.c_str(), options, outcome.table);
	outcome.loaderOk = result.success;
	outcome.errorText = result.errorText;
	outcome.detectedIed = result.detectedIedName;
	outcome.refsParsed = outcome.table.TotalCount() > 0;
	outcome.sgQuirk = !result.success &&
		result.errorText == "SG contains an invalid point" &&
		!outcome.table.sgRefs.empty();
	// 本仓库 loader 的 LoadResult 没有 sgCount，定值点数直接取解析结果
	outcome.sgPoints = (int)outcome.table.sgRefs.size();

	// 2) 设计器自己的 reader：数据集/控制块/通信地址的语义统计与交叉校验
	SclModel model;
	std::string readErr;
	if (SclReader::ReadSclFile(sclPath, iedName, model, readErr))
		AnalyzeSemantics(model, outcome);
	else
		outcome.semanticErrors.push_back("semantic check skipped: SCL parse failed: " + readErr);

	return outcome;
}

bool ValidateSclText(const std::string& sclText, const std::string& iedName,
	const std::string& tempPath, ValidateOutcome& outcome)
{
	FILE* fp = fopen(tempPath.c_str(), "wb");
	if (fp == NULL)
	{
		outcome.errorText = "create temp file failed: " + tempPath;
		return false;
	}
	fwrite(sclText.data(), 1, sclText.size(), fp);
	fclose(fp);
	outcome = ValidateSclFile(tempPath, iedName);
	return true;
}
