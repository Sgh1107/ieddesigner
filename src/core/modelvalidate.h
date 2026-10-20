#ifndef MODELVALIDATE_H
#define MODELVALIDATE_H

#include <string>
#include <vector>
#include "iec61850_model_types.h"

struct ValidateOutcome
{
	bool refsParsed;
	bool loaderOk;
	bool sgQuirk;
	std::string errorText;
	std::string detectedIed;
	SMMSPointTable table;

	// 第三期任务 3：数据集/控制块引用一致性校验结果
	int sgPoints;
	int datasetCount;
	int reportControlCount;
	int gseControlCount;
	int sampledValueControlCount;
	int logControlCount;
	int settingControlCount;
	int gseAddressCount;
	int smvAddressCount;
	std::vector<std::string> semanticErrors;

	ValidateOutcome()
		: refsParsed(false), loaderOk(false), sgQuirk(false),
		  sgPoints(0), datasetCount(0), reportControlCount(0), gseControlCount(0),
		  sampledValueControlCount(0), logControlCount(0), settingControlCount(0),
		  gseAddressCount(0), smvAddressCount(0) {}
};

ValidateOutcome ValidateSclFile(const std::string& sclPath, const std::string& iedName);
bool ValidateSclText(const std::string& sclText, const std::string& iedName,
	const std::string& tempPath, ValidateOutcome& outcome);

#endif
