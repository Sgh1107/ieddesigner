#include "xsdvalidator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <libxml/parser.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlerror.h>

namespace XsdValidator
{
	// 进程内 schema 缓存：首次加载后复用
	static xmlSchemaPtr g_schema = NULL;
	static std::string g_schemaDir;

	std::string EnsureSchemasExtracted()
	{
		QString base = QCoreApplication::applicationDirPath();
		if (base.isEmpty())
			base = QDir::tempPath();   // CLI 模式无 app 实例时
		QString dir = base + "/schemas/SCL2003";
		static const char* files[] = {
			"SCL.xsd", "SCL_BaseSimpleTypes.xsd", "SCL_BaseTypes.xsd",
			"SCL_Communication.xsd", "SCL_DataTypeTemplates.xsd", "SCL_Enums.xsd",
			"SCL_IED.xsd", "SCL_Substation.xsd", "IECCopyright.xsd"
		};
		QDir().mkpath(dir);
		for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
		{
			QFile res(QString(":/xsd/SCL2003/") + files[i]);
			if (!res.open(QIODevice::ReadOnly))
				return std::string();
			QFile out(dir + "/" + files[i]);
			if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return std::string();
			out.write(res.readAll());
			out.close();
		}
		return dir.toUtf8().constData();
	}

	struct ErrorCollector
	{
		std::vector<XsdError>* errors;
		bool fatal;
	};

	// 结构化错误回调：收集校验错误（含行号）
	static void OnStructuredError(void* userData, const xmlError* error)
	{
		ErrorCollector* col = (ErrorCollector*)userData;
		if (error == NULL || col == NULL)
			return;
		if (error->level < XML_ERR_ERROR)
			return;
		XsdError e;
		e.line = error->line;
		std::string msg = (error->message != NULL) ? error->message : "";
		// 去掉 libxml2 自带的换行
		while (!msg.empty() && (msg[msg.size() - 1] == '\n' || msg[msg.size() - 1] == '\r'))
			msg.erase(msg.size() - 1);
		e.message = msg;
		col->errors->push_back(e);
		if (error->level >= XML_ERR_FATAL)
			col->fatal = true;
	}

	// 加载并缓存 schema；失败返回 false 并给出 errorText
	static bool EnsureSchema(const std::string& xsdDir, std::string& errorText)
	{
		if (g_schema != NULL && g_schemaDir == xsdDir)
			return true;
		if (g_schema != NULL)
		{
			xmlSchemaFree(g_schema);
			g_schema = NULL;
		}

		std::string sclXsd = xsdDir + "/SCL.xsd";
		xmlSchemaParserCtxtPtr pctx = xmlSchemaNewParserCtxt(sclXsd.c_str());
		if (pctx == NULL)
		{
			errorText = "cannot create schema parser context: " + sclXsd;
			return false;
		}
		g_schema = xmlSchemaParse(pctx);
		xmlSchemaFreeParserCtxt(pctx);
		if (g_schema == NULL)
		{
			errorText = "schema parse failed: " + sclXsd;
			return false;
		}
		g_schemaDir = xsdDir;
		return true;
	}

	bool Validate(const std::string& sclText, const std::string& xsdDir,
		std::vector<XsdError>& errors, std::string& errorText)
	{
		errors.clear();
		errorText.clear();

		if (!EnsureSchema(xsdDir, errorText))
			return false;

		// 解析待校验文本；NONET 禁止 schema 里的网络访问
		xmlDocPtr doc = xmlReadMemory(sclText.c_str(), (int)sclText.size(),
			"model.xml", NULL, XML_PARSE_NONET);
		if (doc == NULL)
		{
			ErrorCollector col;
			col.errors = &errors;
			col.fatal = false;
			xmlDocPtr raw = xmlReadMemory(sclText.c_str(), (int)sclText.size(),
				"model.xml", NULL, XML_PARSE_NONET | XML_PARSE_RECOVER);
			if (raw == NULL)
			{
				errorText = "XML parse failed (not well-formed)";
				return false;
			}
			// well-formed 但带恢复：按结构错误报告（行号来自原始解析错误）
			xmlFreeDoc(raw);
			XsdError e;
			e.line = xmlGetLastError() ? xmlGetLastError()->line : 0;
			e.message = xmlGetLastError() && xmlGetLastError()->message ?
				xmlGetLastError()->message : "XML parse error";
			errors.push_back(e);
			return true;
		}

		xmlSchemaValidCtxtPtr vctx = xmlSchemaNewValidCtxt(g_schema);
		if (vctx == NULL)
		{
			errorText = "cannot create validation context";
			xmlFreeDoc(doc);
			return false;
		}

		ErrorCollector col;
		col.errors = &errors;
		col.fatal = false;
		xmlSchemaSetValidStructuredErrors(vctx, OnStructuredError, &col);

		int rc = xmlSchemaValidateDoc(vctx, doc);

		xmlSchemaFreeValidCtxt(vctx);
		xmlFreeDoc(doc);

		if (rc == -1)
		{
			// 校验过程内部错误（非文档违规）
			errorText = "validation internal error";
			return false;
		}
		// rc == 0 通过；rc > 0 有违规（errors 已由回调收集）
		if (errors.empty() && rc > 0)
			errors.push_back(XsdError{ 0, "document does not validate (details unavailable)" });
		return true;
	}
}
