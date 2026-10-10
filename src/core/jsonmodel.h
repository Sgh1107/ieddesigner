#ifndef JSONMODEL_H
#define JSONMODEL_H

#include <string>
#include "sclmodel.h"

namespace JsonModel
{
	bool ReadOnlineJsonFile(const std::string& path, const std::string& preferredIedName,
		const std::string& ip, int port, SclModel& model, std::string& errorText);
}

#endif
