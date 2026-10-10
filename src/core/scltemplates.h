#ifndef SCLTEMPLATES_H
#define SCLTEMPLATES_H

#include <string>
#include <vector>
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

namespace SclTemplates
{
	const std::vector<SclLnT>& LnTypes();
	const SclLnT* FindLn(const std::string& lnClass);
	std::vector<std::string> LnClassList();
	std::vector<SclDa> DasForCdc(const std::string& cdc, const std::string& stValEnum);
	SclLn MakeLn(const std::string& lnClass);
}

#endif
