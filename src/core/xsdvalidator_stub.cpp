#include "xsdvalidator.h"

#include <QString>

// 本文件只在构建时找不到 libxml2 的情况下参与编译（见 ieddesigner.pro）：
// 目的是让 Qt4 分支在没装 libxml2 的机器上也能编译通过，运行到 XSD 校验时
// 给出明确的“未编译”提示，而不是在编译/链接阶段直接失败。
// 找到 libxml2 时参与编译的是 xsdvalidator.cpp（真正的实现）。

namespace
{
	// GBK 源码里的中文字面量转成内部统一使用的 UTF-8 std::string
	std::string Utf8FromGbk(const char* gbk)
	{
		return std::string(QString::fromLocal8Bit(gbk).toUtf8().constData());
	}
}

namespace XsdValidator
{
	std::string EnsureSchemasExtracted()
	{
		// 返回非空占位目录：让调用方继续走到 Validate()，由 Validate() 给出本次构建未包含
		// XSD 校验的明确提示（若返回空串，调用方会先报无法释放 schema 套件，容易被误认为文件损坏）
		return std::string(".");
	}

	bool Validate(const std::string& sclText, const std::string& xsdDir,
		std::vector<XsdError>& errors, std::string& errorText)
	{
		(void)sclText;
		(void)xsdDir;
		errors.clear();
		// 本 stub 版本没有 libxml2，直接给出未编译的原因（errorText 交给调用方展示）
		errorText = Utf8FromGbk("本次构建未包含 XSD 校验（编译时未找到 libxml2），"
			"请在 ieddesigner.pro 里配置 libxml2 静态库后重新构建");
		return false;
	}
}
