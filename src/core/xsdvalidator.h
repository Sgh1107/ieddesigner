#ifndef XSDVALIDATOR_H
#define XSDVALIDATOR_H

#include <string>
#include <vector>

// SCL XSD 校验（第三期任务 4）：基于 libxml2 的 xmlSchema 校验
// schema 套件（SCL2003）打包于 qrc:/xsd/SCL2003/，首次校验时释放到临时目录后加载，
// 进程内缓存解析结果，重复校验零开销
namespace XsdValidator
{
	struct XsdError
	{
		int line;          // 违规所在行号（schema 能定位时）
		std::string message;
	};

	// 从 qrc:/xsd/SCL2003/ 释放 schema 套件到磁盘（exe 旁 schemas/ 目录，回退临时目录），
	// 返回 SCL.xsd 所在目录；失败返回空串
	std::string EnsureSchemasExtracted();

	// 校验 SCL 文本。xsdDir 为 SCL.xsd 所在目录（由 EnsureSchemasExtracted 提供）。
	// 返回 false 表示校验流程本身失败（schema 加载失败等，见 errorText），
	// 返回 true 时看 errors：空 = 通过
	bool Validate(const std::string& sclText, const std::string& xsdDir,
		std::vector<XsdError>& errors, std::string& errorText);
}

#endif
