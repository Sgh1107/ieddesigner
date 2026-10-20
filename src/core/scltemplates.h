#ifndef SCLTEMPLATES_H
#define SCLTEMPLATES_H

#include <string>
#include <vector>
#include <map>
#include <utility>
#include "sclmodel.h"

struct SclDaT
{
	const char* name;
	const char* bType;
	const char* fc;
	const char* structType;
};

struct SclCdcT
{
	const char* cdc;
	const SclDaT* das;
	int count;
};

struct SclDoT
{
	const char* name;
	const char* cdc;
	const char* stValEnum;
};

struct SclLnT
{
	const char* lnClass;
	const char* desc;
	const SclDoT* dos;
	int count;
};

// 某个 DO 可编辑的 DAI 规格（Struct DA 已展开为点分路径）
struct SclDaiSpec
{
	std::string path;      // 如 "mag.f"、"ctlModel"
	std::string bType;
	std::string fc;
	std::string enumId;    // bType==Enum 时的枚举类型 id
};

// CDC 模板（内置或用户自定义）
struct SclCdcTemplate
{
	std::string cdc;
	std::string desc;
	std::vector<SclDa> das;
	bool isCustom;

	SclCdcTemplate() : isCustom(false) {}
};

namespace SclTemplates
{
	const std::vector<SclLnT>& LnTypes();
	const SclLnT* FindLn(const std::string& lnClass);
	std::vector<std::string> LnClassList();
	std::vector<SclDa> DasForCdc(const std::string& cdc, const std::string& stValEnum);
	SclLn MakeLn(const std::string& lnClass);

	// 某个 DO 的可用 DAI 规格（Struct DA 递归展开 BDA 为点分路径）
	std::vector<SclDaiSpec> DaiSpecsForDo(const std::string& cdc, const std::string& stValEnum,
		const std::vector<SclDa>& customDas, bool hasCustomDas);

	// 已知 DAType 的 BDA 列表（未知名返回空）
	std::vector<SclDa> BdasForDaType(const std::string& id);

	// 注册打开文件时解析出的 DAType 定义（reader 调用）：
	// 使 DaiSpecsForDo 能展开文件自定义的 Struct DA（内置表查不到的 id 落到注册表）
	void RegisterFileDaTypes(const std::map<std::string, std::vector<SclDa> >& daTypeBdas);

	// 已知枚举类型的取值表（未知名返回空；自定义枚举查模型 extraEnumTypes）
	std::vector<std::pair<int, std::string> > EnumValsFor(const std::string& id);

	// 内置 CDC 模板清单
	std::vector<SclCdcTemplate> BuiltinCdcTemplates();
}

#endif
