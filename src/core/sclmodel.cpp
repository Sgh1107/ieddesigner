#include "sclmodel.h"
#include "scltemplates.h"

std::string SclLn::Name() const
{
	return prefix + lnClass + inst;
}

SclIed::SclIed()
{
	name = "TEMPLATE";
	apName = "accessPoint1";
	ip = "192.168.31.68";
	ipSubnet = "255.255.255.0";
	ipGateway = "192.168.31.1";
	port = 102;
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
	m.ied.desc = "演示IED";
	m.ied.apName = "accessPoint1";

	SclLDevice ld;
	ld.inst = "LD1";
	ld.ldName = "LD1";
	ld.desc = "演示逻辑设备";

	ld.ln0 = SclTemplates::MakeLn("LLN0");
	ld.ln0.inst = "";

	SclLn lphd = SclTemplates::MakeLn("LPHD");
	lphd.inst = "1";

	SclLn mmxu = SclTemplates::MakeLn("MMXU");
	mmxu.inst = "1";
	mmxu.desc = "测量";
	for (size_t i = 0; i < mmxu.dos.size(); ++i)
	{
		if (mmxu.dos[i].name == "TotW") mmxu.dos[i].desc = "总有功功率";
		if (mmxu.dos[i].name == "TotVAr") mmxu.dos[i].desc = "总无功功率";
		if (mmxu.dos[i].name == "TotPF") mmxu.dos[i].desc = "总功率因数";
		if (mmxu.dos[i].name == "Hz") mmxu.dos[i].desc = "频率";
	}

	SclLn ggio = SclTemplates::MakeLn("GGIO");
	ggio.inst = "1";
	ggio.desc = "通用IO";
	for (size_t i = 0; i < ggio.dos.size(); ++i)
	{
		const std::string& dn = ggio.dos[i].name;
		if (dn.compare(0, 3, "Ind") == 0) ggio.dos[i].desc = "开入" + dn.substr(3);
		if (dn.compare(0, 5, "SPCSO") == 0) ggio.dos[i].desc = "遥控" + dn.substr(5);
		if (dn.compare(0, 4, "AnIn") == 0) ggio.dos[i].desc = "遥测" + dn.substr(4);
	}

	SclLn sgcb = SclTemplates::MakeLn("SGCB");
	sgcb.inst = "";
	sgcb.desc = "定值区控制";
	for (size_t i = 0; i < sgcb.dos.size(); ++i)
	{
		if (sgcb.dos[i].name == "NumSG") sgcb.dos[i].desc = "定值区总数";
		if (sgcb.dos[i].name == "ActSG") sgcb.dos[i].desc = "当前定值区";
		if (sgcb.dos[i].name == "EditSG") sgcb.dos[i].desc = "编辑定值区";
		if (sgcb.dos[i].name == "CnfEdit") sgcb.dos[i].desc = "确认编辑";
	}

	ld.lns.push_back(lphd);
	ld.lns.push_back(mmxu);
	ld.lns.push_back(ggio);
	ld.lns.push_back(sgcb);

	m.ied.lds.push_back(ld);
	return m;
}
