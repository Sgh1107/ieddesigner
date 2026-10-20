#include "onlineclient.h"
#include "jsonmodel.h"

#include <set>
#include <stdio.h>

#include "iec61850_client.h"
#include "mms_value.h"
#include "mms_common.h"

namespace OnlineClient
{
	static std::string MmsTypeToText(MmsType type)
	{
		switch (type)
		{
		case MMS_BOOLEAN: return "BOOLEAN";
		case MMS_INTEGER: return "INTEGER";
		case MMS_UNSIGNED: return "UNSIGNED";
		case MMS_FLOAT: return "FLOAT";
		case MMS_BIT_STRING: return "BIT_STRING";
		case MMS_OCTET_STRING: return "OCTET_STRING";
		case MMS_VISIBLE_STRING: return "VISIBLE_STRING";
		case MMS_STRING: return "VISIBLE_STRING";
		case MMS_UTC_TIME: return "UTC_TIME";
		case MMS_STRUCTURE: return "STRUCTURE";
		case MMS_ARRAY: return "ARRAY";
		default: return "INTEGER";
		}
	}

	static FunctionalConstraint ParseFc(const std::string& fcText)
	{
		if (fcText == "ST") return IEC61850_FC_ST;
		if (fcText == "MX") return IEC61850_FC_MX;
		if (fcText == "SP") return IEC61850_FC_SP;
		if (fcText == "SV") return IEC61850_FC_SV;
		if (fcText == "CF") return IEC61850_FC_CF;
		if (fcText == "DC") return IEC61850_FC_DC;
		if (fcText == "SG") return IEC61850_FC_SG;
		if (fcText == "SE") return IEC61850_FC_SE;
		if (fcText == "EX") return IEC61850_FC_EX;
		if (fcText == "BR") return IEC61850_FC_BR;
		if (fcText == "RP") return IEC61850_FC_RP;
		if (fcText == "CO") return IEC61850_FC_CO;
		return IEC61850_FC_ST;
	}

	static void SplitNameAndFc(const std::string& raw, std::string& name, std::string& fc)
	{
		name = raw;
		fc.clear();
		size_t left = name.rfind('[');
		size_t right = name.rfind(']');
		if (left != std::string::npos && right != std::string::npos && right > left)
		{
			fc = name.substr(left + 1, right - left - 1);
			name = name.substr(0, left);
		}
	}

	static bool IsSyntheticName(const std::string& name)
	{
		if (name.empty() || name.find('$') != std::string::npos)
			return true;
		return (name == "ST" || name == "MX" || name == "CF" || name == "CO" || name == "DC" ||
			name == "SP" || name == "SV" || name == "EX" || name == "SG" || name == "SE" ||
			name == "SR" || name == "OR" || name == "BL" || name == "RP" || name == "BR" ||
			name == "LG" || name == "GO" || name == "MS" || name == "US");
	}

	static std::string ReadBType(IedConnection conn, const std::string& ref, const std::string& fcText)
	{
		IedClientError err = IED_ERROR_OK;
		MmsVariableSpecification* spec = IedConnection_getVariableSpecification(conn, &err, ref.c_str(), ParseFc(fcText));
		if (err != IED_ERROR_OK || spec == NULL)
			return std::string();
		std::string text = MmsTypeToText(MmsVariableSpecification_getType(spec));
		MmsVariableSpecification_destroy(spec);
		return text;
	}

	static bool ReadControllable(IedConnection conn, const std::string& doRef)
	{
		IedClientError err = IED_ERROR_OK;
		MmsValue* mv = IedConnection_readObject(conn, &err, (doRef + ".ctlModel").c_str(), IEC61850_FC_CF);
		if (err != IED_ERROR_OK || mv == NULL)
			return false;
		int ctlModel = MmsValue_toInt32(mv);
		MmsValue_delete(mv);
		return ctlModel > 0;
	}

	static void CollectAttrs(IedConnection conn, const std::string& ref, const std::string& namePrefix,
		int depth, std::set<std::string>& visited, std::vector<BAttr>& attrs)
	{
		if (depth > 8 || conn == NULL)
			return;
		if (!visited.insert(ref).second)
			return;

		IedClientError err = IED_ERROR_OK;
		LinkedList dataList = IedConnection_getDataDirectoryFC(conn, &err, ref.c_str());
		if (err != IED_ERROR_OK || dataList == NULL)
			return;

		std::set<std::string> seen;
		for (LinkedList el = LinkedList_getNext(dataList); el; el = LinkedList_getNext(el))
		{
			const char* rawName = (const char*)LinkedList_getData(el);
			if (rawName == NULL || rawName[0] == '\0')
				continue;
			std::string itemName;
			std::string fcText;
			SplitNameAndFc(rawName, itemName, fcText);
			if (itemName.empty())
				continue;
			std::string relative = namePrefix.empty() ? itemName : (namePrefix + "." + itemName);
			if (!seen.insert(relative + "|" + fcText).second)
				continue;
			std::string childRef = ref + "." + itemName;
			if (fcText.empty())
			{
				CollectAttrs(conn, childRef, relative, depth + 1, visited, attrs);
				continue;
			}
			BAttr attr;
			attr.name = relative;
			attr.fc = fcText;
			attr.bTypeRaw = ReadBType(conn, childRef, fcText);
			attrs.push_back(attr);
			if (attr.bTypeRaw == "STRUCTURE" || attr.bTypeRaw == "ARRAY")
				CollectAttrs(conn, childRef, relative, depth + 1, visited, attrs);
		}
		LinkedList_destroy(dataList);
	}

	bool BrowseIedModel(const std::string& ip, int port, const std::string& preferredIedName,
		std::string& detectedIedName, std::vector<BLd>& lds, std::string& errorText)
	{
		lds.clear();
		detectedIedName.clear();

		IedConnection conn = IedConnection_create();
		if (conn == NULL)
		{
			errorText = "create connection failed";
			return false;
		}
		IedConnection_setRequestTimeout(conn, 8000);

		IedClientError err = IED_ERROR_OK;
		IedConnection_connect(conn, &err, ip.c_str(), port);
		if (err != IED_ERROR_OK)
		{
			errorText = "connect failed err=" + std::string(err == IED_ERROR_CONNECTION_REJECTED ? "rejected" : "error");
			IedConnection_destroy(conn);
			return false;
		}

		LinkedList ldList = IedConnection_getServerDirectory(conn, &err, false);
		if (err != IED_ERROR_OK || ldList == NULL)
		{
			errorText = "get server directory failed";
			IedConnection_close(conn);
			IedConnection_destroy(conn);
			return false;
		}

		for (LinkedList ldEl = LinkedList_getNext(ldList); ldEl; ldEl = LinkedList_getNext(ldEl))
		{
			const char* rawLd = (const char*)LinkedList_getData(ldEl);
			if (rawLd == NULL || rawLd[0] == '\0')
				continue;
			std::string ldName = rawLd;
			std::string iedPart;
			std::string instPart;
			size_t slash = ldName.find('/');
			if (slash != std::string::npos)
			{
				iedPart = ldName.substr(0, slash);
				instPart = ldName.substr(slash + 1);
			}
			else if (!preferredIedName.empty() && ldName.compare(0, preferredIedName.size(), preferredIedName) == 0)
			{
				iedPart = preferredIedName;
				instPart = ldName.substr(preferredIedName.size());
			}
			else
			{
				iedPart = ldName;
				instPart = ldName;
			}
			if (detectedIedName.empty())
				detectedIedName = iedPart;
			else if (detectedIedName != iedPart)
				continue;

			BLd bld;
			bld.rawName = ldName;

			IedClientError lnErr = IED_ERROR_OK;
			LinkedList lnList = IedConnection_getLogicalDeviceDirectory(conn, &lnErr, ldName.c_str());
			if (lnErr == IED_ERROR_OK && lnList != NULL)
			{
				for (LinkedList lnEl = LinkedList_getNext(lnList); lnEl; lnEl = LinkedList_getNext(lnEl))
				{
					const char* rawLn = (const char*)LinkedList_getData(lnEl);
					if (rawLn == NULL || rawLn[0] == '\0')
						continue;
					std::string lnName = rawLn;
					BLn bln;
					bln.name = lnName;
					std::string lnRef = ldName + "/" + lnName;

					IedClientError doErr = IED_ERROR_OK;
					// 列 LN 下的 DO 必须用 getLogicalNodeDirectory（引用到 LN 级）；
					// getDataDirectoryFC 要求引用带 .DO 部分（DO 级及以上），对 LN 级引用会报 OBJECT_REFERENCE_INVALID
					LinkedList doList = IedConnection_getLogicalNodeDirectory(conn, &doErr, lnRef.c_str(), ACSI_CLASS_DATA_OBJECT);
					std::vector<std::string> doNames;
					std::set<std::string> unique;
					if (doErr == IED_ERROR_OK && doList != NULL)
					{
						for (LinkedList doEl = LinkedList_getNext(doList); doEl; doEl = LinkedList_getNext(doEl))
						{
							const char* rawDo = (const char*)LinkedList_getData(doEl);
							if (rawDo == NULL || rawDo[0] == '\0')
								continue;
							std::string itemName;
							std::string fcText;
							SplitNameAndFc(rawDo, itemName, fcText);
							if (itemName.empty() || IsSyntheticName(itemName))
								continue;
							size_t dot = itemName.find('.');
							std::string rootDo = (dot == std::string::npos) ? itemName : itemName.substr(0, dot);
							if (rootDo.empty() || IsSyntheticName(rootDo))
								continue;
							if (unique.insert(rootDo).second)
								doNames.push_back(rootDo);
						}
						LinkedList_destroy(doList);
					}

					for (size_t d = 0; d < doNames.size(); ++d)
					{
						BDo bdo;
						bdo.name = doNames[d];
						std::string doRef = lnRef + "." + doNames[d];
						std::set<std::string> visited;
						CollectAttrs(conn, doRef, "", 0, visited, bdo.attrs);
						if (bdo.attrs.empty())
							continue;
						bdo.controllable = ReadControllable(conn, doRef);
						bln.dos.push_back(bdo);
					}
					if (!bln.dos.empty())
						bld.lns.push_back(bln);
				}
				LinkedList_destroy(lnList);
			}
			if (!bld.lns.empty())
				lds.push_back(bld);
		}
		LinkedList_destroy(ldList);

		IedConnection_close(conn);
		IedConnection_destroy(conn);

		if (lds.empty())
		{
			errorText = "no model browsed from server";
			return false;
		}
		return true;
	}
}

OnlineBrowseThread::OnlineBrowseThread(const QString& ip, int port, const QString& iedName, QObject* parent)
	: QThread(parent)
	, m_ip(ip)
	, m_port(port)
	, m_iedName(iedName)
	, m_ok(false)
{
}

void OnlineBrowseThread::run()
{
	std::string err;
	std::string detected;
	std::vector<BLd> lds;
	std::string preferred = m_iedName.toUtf8().constData();
	m_ok = OnlineClient::BrowseIedModel(m_ip.toUtf8().constData(), m_port, preferred, detected, lds, err);
	if (m_ok)
	{
		std::string iedName = preferred.empty() ? detected : preferred;
		BrowseModel::BuildModelFromBrowse(iedName, m_ip.toUtf8().constData(), m_port, lds, m_model);
	}
	else
	{
		m_error = QString::fromUtf8(err.c_str());
	}
}
