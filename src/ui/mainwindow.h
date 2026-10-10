#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include "sclmodel.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QTableWidgetItem;
class QPlainTextEdit;
class QAction;
class OnlineBrowseThread;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow(QWidget* parent = 0);
	void applyInitialColumnRatio();

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

private:
	void buildTree();
	QTreeWidgetItem* addNode(QTreeWidgetItem* parent, const QString& text,
		const QString& detail, const QString& path);
	void populateProps(const QString& path);
	void applyProp(const QString& path, int row, const QString& value);
	void log(const QString& text);
	void validateAndLog(const std::string& sclText);
	SclLDevice* ldAt(int li);
	SclLn* lnAt(int li, int ni);
	SclDo* doAt(int li, int ni, int di);

	QTreeWidget* m_tree;
	QTableWidget* m_props;
	QPlainTextEdit* m_log;
	QAction* m_jsonAct;
	QAction* m_onlineAct;
	OnlineBrowseThread* m_browseThread;
	SclModel m_model;
	QString m_currentFile;
	QString m_currentPath;
	bool m_loadingProps;
};

#endif
