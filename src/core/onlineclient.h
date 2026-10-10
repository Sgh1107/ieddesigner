#ifndef ONLINECLIENT_H
#define ONLINECLIENT_H

#include <string>
#include <vector>
#include <QThread>
#include "browsemodel.h"
#include "sclmodel.h"

namespace OnlineClient
{
	bool BrowseIedModel(const std::string& ip, int port, const std::string& preferredIedName,
		std::string& detectedIedName, std::vector<BLd>& lds, std::string& errorText);
}

class OnlineBrowseThread : public QThread
{
	Q_OBJECT
public:
	OnlineBrowseThread(const QString& ip, int port, const QString& iedName, QObject* parent = 0);
	void run();

	QString m_ip;
	int m_port;
	QString m_iedName;
	bool m_ok;
	QString m_error;
	SclModel m_model;
};

#endif
