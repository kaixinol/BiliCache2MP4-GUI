#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButtonBrowseFile_clicked();
    void on_pushButtonBrowseOutput_clicked();
    void on_pushButtonBrowseFfmpeg_clicked();
    void on_pushButtonStart_clicked();
    void on_pushButtonClearLog_clicked();
    void appendLog(const QString &text);

private:
    Ui::MainWindow *ui;

    // 工具函数
    QString findPythonExecutable();
    QString findFFmpegExecutable();
    bool ensureRequiredScripts();
    bool downloadFile(const QString &url, const QString &outputPath);

    QString cachedPythonPath;
};

#endif // MAINWINDOW_H