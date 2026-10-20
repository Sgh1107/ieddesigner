#ifndef SCLMODEL_H
#define SCLMODEL_H

#include <string>
#include <vector>
#include <utility>

// ---------------- 新建模型默认值 ----------------
// 集中维护新建工程的默认参数，SclIed 构造函数与 MakeNewModel 共用，
// 避免两处各写一套导致默认值漂移（原构造函数为 192.168.31.x，MakeNewModel 为 192.168.1.x）。
// 需要调整默认规格时只改这里。
namespace SclDefaults
{
	const char* const IED_NAME          = "TEMPLATE";
	const char* const IED_DESC          = "TEMPLATE";
	const char* const MANUFACTURER      = "newfert";
	const char* const IED_TYPE          = "RTUType";
	const char* const AP_NAME           = "accessPoint1";
	const char* const LD_INST           = "LD1";
	// 通信参数（对齐 config_new.cid 规格）
	const char* const IP                = "192.168.1.100";
	const char* const IP_SUBNET         = "255.255.255.0";
	const char* const IP_GATEWAY        = "192.168.1.1";
	const int         PORT              = 102;
	const char* const MMS_PORT_STR      = "102";
	// 以下出厂设置
	const char* const OSI_AP_TITLE      = "1,1,9999,1";
	const char* const OSI_AE_QUALIFIER  = "12";
	const char* const OSI_PSEL          = "00000001";
	const char* const OSI_SSEL          = "0001";
	const char* const OSI_TSEL          = "0001";
	
	const int         SERVER_TIMEOUT    = 30;
}

struct SclDa
{
	std::string name;
	std::string bType;
	std::string fc;
	std::string structType;

	SclDa() {}
};

// DOI 下的 DAI 值设置：name 为点分路径（Struct DA 展开为 "mag.f" 形式），val 为 Val 文本
struct SclDai
{
	std::string name;
	std::string val;

	SclDai() {}
	SclDai(const std::string& n, const std::string& v) : name(n), val(v) {}
};

struct SclDo
{
	std::string name;
	std::string cdc;
	std::string stValEnum;
	std::string desc;
	std::vector<SclDai> dais;      // 已设置的 DAI 值（不含 "d"，desc 单独存放）
	std::vector<SclDa> customDas;
	bool hasCustomDas;
	std::string customDoTypeId;

	SclDo() : hasCustomDas(false) {}
	SclDo(const std::string& n, const std::string& c, const std::string& e)
		: name(n), cdc(c), stValEnum(e), hasCustomDas(false) {}

	const std::string* FindDaiVal(const std::string& daiName) const;
	void SetDaiVal(const std::string& daiName, const std::string& val);   // val 置空即删除
};

// ---------------- 数据集与控制块（挂在 LN0/LN 下） ----------------

// FCDA 成员：引用到 DO（daName 空表示到 DO 级）
struct SclFcda
{
	std::string iedName;
	std::string ldInst;
	std::string prefix;
	std::string lnClass;
	std::string lnInst;
	std::string doName;
	std::string daName;   // 可空
	std::string fc;

	std::string RefText() const;   // "IEDLD1/LN1.DO" 风格展示文本
};

struct SclDataSet
{
	std::string name;
	std::string desc;
	std::vector<SclFcda> fcdas;
};

// TrgOps 触发条件（present=元素是否存在，保证往返保真）
struct SclTrgOps
{
	bool present;
	bool dchg;
	bool qchg;
	bool dupd;
	bool period;
	bool gi;

	SclTrgOps() : present(false), dchg(false), qchg(false), dupd(false), period(false), gi(false) {}
};

// OptFields 可选字段
struct SclOptFields
{
	bool present;
	bool seqNum;
	bool timeStamp;
	bool dataSet;
	bool reasonCode;
	bool dataRef;
	bool bufOvfl;
	bool entryID;
	bool config;

	SclOptFields() : present(false), seqNum(false), timeStamp(false), dataSet(false), reasonCode(false),
		dataRef(false), bufOvfl(false), entryID(false), config(false) {}
};

struct SclReportControl
{
	std::string name;
	std::string rptId;      // 空=不输出
	std::string cbId;       // 控制块 ID (RptID/CBID)
	std::string dataSet;
	std::string confRev;
	bool buffered;          // true=BRCB
	std::string bufTime;    // 空=不输出
	std::string intgPd;     // ms，空=不输出
	SclTrgOps trgOps;
	SclOptFields optFields;
	bool indexed;
	int maxClients;         // -1=不输出

	SclReportControl() : buffered(false), indexed(true), maxClients(-1) {}
};

struct SclGseControl
{
	std::string name;
	std::string appId;
	std::string dataSet;
	std::string confRev;
	bool fixedOffs;

	SclGseControl() : fixedOffs(false) {}
};

struct SclSampledValueControl
{
	std::string name;
	std::string smvId;
	std::string dataSet;
	std::string confRev;
	std::string smpRate;    // 采样点数，如 "80"
	std::string nofASDU;    // 默认 "1"
	std::string smpMod;     // "S"=每周波采样 "P"=每秒采样

	SclSampledValueControl() : smpRate("80"), nofASDU("1"), smpMod("S") {}
};

struct SclLogControl
{
	std::string name;
	std::string dataSet;
	std::string logName;
	std::string intgPd;     // 空=不输出
	SclTrgOps trgOps;
};

struct SclSettingControl
{
	std::string name;
	std::string dataSet;
	std::string numOfSG;    // 定值组数，空=不输出
};

// ---------------- Communication 段 ----------------

// GSE/SMV 通信地址（<GSE ldInst cbName> / <SMV ldInst cbName>）
struct SclCommAddress
{
	std::string ldInst;
	std::string cbName;
	std::vector<std::pair<std::string, std::string> > pParams;  // MAC-Address/APPID/VLAN-ID/VLAN-PRIORITY
	std::string minTime;    // GSE 专用（ms），空=不输出
	std::string maxTime;    // GSE 专用，空=不输出
};

struct SclConnectedAP
{
	std::string apName;
	std::string iedName;
	std::vector<std::pair<std::string, std::string> > pParams;
	std::vector<SclCommAddress> gses;
	std::vector<SclCommAddress> smvs;
};

struct SclSubNetwork
{
	std::string name;
	std::string type;       // 如 "8-MMS"
	std::vector<SclConnectedAP> aps;
};

// ExtRef 外部信号引用：LN0/LN 下 <Inputs><ExtRef/></Inputs>，GOOSE 订阅方声明
struct SclExtRef
{
	std::string iedName;   // 来源装置名（必填）
	std::string ldInst;    // 来源逻辑设备实例
	std::string prefix;    // 来源 LN 前缀（可空）
	std::string lnClass;   // 来源 LN 类名
	std::string lnInst;    // 来源 LN 实例号
	std::string doName;    // 来源 DO 名（必填）
	std::string daName;    // 来源 DA 名（可空 = DO 级引用）
	std::string desc;      // 备注（可空）
	std::string intAddr;   // 订阅方内部输入地址（可空）

	SclExtRef() {}
	// 引用路径展示文本："来源IED/ldInst/LN.doName[.daName]"
	std::string RefText() const
	{
		std::string ln = prefix + lnClass + lnInst;
		std::string ref = iedName;
		if (!ldInst.empty())
			ref += "/" + ldInst;
		if (!ln.empty())
			ref += "/" + ln;
		ref += "." + doName;
		if (!daName.empty())
			ref += "." + daName;
		return ref;
	}
};

struct SclLn
{
	std::string prefix;
	std::string lnClass;
	std::string inst;
	std::string desc;
	std::string comment;      // XML 注释（写出时剔除特殊字符）
	std::vector<SclDo> dos;
	std::vector<SclDataSet> dataSets;
	std::vector<SclReportControl> reports;
	std::vector<SclGseControl> gses;
	std::vector<SclSampledValueControl> smvs;
	std::vector<SclLogControl> logs;
	std::vector<SclSettingControl> settings;
	std::vector<SclExtRef> extRefs;   // 外部信号引用（Inputs 段）

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
	std::vector<SclSubNetwork> subnets;     // Communication 段（空=按兼容格式输出）
	std::vector<SclExtraDoType> extraDoTypes;
	std::vector<SclExtraDaType> extraDaTypes;
	std::vector<SclExtraEnumType> extraEnumTypes;

	SclModel();
};

SclModel MakeDemoModel();

// 新建模型骨架（对齐 config_new.cid 规格：完整通信 P 参数 + Services 声明）
SclModel MakeNewModel();

#endif
