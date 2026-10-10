#include "csvwriter.h"

#include <stdio.h>
#include <map>
#include <QString>

namespace CsvWriter
{
	static const char* FcText(uint8 fc)
	{
		switch (fc)
		{
		case 1: return "MX";
		case 2: return "SP";
		case 3: return "SV";
		case 4: return "CF";
		case 5: return "DC";
		case 6: return "SG";
		case 7: return "SE";
		case 8: return "EX";
		case 9: return "BR";
		case 10: return "RP";
		default: return "ST";
		}
	}

	static std::string CsvField(const std::string& gbkValue)
	{
		bool needQuote = gbkValue.find(',') != std::string::npos ||
			gbkValue.find('"') != std::string::npos;
		if (!needQuote)
			return gbkValue;
		std::string out = "\"";
		for (size_t i = 0; i < gbkValue.size(); ++i)
		{
			if (gbkValue[i] == '"')
				out += "\"\"";
			else
				out += gbkValue[i];
		}
		out += "\"";
		return out;
	}

	struct DescMapBuilder
	{
		std::map<std::string, std::string> descByRef;

		void Add(const SclLn& ln, const std::string& ldInst)
		{
			for (size_t d = 0; d < ln.dos.size(); ++d)
			{
				const SclDo& dobj = ln.dos[d];
				std::string key = ldInst + "/" + ln.Name() + "/" + dobj.name;
				descByRef[key] = dobj.desc;
			}
		}
	};

	bool WriteCsv(const SclModel& model, const SMMSPointTable& table,
		const std::string& path, std::string& errorText)
	{
		DescMapBuilder descs;
		for (size_t li = 0; li < model.ied.lds.size(); ++li)
		{
			const SclLDevice& ld = model.ied.lds[li];
			descs.Add(ld.ln0, ld.inst);
			for (size_t i = 0; i < ld.lns.size(); ++i)
				descs.Add(ld.lns[i], ld.inst);
		}

		std::string nl = "\r\n";
		std::string text;
		text += "组,组内序号,SetCode,IED,LD,LN,DO,DA,读引用,控制引用,FC,CDC,值类型,可控,描述" + nl;

		struct Section { const char* group; const std::vector<SMMSDataRef>* refs; bool isSg; };
		Section sections[5] = {
			{ "DI", &table.diRefs, false },
			{ "AI", &table.aiRefs, false },
			{ "DO", &table.doRefs, false },
			{ "AO", &table.aoRefs, false },
			{ "SG", &table.sgRefs, true }
		};

		for (int s = 0; s < 5; ++s)
		{
			const std::vector<SMMSDataRef>& refs = *sections[s].refs;
			for (size_t i = 0; i < refs.size(); ++i)
			{
				const SMMSDataRef& r = refs[i];
				std::string daBase = r.strDA;
				size_t dot = daBase.find('.');
				if (dot != std::string::npos)
					daBase = daBase.substr(0, dot);
				std::string desc = descs.descByRef[r.strLDInst + "/" + r.strLNName + "/" + daBase];

				char numBuf[64];
				sprintf(numBuf, "%d,%d,", (int)r.iInternalIndex, sections[s].isSg ? (int)r.iInternalIndex + 1 : 0);

				text += CsvField(sections[s].group);
				text += ",";
				text += numBuf;
				text += CsvField(r.strIEDName) + ",";
				text += CsvField(r.strLDInst) + ",";
				text += CsvField(r.strLNName) + ",";
				text += CsvField(r.strDO) + ",";
				text += CsvField(r.strDA) + ",";
				text += CsvField(r.strReference) + ",";
				text += CsvField(r.strControlReference) + ",";
				text += FcText(r.ucFC);
				text += ",";
				text += CsvField(r.strCDC) + ",";
				text += CsvField(r.strBType) + ",";
				text += (r.bControllable ? "是" : "否");
				text += ",";
				text += CsvField(desc);
				text += nl;
			}
		}

		Q_UNUSED(model);
		FILE* fp = fopen(path.c_str(), "wb");
		if (fp == NULL)
		{
			errorText = "open csv file failed: " + path;
			return false;
		}
		fwrite(text.data(), 1, text.size(), fp);
		fclose(fp);
		return true;
	}
}
