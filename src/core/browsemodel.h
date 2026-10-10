#ifndef BROWSEMODEL_H
#define BROWSEMODEL_H

#include <string>
#include <vector>
#include "sclmodel.h"

struct BAttr
{
	std::string name;
	std::string fc;
	std::string bTypeRaw;
};

struct BDo
{
	std::string name;
	bool controllable;
	std::vector<BAttr> attrs;

	BDo() : controllable(false) {}
};

struct BLn
{
	std::string name;
	std::vector<BDo> dos;
};

struct BLd
{
	std::string rawName;
	std::vector<BLn> lns;
};

namespace BrowseModel
{
	std::string MmsTypeToBType(const std::string& rawType, const std::string& daName);
	std::string InferCdc(const std::vector<SclDa>& das);
	void BuildModelFromBrowse(const std::string& iedName, const std::string& ip, int port,
		const std::vector<BLd>& lds, SclModel& out);
	void SplitLnName(const std::string& lnName, std::string& prefix, std::string& lnClass, std::string& inst);
}

#endif
