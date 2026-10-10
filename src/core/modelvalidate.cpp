#include "modelvalidate.h"
#include "iec61850_model_loader.h"

#include <stdio.h>

ValidateOutcome ValidateSclFile(const std::string& sclPath, const std::string& iedName)
{
	ValidateOutcome outcome;
	CIec61850ModelLoader loader;
	LoadOptions options;
	options.targetIedName = iedName;
	options.allowAutoDetect = true;
	options.logEnabled = true;

	LoadResult result = loader.LoadFromFile(sclPath.c_str(), options, outcome.table);
	outcome.loaderOk = result.success;
	outcome.errorText = result.errorText;
	outcome.detectedIed = result.detectedIedName;
	outcome.refsParsed = outcome.table.TotalCount() > 0;
	outcome.sgQuirk = !result.success &&
		result.errorText == "SG contains an invalid point" &&
		!outcome.table.sgRefs.empty();
	return outcome;
}

bool ValidateSclText(const std::string& sclText, const std::string& iedName,
	const std::string& tempPath, ValidateOutcome& outcome)
{
	FILE* fp = fopen(tempPath.c_str(), "wb");
	if (fp == NULL)
	{
		outcome.errorText = "create temp file failed: " + tempPath;
		return false;
	}
	fwrite(sclText.data(), 1, sclText.size(), fp);
	fclose(fp);
	outcome = ValidateSclFile(tempPath, iedName);
	return true;
}
