#ifndef SCLMODEL_H
#define SCLMODEL_H

#include <string>
#include <vector>
#include <utility>

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
	std::string comment;      // XML 注释（写出时剔除特殊字符）
	std::vector<SclDo> dos;

	std::string Name() const;
};

struct SclLDevice
{
	std::string inst;
	std::string ldName;
	std::string desc;
	std::string comment;      // XML 注释
	SclLn ln0;
	std::vector<SclLn> lns;
};

// 附加接入点：主接入点(含 Server)之外的其他 AccessPoint
struct SclAccessPoint
{
	std::string name;
	std::string serverAt;     // 非空表示 <ServerAt apName="..."/>，空表示独立接入点

	SclAccessPoint() {}
	SclAccessPoint(const std::string& n, const std::string& at) : name(n), serverAt(at) {}
};

// Header 修订历史条目 <Hitem version what who when why/>
struct SclHistoryItem
{
	std::string version;
	std::string what;
	std::string who;
	std::string when;
	std::string why;
};

struct SclIed
{
	std::string name;
	std::string desc;
	std::string manufacturer;
	std::string type;
	std::string apName;                 // 主接入点名称（其下挂 Server）
	std::vector<SclAccessPoint> extraAps;   // 附加接入点
	std::vector<std::pair<std::string, std::string> > pParams;  // Communication 地址参数（有序，空=按旧格式输出 4 项）
	std::string ip;
	std::string ipSubnet;
	std::string ipGateway;
	int port;
	std::vector<std::string> services;  // IED->Services 子元素名（空=不输出）
	std::string servicesNameLength;     // Services 的 nameLength 属性，空=不输出
	int serverTimeout;                  // Server timeout 属性，-1=不输出
	bool authentication;                // Server 下是否输出 <Authentication/>
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
	std::vector<SclHistoryItem> history;    // Header 修订历史
	SclIed ied;
	std::vector<SclExtraDoType> extraDoTypes;
	std::vector<SclExtraDaType> extraDaTypes;
	std::vector<SclExtraEnumType> extraEnumTypes;

	SclModel();
};

SclModel MakeDemoModel();

// 新建模型骨架（对齐 config_new.cid 规格：完整通信 P 参数 + Services 声明）
SclModel MakeNewModel();

#endif
