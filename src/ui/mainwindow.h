#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QFile>
#include "sclmodel.h"

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
