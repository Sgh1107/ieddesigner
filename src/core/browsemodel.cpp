#include "browsemodel.h"
#include "scltemplates.h"

#include <set>

namespace BrowseModel
{
	std::string MmsTypeToBType(const std::string& rawType, const std::string& daName)
	{
		if (rawType == "BOOLEAN") return "BOOLEAN";
		if (rawType == "INTEGER") return "INT32";
		if (rawType == "UNSIGNED") return "INT32U";
		if (rawType == "FLOAT") return "FLOAT32";
		if (rawType == "BIT_STRING")
		{
			if (daName == "q" || (daName.size() > 2 && daName.compare(daName.size() - 2, 2, ".q") == 0))
				return "Quality";
			return "BOOLEAN";
		}
		if (rawType == "UTC_TIME") return "Timestamp";
		if (rawType == "OCTET_STRING") return "Octet64";
		if (rawType == "VISIBLE_STRING") return "VisString255";
		if (rawType == "STRUCTURE" || rawType == "ARRAY") return "Struct";
		if (rawType == "FLOAT32" || rawType == "INT32" || rawType == "INT8U" || rawType == "INT16" ||
			rawType == "INT16U" || rawType == "INT32U" || rawType == "Quality" || rawType == "Timestamp" ||
			rawType == "VisString255" || rawType == "Octet64" || rawType == "BOOLEAN")
			return rawType;
		return "INT32";
	}

	static bool HasDa(const std::vector<SclDa>& das, const std::string& name)
	{
		for (size_t i = 0; i < das.size(); ++i)
		{
			if (das[i].name == name)
				return true;
		}
		return false;
	}

	static const SclDa* FindDa(const std::vector<SclDa>& das, const std::string& name)
	{
		for (size_t i = 0; i < das.size(); ++i)
		{
			if (das[i].name == name)
				return &das[i];
		}
		return 0;
	}

	static bool HasFc(const std::vector<SclDa>& das, const std::string& fc)
	{
		for (size_t i = 0; i < das.size(); ++i)
		{
			if (das[i].fc == fc)
				return true;
		}
		return false;
	}

	std::string InferCdc(const std::vector<SclDa>& das)
	{
		const SclDa* setMag = FindDa(das, "setMag");
		if (setMag != 0)
		{
			if (setMag->fc == "SP")
				return "APC";
			if (setMag->fc == "SG")
				return "ASG";
		}
		if (HasDa(das, "Oper") || HasDa(das, "ctlVal") || HasFc(das, "CO"))
		{
			const SclDa* stVal = FindDa(das, "stVal");
			if (stVal != 0 && stVal->bType == "INT32")
				return "DPC";
			return "SPC";
		}
		const SclDa* setVal = FindDa(das, "setVal");
		if (setVal != 0 && setVal->fc == "SG")
		{
			if (setVal->bType == "FLOAT32")
				return "SPG";
			return "ING";
		}
		if (HasDa(das, "mag") || HasDa(das, "mag.f") || HasDa(das, "mag.i"))
			return "MV";
		const SclDa* stVal2 = FindDa(das, "stVal");
		if (stVal2 != 0 && stVal2->fc == "ST")
		{
			if (stVal2->bType == "BOOLEAN")
				return "SPS";
			if (stVal2->bType == "INT32")
				return "INS";
		}
		return "";
	}

	void SplitLnName(const std::string& lnName, std::string& prefix, std::string& lnClass, std::string& inst)
	{
		prefix.clear();
		lnClass = lnName;
		inst.clear();
		size_t digitStart = lnName.size();
		while (digitStart > 0 && lnName[digitStart - 1] >= '0' && lnName[digitStart - 1] <= '9')
			--digitStart;
		if (digitStart < lnName.size())
		{
			lnClass = lnName.substr(0, digitStart);
			inst = lnName.substr(digitStart);
		}
	}

	void BuildModelFromBrowse(const std::string& iedName, const std::string& ip, int port,
		const std::vector<BLd>& lds, SclModel& out)
	{
		out = SclModel();
		out.ied.name = iedName;
		out.ied.ip = ip;
		out.ied.port = port;

		for (size_t li = 0; li < lds.size(); ++li)
		{
			const BLd& bld = lds[li];
			SclLDevice ld;
			std::string raw = bld.rawName;
			size_t slash = raw.find('/');
			if (slash != std::string::npos)
			{
				if (iedName.empty() || raw.substr(0, slash) == iedName)
					ld.inst = raw.substr(slash + 1);
				else
					ld.inst = raw;
			}
			else if (!iedName.empty() && raw.compare(0, iedName.size(), iedName) == 0 && raw.size() > iedName.size())
			{
				ld.inst = raw.substr(iedName.size());
			}
			else
			{
				ld.inst = raw;
			}
			ld.ldName = ld.inst;

			bool haveLn0 = false;
			for (size_t ni = 0; ni < bld.lns.size(); ++ni)
			{
				const BLn& bln = bld.lns[ni];
				SclLn ln;
				SplitLnName(bln.name, ln.prefix, ln.lnClass, ln.inst);

				for (size_t di = 0; di < bln.dos.size(); ++di)
				{
					const BDo& bdo = bln.dos[di];
					SclDo dobj;
					dobj.name = bdo.name;
					std::string doTypeId = "DOT_" + bln.name + "_" + bdo.name;

					std::vector<SclDa> flat;
					for (size_t ai = 0; ai < bdo.attrs.size(); ++ai)
					{
						const BAttr& attr = bdo.attrs[ai];
						if (attr.name.find('.') != std::string::npos)
							continue;
						SclDa da;
						da.name = attr.name;
						da.fc = attr.fc;
						da.bType = MmsTypeToBType(attr.bTypeRaw, attr.name);
						flat.push_back(da);
					}
					for (size_t fi = 0; fi < flat.size(); ++fi)
					{
						if (flat[fi].bType != "Struct")
							continue;
						std::string datId = doTypeId + "_" + flat[fi].name;
						flat[fi].structType = datId;
						SclExtraDaType dt;
						dt.id = datId;
						for (size_t ai = 0; ai < bdo.attrs.size(); ++ai)
						{
							const BAttr& attr = bdo.attrs[ai];
							std::string prefixName = flat[fi].name + ".";
							if (attr.name.compare(0, prefixName.size(), prefixName) != 0)
								continue;
							if (attr.name.find('.', prefixName.size()) != std::string::npos)
								continue;
							SclDa bda;
							bda.name = attr.name.substr(prefixName.size());
							bda.fc = attr.fc;
							bda.bType = MmsTypeToBType(attr.bTypeRaw, bda.name);
							dt.bdas.push_back(bda);
						}
						if (!dt.bdas.empty())
							out.extraDaTypes.push_back(dt);
					}

					dobj.customDas = flat;
					dobj.hasCustomDas = true;
					dobj.customDoTypeId = doTypeId;
					dobj.cdc = InferCdc(flat);
					ln.dos.push_back(dobj);
				}

				if (ln.lnClass == "LLN0")
				{
					ld.ln0 = ln;
					haveLn0 = true;
				}
				else
				{
					ld.lns.push_back(ln);
				}
			}
			if (!haveLn0)
				ld.ln0 = SclTemplates::MakeLn("LLN0");
			out.ied.lds.push_back(ld);
		}
	}
}
