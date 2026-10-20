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

	// 第三期任务 3：汇总 loader 侧数据集/控制块语义校验结果
	outcome.sgPoints = result.sgCount;
	outcome.datasetCount = result.semantics.datasetCount;
	outcome.reportControlCount = result.semantics.reportControlCount;
	outcome.gseControlCount = result.semantics.gseControlCount;
	outcome.sampledValueControlCount = result.semantics.sampledValueControlCount;
	outcome.logControlCount = result.semantics.logControlCount;
	outcome.settingControlCount = result.semantics.settingControlCount;
	outcome.gseAddressCount = result.semantics.gseAddressCount;
	outcome.smvAddressCount = result.semantics.smvAddressCount;
	outcome.semanticErrors = result.semantics.errors;
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
