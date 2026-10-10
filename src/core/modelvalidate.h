#ifndef MODELVALIDATE_H
#define MODELVALIDATE_H

#include <string>
#include "iec61850_model_types.h"

struct ValidateOutcome
{
	bool refsParsed;
	bool loaderOk;
	bool sgQuirk;
	std::string errorText;
	std::string detectedIed;
	SMMSPointTable table;

	ValidateOutcome() : refsParsed(false), loaderOk(false), sgQuirk(false) {}
};

ValidateOutcome ValidateSclFile(const std::string& sclPath, const std::string& iedName);
bool ValidateSclText(const std::string& sclText, const std::string& iedName,
	const std::string& tempPath, ValidateOutcome& outcome);

#endif
