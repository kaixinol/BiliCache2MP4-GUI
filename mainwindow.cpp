#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QDir>
#include <QProcess>
#include <QDateTime>
#include <QCoreApplication>
#include <QThread>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 初始化进度条
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setValue(0);

    // 线程数限制
    int maxThreads = QThread::idealThreadCount();
    ui->spinBoxThread->setMaximum(qMax(1, maxThreads));
    ui->spinBoxThread->setValue(qMin(8, maxThreads)); // 默认给8线程或最大值
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::appendLog(const QString &text)
{
    ui->plainTextEditLog->appendPlainText(text);
}

// ---------------- 核心改进：原生高速下载 ----------------
bool MainWindow::downloadFile(const QString &urlStr, const QString &outputPath)
{
    appendLog(QString("正在下载: %1").arg(urlStr));

    QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(urlStr)));
    // 设置 User-Agent 模拟浏览器，防止部分代理拦截
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Qt/6.11");

    QNetworkReply *reply = manager.get(request);

    // 使用局部事件循环防止阻塞 UI
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() == QNetworkReply::NoError) {
        QFile file(outputPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            appendLog(QString("✓ 下载成功: %1").arg(QFileInfo(outputPath).fileName()));
            reply->deleteLater();
            return true;
        } else {
            appendLog("错误: 无法写入文件到磁盘");
        }
    } else {
        appendLog(QString("✗ 下载失败: %1").arg(reply->errorString()));
    }

    reply->deleteLater();
    return false;
}

bool MainWindow::ensureRequiredScripts()
{
    QString appDir = QCoreApplication::applicationDirPath();
    bool success = true;
    QString proxyPrefix = "https://gh-proxy.org/";

    // 依赖列表：文件名 和 原始GitHub路径
    QMap<QString, QString> scripts;
    scripts.insert("danmaku2ass.py", "https://github.com/m13253/danmaku2ass/raw/master/danmaku2ass.py");
    scripts.insert("converter.py", "https://github.com/kaixinol/BiliCache2MP4/raw/main/converter.py");

    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        QString filePath = appDir + "/" + it.key();
        if (!QFile::exists(filePath)) {
            appendLog(QString("检测到缺失: %1，尝试通过代理下载...").arg(it.key()));
            if (!downloadFile(proxyPrefix + it.value(), filePath)) {
                success = false;
            }
        } else {
            appendLog(QString("✓ %1 已就绪").arg(it.key()));
        }
    }

    return success;
}

// ---------------- UI 与 逻辑 处理 ----------------

void MainWindow::on_pushButtonBrowseFile_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择B站缓存文件夹", ui->lineEditFile->text());
    if (!dir.isEmpty()) ui->lineEditFile->setText(QDir::toNativeSeparators(dir));
}

void MainWindow::on_pushButtonBrowseOutput_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择输出目录", ui->lineEditOutput->text());
    if (!dir.isEmpty()) ui->lineEditOutput->setText(QDir::toNativeSeparators(dir));
}

void MainWindow::on_pushButtonBrowseFfmpeg_clicked()
{
    QString file = QFileDialog::getOpenFileName(this, "选择ffmpeg.exe", "", "FFmpeg (ffmpeg.exe);;All Files (*)");
    if (!file.isEmpty()) ui->lineEditFfmpeg->setText(QDir::toNativeSeparators(file));

}
QString MainWindow::findFFmpegExecutable()
{
    QString inputFfmpeg = ui->lineEditFfmpeg->text().trimmed();

    // 1. 如果用户手动指定了路径，直接校验并返回绝对路径
    if (!inputFfmpeg.isEmpty()) {
        QFileInfo checkFile(inputFfmpeg);
        if (checkFile.exists() && checkFile.isFile()) {
            return checkFile.absoluteFilePath();
        }
        return ""; // 手动指定的路径无效
    }

// 2. 如果没指定，在系统环境变量 PATH 中寻找
#ifdef Q_OS_WIN
    QString ffmpegName = "ffmpeg.exe";
#else
    QString ffmpegName = "ffmpeg";
#endif

    // QStandardPaths::findExecutable 会遍历 PATH 变量寻找可执行文件
    QString systemFfmpeg = QStandardPaths::findExecutable(ffmpegName);
    return systemFfmpeg; // 找到则返回绝对路径，找不到返回空字符串
}
void MainWindow::on_pushButtonStart_clicked()
{
    ui->plainTextEditLog->clear();
    appendLog("===== 准备启动转换任务 =====");

    // --- 1. 获取路径 ---
    QString appDir = QCoreApplication::applicationDirPath();
    QString inputPath = ui->lineEditFile->text().trimmed();
    QString outputPath = ui->lineEditOutput->text().trimmed();

    // 脚本路径定义
    QString entryScript = QDir(appDir).absoluteFilePath("converter.py");
    QString danmakuScript = QDir(appDir).absoluteFilePath("danmaku2ass.py");

    // --- 2. 环境校验 ---

    // Python 绝对路径
    QString pythonAbsPath = findPythonExecutable();
    if (pythonAbsPath.isEmpty()) {
        appendLog("错误: 系统未检测到 Python，请安装并添加到环境变量");
        return;
    }

    // FFmpeg 绝对路径
    QString ffmpegAbsPath = findFFmpegExecutable(); // 使用上一轮定义的逻辑
    if (ffmpegAbsPath.isEmpty()) {
        appendLog("错误: 无法定位 FFmpeg 绝对路径，请检查设置或环境变量");
        return;
    }

    // 脚本存在性检查
    if (!QFile::exists(entryScript) || !QFile::exists(danmakuScript)) {
        appendLog("正在检查环境脚本...");
        if (!ensureRequiredScripts()) {
            appendLog("错误: 关键脚本 (converter.py 或 danmaku2ass.py) 缺失且下载失败");
            return;
        }
    }

    // 自动处理输出路径
    if (outputPath.isEmpty()) {
        QFileInfo inputInfo(inputPath);
        // 拼接格式：原文件夹名_converted_videos
        outputPath = inputInfo.absoluteFilePath() + "_" + "converted_videos";

        QDir().mkpath(outputPath);
        ui->lineEditOutput->setText(QDir::toNativeSeparators(outputPath));
        appendLog("提示: 已自动设置输出目录为同级目录");
    }

    ui->pushButtonStart->setEnabled(false);

    // --- 3. 输出绝对路径详情 (用户要求) ---
    appendLog("--- 运行环境详情 ---");
    appendLog("Python 解释器 : " + pythonAbsPath);
    appendLog("FFmpeg 程序   : " + ffmpegAbsPath);
    appendLog("主入口脚本    : " + entryScript);
    appendLog("弹幕转换脚本  : " + danmakuScript);
    appendLog("输入数据目录  : " + QFileInfo(inputPath).absoluteFilePath());
    appendLog("转换输出目录  : " + QFileInfo(outputPath).absoluteFilePath());
    appendLog("-------------------");

    // --- 4. 构建参数与启动 ---
    QStringList args;
    args << entryScript;
    args << "-f" << QDir::toNativeSeparators(ffmpegAbsPath);

    if (ui->checkBoxFolder->isChecked()) args << "-folder";
    if (ui->checkBoxDanmaku->isChecked()) args << "-danmaku";
    if (ui->checkBoxNfo->isChecked()) args << "-nfo";

    args << "-t" << QString::number(ui->spinBoxThread->value());
    args << QDir::toNativeSeparators(inputPath);
    args << "-o" << QDir::toNativeSeparators(outputPath);

    QProcess *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    // 实时读取输出
    connect(process, &QProcess::readyReadStandardOutput, this, [=]() {
        QString out = QString::fromUtf8(process->readAllStandardOutput());
        for (const QString &line : out.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts)) {
            appendLog(line.trimmed());
        }
    });

    // 完成回调
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int code, QProcess::ExitStatus status) {
                if (status == QProcess::NormalExit && code == 0) {
                    appendLog("===== 任务全部完成 =====");
                } else {
                    appendLog(QString("===== 任务异常中止 (退出码: %1) =====").arg(code));
                }
                ui->progressBar->setMaximum(100);
                ui->progressBar->setValue(100);
                ui->pushButtonStart->setEnabled(true);
                process->deleteLater();
            });

    appendLog("启动转换引擎...");
    process->start(pythonAbsPath, args);
    ui->progressBar->setMaximum(0);
}

QString MainWindow::findPythonExecutable()
{
    if (!cachedPythonPath.isEmpty()) return cachedPythonPath;

    // 常见的 Python 命令名称
    QStringList cmds = { "python3", "py","python"};
    for (const QString &c : cmds) {
        // 先尝试通过 PATH 寻找可执行文件的绝对路径
        QString fullPath = QStandardPaths::findExecutable(c);
        if (fullPath.isEmpty()) continue;

        // 验证该路径是否确实有效
        QProcess p;
        p.start(fullPath, {"--version"});
        if (p.waitForFinished(500) && p.exitCode() == 0) {
            cachedPythonPath = fullPath; // 存储的是绝对路径
            return fullPath;
        }
    }
    return "";
}

void MainWindow::on_pushButtonClearLog_clicked() { ui->plainTextEditLog->clear(); }