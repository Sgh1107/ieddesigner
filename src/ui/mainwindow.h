#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QFile>
#include <QFutureWatcher>
#include "sclmodel.h"
#include "scltemplates.h"

class QTreeWidget;
class QTreeWidgetItem;
class QScrollArea;
class QGridLayout;
class QPlainTextEdit;
class QAction;
class QMenu;
class QComboBox;
class QLineEdit;
class QListWidget;
class QTableWidget;
class OnlineBrowseThread;
class TreeDetailDelegate;

namespace XsdValidator { struct XsdError; }

// openSclPath 两阶段异步加载的返回值。
// （原定义在 mainwindow.cpp 匿名 namespace；Qt4 版需在头文件声明 QFutureWatcher 成员，故移至此处）
struct SclIedListResult
{
	std::vector<std::string> ieds;
	std::string err;
};

struct SclLoadResult
{
	bool ok;
	std::string err;
	SclModel model;

	SclLoadResult() : ok(false) {}
};

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow(QWidget* parent = 0);
	~MainWindow();

protected:
	void closeEvent(QCloseEvent* event);

private slots:
	void onNewModel();
	void onOpenScl();
	void onSaveCid();
	void onSaveCidAs();
	void onExportIcd();
	void onExportCid();
	void onExportCsv();
	void onImportJson();
	void onOnlineRead();
	void onOnlineFinished();
	void onTreeSelectionChanged();
	void onTreeContextMenu(const QPoint& pos);
	void onTreeItemChanged(QTreeWidgetItem* item, int column);
	void onXmlPreview();
	// Qt4 老式 connect 仅支持槽：以下为 Qt6 中 lambda 连接的等价槽
	void onLangZh();
	void onLangEn();
	void onOpenRecentFile();
	void onClearRecentFiles();
	void onDeferredRebuildTree();
	void onPropComboChanged();
	void onPropLineEditFinished();
	void onCopyIecReference();
	void onCopyMmsReference();
	void onPreviewXsdCheck();
	void onListIedsFinished();
	void onLoadModelFinished();
	// DataSet 编辑对话框（原 editDataSetDialog 内局部 lambda 提升为槽）
	void onDsItemChanged(QTreeWidgetItem* item, int column);
	void onDsMoveUp();
	void onDsMoveDown();
	void onDsRemove();
	// ExtRef 编辑对话框（原 editExtRefDialog 内局部 lambda 提升为槽）
	void onExtRefItemClicked(QTreeWidgetItem* item, int column);
	void onExtRefAccepted();
	// 模板库表格的填充/编辑辅助（原为 manageTemplates 里的 lambda 体）
	void onTplSearchChanged(const QString& text);
	void onTplAdd();
	void onTplEdit();
	void onTplDel();
	void onTplDaRowAdd();
	void onTplDaRowDel();
	// 模板库按钮原来的 lambda 体，拆成成员槽
	void manageTemplates();

public:
	// 供自动化测试驱动（--autopen）
	void openSclPath(const QString& path);

private:
	void buildTree();
	// Qt4 无 lambda：原 buildTree 内的局部 lambda 提升为成员函数
	void addDaiNodes(QTreeWidgetItem* doItem, int li, int ni, int di, const SclDo& dobj);
	void addCbSections(QTreeWidgetItem* lnItem, int li, int ni, const SclLn& ln);
	static void collectExpandedPaths(QTreeWidgetItem* item, QSet<QString>& out);
	QTreeWidgetItem* restoreTreeState(QTreeWidgetItem* item, const QSet<QString>& expanded,
		const QString& selectPath) const;
	QTreeWidgetItem* addNode(QTreeWidgetItem* parent, const QString& text,
		const QString& detail, const QString& path);
	void applyNodeStyle(QTreeWidgetItem* item, const QString& path);
	void applyDetailEdit(const QString& path, const QString& text);
	void populateProps(const QString& path);
	void applyProp(const QString& path, int row, const QString& value);
	// 属性面板编辑器构造（原 populateProps 内局部 lambda 提升，Qt4 无 lambda）
	QComboBox* makePropCombo(const QString& path, const QStringList& opts, const QString& cur, int row);
	QLineEdit* makePropText(const QString& path, const QString& cur, int row, bool ro, QLineEdit*& outLe);
	void log(const QString& text);
	void logFileLine(const QString& text);
	void initLogFile();
	void validateAndLog(const std::string& sclText);
	SclLDevice* ldAt(int li);
	SclLn* lnAt(int li, int ni);
	SclDo* doAt(int li, int ni, int di);

	// 编辑器基本功能
	void setupMenus();
	void rebuildRecentMenu();
	void pushRecentFile(const QString& path);
	void setBusy(bool busy);
	void addHistoryItem();
	void deleteHistoryItem(int idx);
	void addAccessPoint();
	void deleteAccessPoint(int extraIdx);
	void addLDevice();
	void deleteLDevice(int li);
	void copyLDevice(int li);
	void pasteLDevice();
	void copyLogicNode(int li, int ni);
	void pasteLogicNode(int li);
	void showReference(const QString& path);
	bool apNameExists(const std::string& name, int skipExtraIdx) const;
	bool ldInstExists(const std::string& inst, int skipLi) const;
	bool lnExistsInLd(const SclLDevice& ld, const std::string& prefix,
		const std::string& lnClass, const std::string& inst, int skipNi) const;

	// DO/DAI 编辑与模板库
	void loadUserTemplates();
	void saveUserTemplates() const;
	// 模板库表格的填充/编辑辅助（按钮的槽声明见上面的 private slots）
	void tplFillTable(QTableWidget* table, const QString& filter);
	bool tplEditCdcDialog(SclCdcTemplate& t, bool isNew);
	void addDo(int li, int ni);
	void editDo(int li, int ni, int di);
	void deleteDo(int li, int ni, int di);
	std::vector<SclCdcTemplate> mergedCdcTemplates() const;
	SclCdcTemplate findCdcTemplate(const std::string& cdc, bool& found) const;
	QStringList enumIdList() const;
	std::vector<std::pair<int, std::string> > enumValsFor(const std::string& id) const;
	std::vector<SclDaiSpec> daiSpecsFor(const SclDo& dobj) const;
	static bool cdcNeedsStValEnum(const std::vector<SclDa>& das);
	static bool daiTypeEditable(const std::string& bType);
	bool daiValueValid(const SclDaiSpec& spec, const QString& value, QString& normalized) const;

	// 数据集/控制块/通信段 + 界面
	void setupToolbar();
	QIcon iconForPath(const QString& path) const;
	void addSubNet();
	void deleteSubNet(int idx);
	void addConnectedAP(int subnetIdx);
	void deleteConnectedAP(int subnetIdx, int apIdx);
	void addCommAddress(int subnetIdx, int apIdx, bool isGse);
	void deleteCommAddress(int subnetIdx, int apIdx, bool isGse, int addrIdx);
	void addDataSet(int li, int ni);
	bool editDataSetDialog(SclDataSet& ds, int li);
	// DataSet 编辑对话框（原 editDataSetDialog 内局部 lambda 提升，Qt4 无 lambda）
	static bool fcdaSameRef(const SclFcda& f, const SclFcda& g);
	int dsFindFcdaIndex(const SclFcda& f) const;
	void dsRefreshList();
	void dsAddLnNodes(QTreeWidgetItem* ldItem, const SclLDevice& mld, const SclLn& mln, int l, int ni);
	void dsUncheckFcda(const SclFcda& f);
	void deleteDataSet(int li, int ni, int di);
	// ExtRef 外部信号引用（GOOSE 订阅声明）
	void addExternalRef(int li, int ni);
	void editExternalRef(int li, int ni, int idx);
	void deleteExternalRef(int li, int ni, int idx);
	bool editExtRefDialog(SclExtRef& er);
	// ExtRef 编辑对话框（原 editExtRefDialog 内局部 lambda 提升，Qt4 无 lambda）
	void extRefAddLnNodes(QTreeWidgetItem* ldItem, const SclLn& mln, int li, int ni);
	// 保存/导出时自动追加当天修订历史条目
	void ensureAutoHistory();
	void setUiLanguage(const QString& lang);
	// XSD 校验
	void validateXsdAndReport(const std::string& sclText);
	void showXsdErrorList(const std::vector<XsdValidator::XsdError>& errors);
	void addReport(int li, int ni);
	void addGseControl(int li, int ni);
	void addSmvControl(int li, int ni);
	void addLogControl(int li, int ni);
	void addSettingControl(int li, int ni);
	void deleteControlBlock(const QString& kind, int li, int ni, int idx);
	bool parseBoolText(const QString& text, bool& out) const;
	SclCommAddress* findGseAddress(const std::string& ldInst, const std::string& cbName);
	SclCommAddress* findSmvAddress(const std::string& ldInst, const std::string& cbName);
	SclCommAddress* ensureGseAddress(const std::string& ldInst, const std::string& cbName);
	SclCommAddress* ensureSmvAddress(const std::string& ldInst, const std::string& cbName);

	std::vector<SclCdcTemplate> m_userCdc;
	QAction* m_actNew;
	QAction* m_actSave;
	QAction* m_actExportIcd;
	QAction* m_actTemplates;
	QAction* m_actXml;

	QTreeWidget* m_tree;
	QScrollArea* m_propScroll;
	QGridLayout* m_propGrid;
	QPlainTextEdit* m_log;
	QAction* m_jsonAct;
	QAction* m_onlineAct;
	QAction* m_openAct;
	QMenu* m_recentMenu;
	OnlineBrowseThread* m_browseThread;
	TreeDetailDelegate* m_detailDelegate;
	SclModel m_model;
	QString m_currentFile;
	QString m_currentPath;
	bool m_loadingProps;
	QFile m_logFile;
	// Qt4 异步打开（原 openSclPath 内 QFutureWatcher + lambda）所需状态
	std::string m_previewSclText;
	QString m_pendingPath;
	QString m_pendingPick;
	QFutureWatcher<SclIedListResult>* m_listWatcher;
	QFutureWatcher<SclLoadResult>* m_loadWatcher;
	// DataSet 编辑对话框的临时状态（模态对话框存续期间有效）
	std::vector<SclFcda> m_dsCur;
	bool m_dsTreeUpdating;
	QListWidget* m_dsList;
	QTreeWidget* m_dsTree;
	// ExtRef 编辑对话框的临时状态（模态对话框存续期间有效）
	SclExtRef* m_erRef;
	bool m_extRefAccepted;
	QLineEdit* m_extIedEdit;
	QLineEdit* m_extLdEdit;
	QLineEdit* m_extPrefixEdit;
	QLineEdit* m_extLnClassEdit;
	QLineEdit* m_extLnInstEdit;
	QLineEdit* m_extDoEdit;
	QLineEdit* m_extDaEdit;
	QLineEdit* m_extDescEdit;
	QLineEdit* m_extIntAddrEdit;
	// 模板库对话框的临时状态（模态对话框存续期间有效）
	QTableWidget* m_tplTable;
	QLineEdit* m_tplSearch;
	QTableWidget* m_tplDaTable;
};

#endif
