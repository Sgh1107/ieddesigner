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
	{ "LLN0", "逻辑节点零", kLln0, sizeof(kLln0) / sizeof(kLln0[0]) },
	{ "LPHD", "物理设备信息", kLphd, sizeof(kLphd) / sizeof(kLphd[0]) },
	{ "MMXU", "三相测量", kMmxu, sizeof(kMmxu) / sizeof(kMmxu[0]) },
	{ "CSWI", "开关控制器", kCswi, sizeof(kCswi) / sizeof(kCswi[0]) },
	{ "XCBR", "断路器", kXcbr, sizeof(kXcbr) / sizeof(kXcbr[0]) },
	{ "XSWI", "刀闸/接地刀", kXswi, sizeof(kXswi) / sizeof(kXswi[0]) },
	{ "GGIO", "通用IO", kGgio, sizeof(kGgio) / sizeof(kGgio[0]) },
	{ "SGCB", "定值区控制", kSgcb, sizeof(kSgcb) / sizeof(kSgcb[0]) }
};

namespace SclTemplates
{
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
