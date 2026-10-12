#include "jsonmodel.h"
#include "browsemodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

namespace JsonModel
{
	struct JVal
	{
		enum Type { TNull, TBool, TNum, TStr, TArr, TObj } type;
		bool b;
		double num;
		std::string str;
		std::vector<JVal> arr;
		std::vector<std::pair<std::string, JVal> > obj;

		JVal() : type(TNull), b(false), num(0) {}

		const JVal* Get(const char* key) const
		{
			for (size_t i = 0; i < obj.size(); ++i)
			{
				if (obj[i].first == key)
					return &obj[i].second;
			}
			return 0;
		}
		std::string Str(const char* key) const
		{
			const JVal* v = Get(key);
			if (v == 0)
				return std::string();
			if (v->type == TStr)
				return v->str;
			if (v->type == TNum)
			{
				char buf[64];
				sprintf(buf, "%d", (int)v->num);
				return std::string(buf);
			}
			if (v->type == TBool)
				return v->b ? "true" : "false";
			return std::string();
		}
	};

	struct JParser
	{
		const char* p;
		const char* end;
		std::string error;

		void SkipWs()
		{
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
				++p;
		}

		bool ParseValue(JVal& out)
		{
			SkipWs();
			if (p >= end)
			{
				error = "unexpected end";
				return false;
			}
			char c = *p;
			if (c == '{')
				return ParseObj(out);
			if (c == '[')
				return ParseArr(out);
			if (c == '"')
			{
				out.type = JVal::TStr;
				return ParseString(out.str);
			}
			if (c == 't' || c == 'f')
			{
				out.type = JVal::TBool;
				if (end - p >= 4 && strncmp(p, "true", 4) == 0)
				{
					out.b = true;
					p += 4;
					return true;
				}
				if (end - p >= 5 && strncmp(p, "false", 5) == 0)
				{
					out.b = false;
					p += 5;
					return true;
				}
				error = "bad literal";
				return false;
			}
			if (c == 'n')
			{
				out.type = JVal::TNull;
				if (end - p >= 4 && strncmp(p, "null", 4) == 0)
				{
					p += 4;
					return true;
				}
				error = "bad literal";
				return false;
			}
			const char* start = p;
			while (p < end && (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9') || *p == 'e' || *p == 'E'))
				++p;
			if (p == start)
			{
				error = "bad value";
				return false;
			}
			out.type = JVal::TNum;
			out.num = strtod(start, 0);
			return true;
		}

		bool ParseString(std::string& out)
		{
			out.clear();
			++p;
			while (p < end)
			{
				char c = *p++;
				if (c == '"')
					return true;
				if (c == '\\' && p < end)
				{
					char e = *p++;
					switch (e)
					{
					case 'n': out += '\n'; break;
					case 't': out += '\t'; break;
					case 'r': out += '\r'; break;
					case 'u':
						p += 4;
						out += '?';
						break;
					default: out += e; break;
					}
					continue;
				}
				out += c;
			}
			error = "unterminated string";
			return false;
		}

		bool ParseObj(JVal& out)
		{
			out.type = JVal::TObj;
			++p;
			SkipWs();
			if (p < end && *p == '}')
			{
				++p;
				return true;
			}
			while (p < end)
			{
				SkipWs();
				if (p >= end || *p != '"')
				{
					error = "expected key";
					return false;
				}
				std::string key;
				if (!ParseString(key))
					return false;
				SkipWs();
				if (p >= end || *p != ':')
				{
					error = "expected colon";
					return false;
				}
				++p;
				JVal v;
				if (!ParseValue(v))
					return false;
				out.obj.push_back(std::make_pair(key, v));
				SkipWs();
				if (p < end && *p == ',')
				{
					++p;
					continue;
				}
				if (p < end && *p == '}')
				{
					++p;
					return true;
				}
				error = "expected , or }";
				return false;
			}
			error = "unterminated object";
			return false;
		}

		bool ParseArr(JVal& out)
		{
			out.type = JVal::TArr;
			++p;
			SkipWs();
			if (p < end && *p == ']')
			{
				++p;
				return true;
			}
			while (p < end)
			{
				JVal v;
				if (!ParseValue(v))
					return false;
				out.arr.push_back(v);
				SkipWs();
				if (p < end && *p == ',')
				{
					++p;
					continue;
				}
				if (p < end && *p == ']')
				{
					++p;
					return true;
				}
				error = "expected , or ]";
				return false;
			}
			error = "unterminated array";
			return false;
		}
	};

	static void AttrsFromJson(const JVal& doObj, BDo& bdo)
	{
		bdo.name = doObj.Str("name");
		bdo.controllable = false;
		const JVal* ctl = doObj.Get("controllable");
		if (ctl != 0 && ctl->type == JVal::TBool)
			bdo.controllable = ctl->b;
		const JVal* attrs = doObj.Get("dataAttributes");
		if (attrs == 0 || attrs->type != JVal::TArr)
			return;
		for (size_t i = 0; i < attrs->arr.size(); ++i)
		{
			BAttr attr;
			attr.name = attrs->arr[i].Str("name");
			attr.fc = attrs->arr[i].Str("fc");
			attr.bTypeRaw = attrs->arr[i].Str("type");
			if (!attr.name.empty())
				bdo.attrs.push_back(attr);
		}
	}

	static bool ParseRawForm(const JVal& root, std::vector<BLd>& lds)
	{
		const JVal* ldArr = root.Get("logicalDevices");
		if (ldArr == 0 || ldArr->type != JVal::TArr)
			return false;
		for (size_t i = 0; i < ldArr->arr.size(); ++i)
		{
			BLd bld;
			bld.rawName = ldArr->arr[i].Str("name");
			const JVal* lnArr = ldArr->arr[i].Get("logicalNodes");
			if (lnArr != 0 && lnArr->type == JVal::TArr)
			{
				for (size_t n = 0; n < lnArr->arr.size(); ++n)
				{
					BLn bln;
					bln.name = lnArr->arr[n].Str("name");
					const JVal* doArr = lnArr->arr[n].Get("dataObjects");
					if (doArr != 0 && doArr->type == JVal::TArr)
					{
						for (size_t d = 0; d < doArr->arr.size(); ++d)
						{
							BDo bdo;
							AttrsFromJson(doArr->arr[d], bdo);
							if (!bdo.name.empty())
								bln.dos.push_back(bdo);
						}
					}
					if (!bln.name.empty())
						bld.lns.push_back(bln);
				}
			}
			if (!bld.rawName.empty())
				lds.push_back(bld);
		}
		return !lds.empty();
	}

	struct SectionDef
	{
		const char* key;
		bool controllable;
	};

	static bool ParseFlatForm(const JVal& root, std::vector<BLd>& lds, std::string& iedName)
	{
		static const SectionDef sections[] = {
			{ "diRefs", false },
			{ "aiRefs", false },
			{ "doRefs", true },
			{ "aoRefs", true },
			{ "sgRefs", false }
		};
		iedName = root.Str("iedName");

		std::map<std::string, size_t> ldIndex;
		std::map<std::string, size_t> lnIndex;
		std::map<std::string, size_t> doIndex;

		for (size_t s = 0; s < sizeof(sections) / sizeof(sections[0]); ++s)
		{
			const JVal* arr = root.Get(sections[s].key);
			if (arr == 0 || arr->type != JVal::TArr)
				continue;
			for (size_t i = 0; i < arr->arr.size(); ++i)
			{
				const JVal& ref = arr->arr[i];
				std::string ldName = ref.Str("ldInst");
				std::string lnName = ref.Str("lnName");
				std::string doName = ref.Str("doName");
				std::string daName = ref.Str("daName");
				if (ldName.empty() || lnName.empty() || doName.empty() || daName.empty())
					continue;

				std::string ldKey = ldName;
				size_t li;
				if (ldIndex.find(ldKey) == ldIndex.end())
				{
					BLd bld;
					bld.rawName = ldName;
					lds.push_back(bld);
					ldIndex[ldKey] = lds.size() - 1;
				}
				li = ldIndex[ldKey];

				std::string lnKey = ldKey + "/" + lnName;
				size_t ni;
				if (lnIndex.find(lnKey) == lnIndex.end())
				{
					BLn bln;
					bln.name = lnName;
					lds[li].lns.push_back(bln);
					lnIndex[lnKey] = lds[li].lns.size() - 1;
				}
				ni = lnIndex[lnKey];

				std::string doKey = lnKey + "/" + doName;
				size_t di;
				if (doIndex.find(doKey) == doIndex.end())
				{
					BDo bdo;
					bdo.name = doName;
					lds[li].lns[ni].dos.push_back(bdo);
					doIndex[doKey] = lds[li].lns[ni].dos.size() - 1;
				}
				di = doIndex[doKey];
				BDo& bdo = lds[li].lns[ni].dos[di];
				if (sections[s].controllable)
					bdo.controllable = true;

				BAttr attr;
				attr.name = daName;
				attr.fc = ref.Str("fc");
				attr.bTypeRaw = ref.Str("bType");
				bdo.attrs.push_back(attr);

				size_t dot = daName.find('.');
				if (dot != std::string::npos)
				{
					std::string base = daName.substr(0, dot);
					bool hasBase = false;
					for (size_t a = 0; a < bdo.attrs.size(); ++a)
					{
						if (bdo.attrs[a].name == base)
							hasBase = true;
					}
					if (!hasBase)
					{
						BAttr parent;
						parent.name = base;
						parent.fc = attr.fc;
						parent.bTypeRaw = "STRUCTURE";
						bdo.attrs.push_back(parent);
					}
				}
			}
		}
		return !lds.empty();
	}

	bool ReadOnlineJsonFile(const std::string& path, const std::string& preferredIedName,
		const std::string& ip, int port, SclModel& model, std::string& errorText)
	{
		FILE* fp = fopen(path.c_str(), "rb");
		if (fp == NULL)
		{
			errorText = "open json failed: " + path;
			return false;
		}
		std::string text;
		char buf[4096];
		size_t rd = 0;
		while ((rd = fread(buf, 1, sizeof(buf), fp)) > 0)
			text.append(buf, rd);
		fclose(fp);

		JParser parser;
		parser.p = text.c_str();
		parser.end = text.c_str() + text.size();
		JVal root;
		if (!parser.ParseValue(root) || root.type != JVal::TObj)
		{
			errorText = "json parse failed: " + parser.error;
			return false;
		}

		std::vector<BLd> lds;
		std::string iedName = preferredIedName;
		if (root.Get("logicalDevices") != 0)
		{
			if (!ParseRawForm(root, lds))
			{
				errorText = "no logicalDevices content in json";
				return false;
			}
		}
		else
		{
			std::string detected;
			if (!ParseFlatForm(root, lds, detected))
			{
				errorText = "no point sections in json";
				return false;
			}
			if (iedName.empty())
				iedName = detected;
		}
		if (iedName.empty())
			iedName = "ONLINEIED";

		BrowseModel::BuildModelFromBrowse(iedName, ip, port, lds, model);
		return true;
	}
}
