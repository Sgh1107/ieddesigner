#include "scltemplates.h"

static const SclDaT kSps[] = {
	{ "stVal", "BOOLEAN", "ST", 0 },
	{ "q", "Quality", "ST", 0 },
	{ "t", "Timestamp", "ST", 0 },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kDps[] = {
	{ "stVal", "Enum", "ST", "Dbpos" },
	{ "q", "Quality", "ST", 0 },
	{ "t", "Timestamp", "ST", 0 },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kMv[] = {
	{ "mag", "Struct", "MX", "AnalogueValue_1" },
	{ "q", "Quality", "MX", 0 },
	{ "t", "Timestamp", "MX", 0 },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kSpc[] = {
	{ "origin", "Struct", "ST", "Originator_1" },
	{ "ctlNum", "INT8U", "ST", 0 },
	{ "stVal", "BOOLEAN", "ST", 0 },
	{ "q", "Quality", "ST", 0 },
	{ "t", "Timestamp", "ST", 0 },
	{ "ctlModel", "Enum", "CF", "CtlModels" },
	{ "Oper", "Struct", "CO", "SPCOperate_1" },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kDpc[] = {
	{ "stVal", "Enum", "ST", "Dbpos" },
	{ "q", "Quality", "ST", 0 },
	{ "t", "Timestamp", "ST", 0 },
	{ "ctlModel", "Enum", "CF", "CtlModels" },
	{ "Oper", "Struct", "CO", "DPCOperate_1" },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kApc[] = {
	{ "mxVal", "Struct", "MX", "AnalogueValue_1" },
	{ "setMag", "Struct", "SP", "AnalogueValue_1" },
	{ "q", "Quality", "ST", 0 },
	{ "t", "Timestamp", "ST", 0 },
	{ "ctlModel", "Enum", "CF", "CtlModels" },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kAsg[] = {
	{ "setMag", "Struct", "SG", "AnalogueValue_1" },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kSpg[] = {
	{ "setVal", "FLOAT32", "SG", 0 },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kIng[] = {
	{ "setVal", "INT32", "SG", 0 },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kEnc[] = {
	{ "stVal", "Enum", "ST", "ModKind" },
	{ "q", "Quality", "ST", 0 },
	{ "t", "Timestamp", "ST", 0 },
	{ "ctlModel", "Enum", "CF", "CtlModels" },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kEns[] = {
	{ "stVal", "Enum", "ST", "" },
	{ "q", "Quality", "ST", 0 },
	{ "t", "Timestamp", "ST", 0 },
	{ "d", "VisString255", "DC", 0 }
};

static const SclDaT kLpl[] = {
	{ "vendor", "VisString255", "DC", 0 },
	{ "swRev", "VisString255", "DC", 0 },
	{ "d", "VisString255", "DC", 0 },
	{ "configRev", "VisString255", "DC", 0 },
	{ "ldNs", "VisString255", "EX", 0 }
};

static const SclDaT kDpl[] = {
	{ "vendor", "VisString255", "DC", 0 },
	{ "swRev", "VisString255", "DC", 0 },
	{ "d", "VisString255", "DC", 0 }
};

static const SclCdcT kCdcTable[] = {
	{ "SPS", kSps, sizeof(kSps) / sizeof(kSps[0]) },
	{ "DPS", kDps, sizeof(kDps) / sizeof(kDps[0]) },
	{ "MV", kMv, sizeof(kMv) / sizeof(kMv[0]) },
	{ "SPC", kSpc, sizeof(kSpc) / sizeof(kSpc[0]) },
	{ "DPC", kDpc, sizeof(kDpc) / sizeof(kDpc[0]) },
	{ "APC", kApc, sizeof(kApc) / sizeof(kApc[0]) },
	{ "ASG", kAsg, sizeof(kAsg) / sizeof(kAsg[0]) },
	{ "SPG", kSpg, sizeof(kSpg) / sizeof(kSpg[0]) },
	{ "ING", kIng, sizeof(kIng) / sizeof(kIng[0]) },
	{ "ENC", kEnc, sizeof(kEnc) / sizeof(kEnc[0]) },
	{ "ENS", kEns, sizeof(kEns) / sizeof(kEns[0]) },
	{ "LPL", kLpl, sizeof(kLpl) / sizeof(kLpl[0]) },
	{ "DPL", kDpl, sizeof(kDpl) / sizeof(kDpl[0]) }
};

static const SclDoT kLln0[] = {
	{ "Mod", "ENC", "ModKind" },
	{ "Beh", "ENS", "Beh" },
	{ "Health", "ENS", "HealthKind" },
	{ "NamPlt", "LPL", "" }
};

static const SclDoT kLphd[] = {
	{ "PhyNam", "DPL", "" },
	{ "PhyHealth", "ENS", "HealthKind" },
	{ "Proxy", "SPS", "" }
};

static const SclDoT kMmxu[] = {
	{ "TotW", "MV", "" },
	{ "TotVAr", "MV", "" },
	{ "TotPF", "MV", "" },
	{ "Hz", "MV", "" }
};

static const SclDoT kCswi[] = {
	{ "Mod", "ENC", "ModKind" },
	{ "Beh", "ENS", "Beh" },
	{ "Health", "ENS", "HealthKind" },
	{ "Pos", "DPC", "" }
};

static const SclDoT kXcbr[] = {
	{ "Mod", "ENC", "ModKind" },
	{ "Beh", "ENS", "Beh" },
	{ "Health", "ENS", "HealthKind" },
	{ "Pos", "DPC", "" },
	{ "BlkOpn", "SPC", "" }
};

static const SclDoT kXswi[] = {
	{ "Mod", "ENC", "ModKind" },
	{ "Beh", "ENS", "Beh" },
	{ "Health", "ENS", "HealthKind" },
	{ "Pos", "DPC", "" },
	{ "BlkOpn", "SPC", "" }
};

static const SclDoT kGgio[] = {
	{ "Mod", "ENC", "ModKind" },
	{ "Beh", "ENS", "Beh" },
	{ "Health", "ENS", "HealthKind" },
	{ "Ind1", "SPS", "" },
	{ "Ind2", "SPS", "" },
	{ "Ind3", "SPS", "" },
	{ "Ind4", "SPS", "" },
	{ "SPCSO1", "SPC", "" },
	{ "SPCSO2", "SPC", "" },
	{ "SPCSO3", "SPC", "" },
	{ "SPCSO4", "SPC", "" },
	{ "AnIn1", "MV", "" },
	{ "AnIn2", "MV", "" },
	{ "AnIn3", "MV", "" },
	{ "AnIn4", "MV", "" }
};

static const SclDoT kSgcb[] = {
	{ "NumSG", "ING", "" },
	{ "ActSG", "ING", "" },
	{ "EditSG", "ING", "" },
	{ "CnfEdit", "SPC", "" }
};

static const SclLnT kLnTable[] = {
	{ "LLN0", "Logical node zero", kLln0, sizeof(kLln0) / sizeof(kLln0[0]) },
	{ "LPHD", "Physical device information", kLphd, sizeof(kLphd) / sizeof(kLphd[0]) },
	{ "MMXU", "Three-phase measurement", kMmxu, sizeof(kMmxu) / sizeof(kMmxu[0]) },
	{ "CSWI", "Switch controller", kCswi, sizeof(kCswi) / sizeof(kCswi[0]) },
	{ "XCBR", "Circuit breaker", kXcbr, sizeof(kXcbr) / sizeof(kXcbr[0]) },
	{ "XSWI", "Disconnector/earthing switch", kXswi, sizeof(kXswi) / sizeof(kXswi[0]) },
	{ "GGIO", "Generic IO", kGgio, sizeof(kGgio) / sizeof(kGgio[0]) },
	{ "SGCB", "Setting group control", kSgcb, sizeof(kSgcb) / sizeof(kSgcb[0]) }
};

namespace SclTemplates
{
	// 打开文件时解析出的 DAType 定义（reader 注入）：使文件自定义的
	// Struct DA（如 SPCOperate_1/Originator_1）也能展开出 BDA 层级
	static std::map<std::string, std::vector<SclDa> >& FileDaTypes()
	{
		static std::map<std::string, std::vector<SclDa> > table;
		return table;
	}

	void RegisterFileDaTypes(const std::map<std::string, std::vector<SclDa> >& daTypeBdas)
	{
		FileDaTypes() = daTypeBdas;
	}
	const std::vector<SclLnT>& LnTypes()
	{
		static std::vector<SclLnT> table;
		if (table.empty())
		{
			for (size_t i = 0; i < sizeof(kLnTable) / sizeof(kLnTable[0]); ++i)
				table.push_back(kLnTable[i]);
		}
		return table;
	}

	const SclLnT* FindLn(const std::string& lnClass)
	{
		const std::vector<SclLnT>& table = LnTypes();
		for (size_t i = 0; i < table.size(); ++i)
		{
			if (table[i].lnClass == lnClass)
				return &table[i];
		}
		return 0;
	}

	std::vector<std::string> LnClassList()
	{
		std::vector<std::string> list;
		const std::vector<SclLnT>& table = LnTypes();
		for (size_t i = 0; i < table.size(); ++i)
			list.push_back(table[i].lnClass);
		return list;
	}

	std::vector<SclDa> DasForCdc(const std::string& cdc, const std::string& stValEnum)
	{
		std::vector<SclDa> result;
		for (size_t i = 0; i < sizeof(kCdcTable) / sizeof(kCdcTable[0]); ++i)
		{
			if (cdc != kCdcTable[i].cdc)
				continue;
			for (int j = 0; j < kCdcTable[i].count; ++j)
			{
				const SclDaT& t = kCdcTable[i].das[j];
				SclDa da;
				da.name = t.name;
				da.bType = t.bType;
				da.fc = t.fc;
				da.structType = t.structType ? t.structType : "";
				if (da.bType == "Enum" && da.structType.empty() && da.name == "stVal")
					da.structType = stValEnum;
				result.push_back(da);
			}
			break;
		}
		return result;
	}

	std::vector<SclDa> BdasForDaType(const std::string& id)
	{
		std::vector<SclDa> result;
		// 先查打开文件注册的 DAType 表，再查内置表（内置表只是常见子集）
		std::map<std::string, std::vector<SclDa> >::const_iterator it = FileDaTypes().find(id);
		if (it != FileDaTypes().end())
			return it->second;
		if (id == "AnalogueValue_1")
		{
			SclDa bda; bda.name = "f"; bda.bType = "FLOAT32"; bda.fc = ""; result.push_back(bda);
		}
		else if (id == "Originator_1")
		{
			SclDa a; a.name = "orCat"; a.bType = "Enum"; a.fc = ""; a.structType = "OrCat"; result.push_back(a);
			SclDa b; b.name = "orIdent"; b.bType = "Octet64"; b.fc = ""; result.push_back(b);
		}
		else if (id == "SPCOperate_1")
		{
			SclDa a; a.name = "ctlVal"; a.bType = "BOOLEAN"; a.fc = ""; result.push_back(a);
			SclDa o; o.name = "origin"; o.bType = "Struct"; o.fc = ""; o.structType = "Originator_1"; result.push_back(o);
			SclDa n; n.name = "ctlNum"; n.bType = "INT8U"; n.fc = ""; result.push_back(n);
			SclDa b; b.name = "T"; b.bType = "Timestamp"; b.fc = ""; result.push_back(b);
			SclDa c; c.name = "Test"; c.bType = "BOOLEAN"; c.fc = ""; result.push_back(c);
			SclDa d; d.name = "Check"; d.bType = "Enum"; d.fc = ""; d.structType = "Check"; result.push_back(d);
		}
		else if (id == "DPCOperate_1")
		{
			SclDa a; a.name = "ctlVal"; a.bType = "Enum"; a.fc = ""; a.structType = "Dbpos"; result.push_back(a);
			SclDa b; b.name = "T"; b.bType = "Timestamp"; b.fc = ""; result.push_back(b);
			SclDa c; c.name = "Test"; c.bType = "BOOLEAN"; c.fc = ""; result.push_back(c);
		}
		return result;
	}

	std::vector<std::pair<int, std::string> > EnumValsFor(const std::string& id)
	{
		std::vector<std::pair<int, std::string> > result;
		if (id == "Beh")
		{
			result.push_back(std::make_pair(1, "on"));
			result.push_back(std::make_pair(2, "blocked"));
			result.push_back(std::make_pair(3, "test"));
			result.push_back(std::make_pair(4, "test/blocked"));
			result.push_back(std::make_pair(5, "off"));
		}
		else if (id == "ModKind")
		{
			result.push_back(std::make_pair(1, "on"));
			result.push_back(std::make_pair(2, "blocked"));
			result.push_back(std::make_pair(3, "test"));
			result.push_back(std::make_pair(5, "off"));
		}
		else if (id == "HealthKind")
		{
			result.push_back(std::make_pair(1, "ok"));
			result.push_back(std::make_pair(2, "warning"));
			result.push_back(std::make_pair(3, "alarm"));
		}
		else if (id == "Dbpos")
		{
			result.push_back(std::make_pair(0, "intermediate-state"));
			result.push_back(std::make_pair(1, "off"));
			result.push_back(std::make_pair(2, "on"));
			result.push_back(std::make_pair(3, "bad-state"));
		}
		else if (id == "CtlModels")
		{
			result.push_back(std::make_pair(0, "status-only"));
			result.push_back(std::make_pair(1, "direct-with-normal-security"));
			result.push_back(std::make_pair(2, "sbo-with-normal-security"));
			result.push_back(std::make_pair(3, "direct-with-enhanced-security"));
			result.push_back(std::make_pair(4, "sbo-with-enhanced-security"));
		}
		return result;
	}

	// 递归展开 DA/BDA：Struct 节点本身入表（树用于分组展示），
	// 其子 BDA 以 "父路径.子名" 继续展开（支持任意层级嵌套）
	static void ExpandDas(const std::vector<SclDa>& das, const std::string& prefix,
		std::vector<SclDaiSpec>& out)
	{
		for (size_t i = 0; i < das.size(); ++i)
		{
			const SclDa& da = das[i];
			std::string path = prefix.empty() ? da.name : (prefix + "." + da.name);
			if (da.bType == "Struct")
			{
				// 结构节点本身入表（bType=Struct，树中作为分组节点，值不可编辑）
				SclDaiSpec spec;
				spec.path = path;
				spec.bType = da.bType;
				spec.fc = da.fc;
				spec.enumId = da.structType;
				out.push_back(spec);
				// 递归展开子 BDA；DAType 未知时止步于本节点（仍展示结构名）
				std::vector<SclDa> bdas = BdasForDaType(da.structType);
				if (!bdas.empty())
					ExpandDas(bdas, path, out);
			}
			else
			{
				SclDaiSpec spec;
				spec.path = path;
				spec.bType = da.bType;
				spec.fc = da.fc;
				spec.enumId = da.structType;
				out.push_back(spec);
			}
		}
	}

	std::vector<SclDaiSpec> DaiSpecsForDo(const std::string& cdc, const std::string& stValEnum,
		const std::vector<SclDa>& customDas, bool hasCustomDas)
	{
		std::vector<SclDa> das = hasCustomDas ? customDas : DasForCdc(cdc, stValEnum);
		std::vector<SclDaiSpec> out;
		ExpandDas(das, "", out);
		return out;
	}

	std::vector<SclCdcTemplate> BuiltinCdcTemplates()
	{
		std::vector<SclCdcTemplate> list;
		for (size_t i = 0; i < sizeof(kCdcTable) / sizeof(kCdcTable[0]); ++i)
		{
			SclCdcTemplate t;
			t.cdc = kCdcTable[i].cdc;
			t.isCustom = false;
			for (int j = 0; j < kCdcTable[i].count; ++j)
			{
				SclDa da;
				da.name = kCdcTable[i].das[j].name;
				da.bType = kCdcTable[i].das[j].bType;
				da.fc = kCdcTable[i].das[j].fc;
				da.structType = kCdcTable[i].das[j].structType ? kCdcTable[i].das[j].structType : "";
				t.das.push_back(da);
			}
			list.push_back(t);
		}
		return list;
	}

	SclLn MakeLn(const std::string& lnClass)
	{
		SclLn ln;
		ln.lnClass = lnClass;
		const SclLnT* t = FindLn(lnClass);
		if (t == 0)
			return ln;
		for (int i = 0; i < t->count; ++i)
			ln.dos.push_back(SclDo(t->dos[i].name, t->dos[i].cdc, t->dos[i].stValEnum ? t->dos[i].stValEnum : ""));
		return ln;
	}
}
