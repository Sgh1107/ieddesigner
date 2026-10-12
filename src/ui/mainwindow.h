#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QFile>
#include "sclmodel.h"
#include "scltemplates.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QTableWidgetItem;
class QPlainTextEdit;
class QAction;
class QMenu;
class OnlineBrowseThread;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow(QWidget* parent = 0);
	~MainWindow();

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
	void onPropChanged(QTableWidgetItem* item);
	void onTreeContextMenu(const QPoint& pos);
	void onXmlPreview();

private:
	void buildTree();
	QTreeWidgetItem* addNode(QTreeWidgetItem* parent, const QString& text,
		const QString& detail, const QString& path);
	void populateProps(const QString& path);
	void applyProp(const QString& path, int row, const QString& value);
	void log(const QString& text);
	void logFileLine(const QString& text);
	void initLogFile();
	void validateAndLog(const std::string& sclText);
	SclLDevice* ldAt(int li);
	SclLn* lnAt(int li, int ni);
	SclDo* doAt(int li, int ni, int di);

	// 第一期新增：编辑器基本功
	void setupMenus();
	void rebuildRecentMenu();
	void pushRecentFile(const QString& path);
	void openSclPath(const QString& path);
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

	// 第二期：DO/DAI 编辑与模板库
	void loadUserTemplates();
	void saveUserTemplates() const;
	void manageTemplates();
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

	// 第二期第二批：数据集/控制块/通信段 + 界面
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
	void deleteDataSet(int li, int ni, int di);
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
	QAction* m_actOpen;
	QAction* m_actSave;
	QAction* m_actExportIcd;
	QAction* m_actTemplates;
	QAction* m_actXml;

	QTreeWidget* m_tree;
	QTableWidget* m_props;
	QPlainTextEdit* m_log;
	QAction* m_jsonAct;
	QAction* m_onlineAct;
	QAction* m_openAct;
	QMenu* m_recentMenu;
	OnlineBrowseThread* m_browseThread;
	SclModel m_model;
	QString m_currentFile;
	QString m_currentPath;
	bool m_loadingProps;
	QFile m_logFile;
};

#endif
