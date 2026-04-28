#pragma once

#include <QMainWindow>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_pushButtonBrowseFile_clicked();
    void on_pushButtonBrowseOutput_clicked();
    void on_pushButtonBrowseFfmpeg_clicked();
    void on_pushButtonStart_clicked();
    void on_pushButtonClearLog_clicked();

    void appendLog(const QString &text);

private:
    // --- UI ---
    Ui::MainWindow *ui {nullptr}; // Qt 仍由父子关系管理生命周期

    // --- 工具函数 ---
    [[nodiscard]] QString findPythonExecutable();
    [[nodiscard]] QString findFFmpegExecutable();
    [[nodiscard]] bool ensureRequiredScripts();
    [[nodiscard]] bool downloadFile(const QString &url,
                                    const QString &outputPath);

    // --- 缓存 ---
    QString cachedPythonPath {};
};