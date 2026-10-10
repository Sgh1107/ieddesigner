#ifndef CSVWRITER_H
#define CSVWRITER_H

#include <string>
#include "sclmodel.h"
#include "iec61850_model_types.h"

namespace CsvWriter
{
	bool WriteCsv(const SclModel& model, const SMMSPointTable& table,
		const std::string& path, std::string& errorText);
}

#endif
