#include "sclmodel.h"
#include "scltemplates.h"

std::string SclLn::Name() const
{
	return prefix + lnClass + inst;
}

std::string SclFcda::RefText() const
{
	std::string ln = prefix + lnClass + lnInst;
	std::string ref = iedName + ldInst + "/" + ln;
	if (!doName.empty())
		ref += "." + doName;
	if (!daName.empty())
		ref += "." + daName;
	return ref;
}

const std::string* SclDo::FindDaiVal(const std::string& daiName) const
{
	for (size_t i = 0; i < dais.size(); ++i)
	{
		if (dais[i].name == daiName)
			return &dais[i].val;
	}
	return 0;
}

void SclDo::SetDaiVal(const std::string& daiName, const std::string& val)
{
	for (size_t i = 0; i < dais.size(); ++i)
	{
		if (dais[i].name == daiName)
		{
			if (val.empty())
				dais.erase(dais.begin() + i);
			else
				dais[i].val = val;
			return;
		}
	}
	if (!val.empty())
		dais.push_back(SclDai(daiName, val));
}

SclIed::SclIed()
{
	name = SclDefaults::IED_NAME;
	apName = SclDefaults::AP_NAME;
	ip = SclDefaults::IP;
	ipSubnet = SclDefaults::IP_SUBNET;
	ipGateway = SclDefaults::IP_GATEWAY;
	port = SclDefaults::PORT;
	serverTimeout = -1;
	authentication = false;
}

SclModel MakeNewModel()
{
	SclModel m;
	m.ied.name = SclDefaults::IED_NAME;
	m.ied.desc = SclDefaults::IED_DESC;
	m.ied.manufacturer = SclDefaults::MANUFACTURER;
	m.ied.type = SclDefaults::IED_TYPE;
	m.ied.apName = SclDefaults::AP_NAME;
	m.ied.serverTimeout = SclDefaults::SERVER_TIMEOUT;
	m.ied.authentication = true;
	// Services 能力声明（元素集合与顺序须符合 SCL2003 v1.7 schema 的 tServices 定义）
	m.ied.servicesNameLength = "";
	const char* services[] = {
		"DynAssociation", "GetDirectory", "GetDataObjectDefinition",
		"DataObjectDirectory", "GetDataSetValue", "SetDataSetValue",
		"DataSetDirectory", "ReadWrite", "GetCBValues"
	};
	for (size_t i = 0; i < sizeof(services) / sizeof(services[0]); ++i)
		m.ied.services.push_back(services[i]);

	// 通信参数（默认值见 SclDefaults，新建对话框可逐工程覆盖）
	m.ied.pParams.push_back(std::make_pair("OSI-AP-Title", SclDefaults::OSI_AP_TITLE));
	m.ied.pParams.push_back(std::make_pair("OSI-AE-Qualifier", SclDefaults::OSI_AE_QUALIFIER));
	m.ied.pParams.push_back(std::make_pair("OSI-PSEL", SclDefaults::OSI_PSEL));
	m.ied.pParams.push_back(std::make_pair("OSI-SSEL", SclDefaults::OSI_SSEL));
	m.ied.pParams.push_back(std::make_pair("OSI-TSEL", SclDefaults::OSI_TSEL));
	m.ied.pParams.push_back(std::make_pair("IP", SclDefaults::IP));
	m.ied.pParams.push_back(std::make_pair("IP-SUBNET", SclDefaults::IP_SUBNET));
	m.ied.pParams.push_back(std::make_pair("IP-GATEWAY", SclDefaults::IP_GATEWAY));
	m.ied.pParams.push_back(std::make_pair("MMS-Port", SclDefaults::MMS_PORT_STR));
	m.ied.ip = SclDefaults::IP;
	m.ied.ipSubnet = SclDefaults::IP_SUBNET;
	m.ied.ipGateway = SclDefaults::IP_GATEWAY;
	m.ied.port = SclDefaults::PORT;

	// 通信段（完整模型化，树中可直接编辑）
	SclConnectedAP cap;
	cap.apName = "accessPoint1";
	cap.iedName = "TEMPLATE";
	cap.pParams = m.ied.pParams;
	SclSubNetwork subnet;
	subnet.name = "SubNetworkName";
	subnet.type = "8-MMS";
	subnet.aps.push_back(cap);
	m.subnets.push_back(subnet);

	SclLDevice ld;
	ld.inst = "LD1";
	ld.ldName = "LD1";
	ld.desc = "Logical device 1";
	ld.comment = "New logical device, editable in the property page";

	ld.ln0 = SclTemplates::MakeLn("LLN0");
	ld.ln0.inst = "";

	SclLn lphd = SclTemplates::MakeLn("LPHD");
	lphd.inst = "1";
	lphd.desc = "Physical device information";

	ld.lns.push_back(lphd);
	m.ied.lds.push_back(ld);
	return m;
}

SclModel::SclModel()
{
	headerId = "newfert-ieddesigner";
	toolName = "newfert-ieddesigner";
}

SclModel MakeDemoModel()
{
	SclModel m;
	m.ied.name = "DEMOIED";
	m.ied.desc = "Demo IED";
	m.ied.apName = "accessPoint1";

	SclLDevice ld;
	ld.inst = "LD1";
	ld.ldName = "LD1";
	ld.desc = "Demo logical device";

	ld.ln0 = SclTemplates::MakeLn("LLN0");
	ld.ln0.inst = "";

	SclLn lphd = SclTemplates::MakeLn("LPHD");
	lphd.inst = "1";

	SclLn mmxu = SclTemplates::MakeLn("MMXU");
	mmxu.inst = "1";
	mmxu.desc = "Measurement";
	for (size_t i = 0; i < mmxu.dos.size(); ++i)
	{
		if (mmxu.dos[i].name == "TotW") mmxu.dos[i].desc = "Total active power";
		if (mmxu.dos[i].name == "TotVAr") mmxu.dos[i].desc = "Total reactive power";
		if (mmxu.dos[i].name == "TotPF") mmxu.dos[i].desc = "Total power factor";
		if (mmxu.dos[i].name == "Hz") mmxu.dos[i].desc = "Frequency";
	}

	SclLn ggio = SclTemplates::MakeLn("GGIO");
	ggio.inst = "1";
	ggio.desc = "Generic IO";
	for (size_t i = 0; i < ggio.dos.size(); ++i)
	{
		const std::string& dn = ggio.dos[i].name;
		if (dn.compare(0, 3, "Ind") == 0) ggio.dos[i].desc = "DI" + dn.substr(3);
		if (dn.compare(0, 5, "SPCSO") == 0) ggio.dos[i].desc = "Remote control" + dn.substr(5);
		if (dn.compare(0, 4, "AnIn") == 0) ggio.dos[i].desc = "AI" + dn.substr(4);
	}

	SclLn sgcb = SclTemplates::MakeLn("SGCB");
	sgcb.inst = "";
	sgcb.desc = "Setting group control";
	for (size_t i = 0; i < sgcb.dos.size(); ++i)
	{
		if (sgcb.dos[i].name == "NumSG") sgcb.dos[i].desc = "Number of setting groups";
		if (sgcb.dos[i].name == "ActSG") sgcb.dos[i].desc = "Active setting group";
		if (sgcb.dos[i].name == "EditSG") sgcb.dos[i].desc = "Editing setting group";
		if (sgcb.dos[i].name == "CnfEdit") sgcb.dos[i].desc = "Confirm edit";
	}

	ld.lns.push_back(lphd);
	ld.lns.push_back(mmxu);
	ld.lns.push_back(ggio);
	ld.lns.push_back(sgcb);

	m.ied.lds.push_back(ld);
	return m;
}
