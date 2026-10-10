#ifndef SCLMODEL_H
#define SCLMODEL_H

#include <string>
#include <vector>

struct SclDa
{
	std::string name;
	std::string bType;
	std::string fc;
	std::string structType;

	SclDa() {}
};

struct SclDo
{
	std::string name;
	std::string cdc;
	std::string stValEnum;
	std::string desc;
	std::vector<SclDa> customDas;
	bool hasCustomDas;
	std::string customDoTypeId;

	SclDo() : hasCustomDas(false) {}
	SclDo(const std::string& n, const std::string& c, const std::string& e)
		: name(n), cdc(c), stValEnum(e), hasCustomDas(false) {}
};

struct SclLn
{
	std::string prefix;
	std::string lnClass;
	std::string inst;
	std::string desc;
	std::vector<SclDo> dos;

	std::string Name() const;
};

struct SclLDevice
{
	std::string inst;
	std::string ldName;
	std::string desc;
	SclLn ln0;
	std::vector<SclLn> lns;
};

struct SclIed
{
	std::string name;
	std::string desc;
	std::string apName;
	std::string ip;
	std::string ipSubnet;
	std::string ipGateway;
	int port;
	std::vector<SclLDevice> lds;

	SclIed();
};

struct SclExtraDoType
{
	std::string id;
	std::string cdc;
	std::vector<SclDa> das;
};

struct SclExtraDaType
{
	std::string id;
	std::vector<SclDa> bdas;
};

struct SclExtraEnumType
{
	std::string id;
	std::vector<std::pair<int, std::string> > vals;
};

struct SclModel
{
	std::string headerId;
	std::string toolName;
	SclIed ied;
	std::vector<SclExtraDoType> extraDoTypes;
	std::vector<SclExtraDaType> extraDaTypes;
	std::vector<SclExtraEnumType> extraEnumTypes;

	SclModel();
};

SclModel MakeDemoModel();

#endif
