#ifndef SCLREADER_H
#define SCLREADER_H

#include <string>
#include <vector>
#include "sclmodel.h"

namespace SclReader
{
	std::vector<std::string> ListIeds(const std::string& path, std::string& errorText);
	bool ReadSclFile(const std::string& path, const std::string& iedName,
		SclModel& model, std::string& errorText);
}

#endif
