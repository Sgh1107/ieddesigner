#ifndef SCLWRITER_H
#define SCLWRITER_H

#include <string>
#include "sclmodel.h"

namespace SclWriter
{
	std::string BuildSclText(const SclModel& model, bool withCommunication);
	bool WriteFile(const std::string& text, const std::string& path, std::string& errorText);
	bool WriteIcd(const SclModel& model, const std::string& path, std::string& errorText);
	bool WriteCid(const SclModel& model, const std::string& path, std::string& errorText);
}

#endif
