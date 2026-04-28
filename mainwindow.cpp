#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

namespace {
constexpr auto kProxyPrefix = "https://gh-proxy.org/";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);

    const auto maxThreads = QThread::idealThreadCount();
    ui->spinBoxThread->setMaximum(qMax(1, maxThreads));
    ui->spinBoxThread->setValue(qMin(8, maxThreads));
    this->setWindowTitle(this->windowTitle() + " " + APP_VERSION);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::appendLog(const QString &text)
{
    ui->plainTextEditLog->appendPlainText(text);
}

bool MainWindow::downloadFile(const QString &urlStr, const QString &outputPath)
{
    appendLog(QStringLiteral("正在下载: %1").arg(urlStr));

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(urlStr)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Qt/6.11"));

    auto *reply = manager.get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto cleanup = qScopeGuard([&] {
        reply->deleteLater();
    });

    if (reply->error() != QNetworkReply::NoError) {
        appendLog(QStringLiteral("✗ 下载失败: %1").arg(reply->errorString()));
        return false;
    }

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendLog(QStringLiteral("错误: 无法写入文件到磁盘"));
        return false;
    }

    file.write(reply->readAll());
    appendLog(QStringLiteral("✓ 下载成功: %1").arg(QFileInfo(outputPath).fileName()));
    return true;
}

bool MainWindow::ensureRequiredScripts()
{
    const auto appDir = QCoreApplication::applicationDirPath();
    bool success = true;

    const QMap<QString, QString> scripts{
                                         {QStringLiteral("danmaku2ass.py"),
                                          QStringLiteral("https://github.com/m13253/danmaku2ass/raw/master/danmaku2ass.py")},
                                         {QStringLiteral("converter.py"),
                                          QStringLiteral("https://github.com/kaixinol/BiliCache2MP4/raw/main/converter.py")},
                                         };

    for (auto it = scripts.cbegin(); it != scripts.cend(); ++it) {
        const auto filePath = QDir(appDir).absoluteFilePath(it.key());

        if (QFile::exists(filePath)) {
            appendLog(QStringLiteral("✓ %1 已就绪").arg(it.key()));
            continue;
        }
        appendLog(QStringLiteral("检测到缺失: %1，尝试通过代理下载...").arg(it.key()));
        if (!downloadFile(QString::fromLatin1(kProxyPrefix) + it.value(), filePath)) {
            success = false;
        }
    }

    return success;
}

void MainWindow::on_pushButtonBrowseFile_clicked()
{
    const auto dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择B站缓存文件夹"),
        ui->lineEditFile->text()
        );

    if (!dir.isEmpty()) {
        ui->lineEditFile->setText(QDir::toNativeSeparators(dir));
    }
}

void MainWindow::on_pushButtonBrowseOutput_clicked()
{
    const auto dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择输出目录"),
        ui->lineEditOutput->text()
        );

    if (!dir.isEmpty()) {
        ui->lineEditOutput->setText(QDir::toNativeSeparators(dir));
    }
}

void MainWindow::on_pushButtonBrowseFfmpeg_clicked()
{
    const auto file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择ffmpeg.exe"),
        QString(),
        QStringLiteral("FFmpeg (ffmpeg.exe);;All Files (*)")
        );

    if (!file.isEmpty()) {
        ui->lineEditFfmpeg->setText(QDir::toNativeSeparators(file));
    }
}

QString MainWindow::findFFmpegExecutable()
{
    const auto inputFfmpeg = ui->lineEditFfmpeg->text().trimmed();

    if (!inputFfmpeg.isEmpty()) {
        const QFileInfo checkFile(inputFfmpeg);
        if (checkFile.exists() && checkFile.isFile()) {
            return checkFile.absoluteFilePath();
        }
        return {};
    }

#ifdef Q_OS_WIN
    constexpr auto ffmpegName = "ffmpeg.exe";
#else
    constexpr auto ffmpegName = "ffmpeg";
#endif

    return QStandardPaths::findExecutable(QString::fromLatin1(ffmpegName));
}

QString MainWindow::findPythonExecutable()
{
    if (!cachedPythonPath.isEmpty()) {
        return cachedPythonPath;
    }

    const QStringList candidates{
        QStringLiteral("python3"),
        QStringLiteral("py"),
        QStringLiteral("python")
    };

    for (const auto &cmd : candidates) {
        const auto fullPath = QStandardPaths::findExecutable(cmd);
        if (fullPath.isEmpty()) {
            continue;
        }

        QProcess probe;
        probe.start(fullPath, {QStringLiteral("--version")});
        if (probe.waitForFinished(500) && probe.exitCode() == 0) {
            cachedPythonPath = fullPath;
            return fullPath;
        }
    }

    return {};
}

void MainWindow::on_pushButtonStart_clicked()
{

    ui->plainTextEditLog->clear();
    const auto inputPath = ui->lineEditFile->text().trimmed();
    QDir rootDir(inputPath);
    if (!rootDir.exists()) {
        appendLog(QStringLiteral("错误: 输入路径不存在"));
        return;
    }

    bool structureOk = false;
    const auto subDirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &sub : subDirs) {
        QDir subDir(rootDir.absoluteFilePath(sub));
        // 检查 entry.json 是否在当前子目录或下一级数字目录中
        if (subDir.exists(QStringLiteral("entry.json")) ||
            !subDir.entryList({QStringLiteral("entry.json")}, QDir::Files, QDir::Name).isEmpty()) {
            structureOk = true;
            break;
        }
        // 继续深度检查一级（适配 AV号/分P号/entry.json 结构）
        const auto nestedDirs = subDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &nested : nestedDirs) {
            if (QFile::exists(subDir.absoluteFilePath(nested + QStringLiteral("/entry.json")))) {
                structureOk = true;
                break;
            }
        }
        if (structureOk) break;
    }

    if (!structureOk) {
        appendLog(QStringLiteral("警告: 文件夹结构不符合B站缓存规范 (未发现 entry.json)"));
        appendLog(QStringLiteral(R"(请确保文件夹类似：
C:\USERS\EXAMPLE\DESKTOP\缓存文件夹
├───115302541xxxxxx
│   └───c_327853xxxxx
│       └───80
├───11541298545xxxx
│   └───c_332639xxxxx
│       └───80
├───11543071903xxxx
│   └───c_333675xxxx
│       └───80
├───1155206448xxxx
│   └───c_338660xxxx
│       └───80
└───1155667989xxxx
    └───c_340766xxxxx
        └───80)"));
    }
    appendLog(QStringLiteral("===== 准备启动转换任务 ====="));

    const auto appDir = QCoreApplication::applicationDirPath();
    auto outputPath = ui->lineEditOutput->text().trimmed();

    const auto entryScript = QDir(appDir).absoluteFilePath(QStringLiteral("converter.py"));
    const auto danmakuScript = QDir(appDir).absoluteFilePath(QStringLiteral("danmaku2ass.py"));

    const auto pythonAbsPath = findPythonExecutable();
    if (pythonAbsPath.isEmpty()) {
        appendLog(QStringLiteral("错误: 系统未检测到 Python，请安装并添加到环境变量"));
        return;
    }

    const auto ffmpegAbsPath = findFFmpegExecutable();
    if (ffmpegAbsPath.isEmpty()) {
        appendLog(QStringLiteral("错误: 无法定位 FFmpeg 绝对路径，请检查设置或环境变量"));
        return;
    }

    if (!QFile::exists(entryScript) || !QFile::exists(danmakuScript)) {
        appendLog(QStringLiteral("正在检查环境脚本..."));
        if (!ensureRequiredScripts()) {
            appendLog(QStringLiteral("错误: 关键脚本 (converter.py 或 danmaku2ass.py) 缺失且下载失败"));
            return;
        }
    }

    if (outputPath.isEmpty()) {
        const QFileInfo inputInfo(inputPath);
        const auto baseDir = inputInfo.absolutePath();
        outputPath = QDir(baseDir).absoluteFilePath(
            inputInfo.completeBaseName() + QStringLiteral("_converted_videos")
            );

        QDir().mkpath(outputPath);
        ui->lineEditOutput->setText(QDir::toNativeSeparators(outputPath));
        appendLog(QStringLiteral("提示: 已自动设置输出目录为同级目录"));
    }

    ui->pushButtonStart->setEnabled(false);

    appendLog(QStringLiteral("--- 运行环境详情 ---"));
    appendLog(QStringLiteral("Python 解释器 : %1").arg(pythonAbsPath));
    appendLog(QStringLiteral("FFmpeg 程序   : %1").arg(ffmpegAbsPath));
    appendLog(QStringLiteral("主入口脚本    : %1").arg(entryScript));
    appendLog(QStringLiteral("弹幕转换脚本  : %1").arg(danmakuScript));
    appendLog(QStringLiteral("输入数据目录  : %1").arg(QFileInfo(inputPath).absoluteFilePath()));
    appendLog(QStringLiteral("转换输出目录  : %1").arg(QFileInfo(outputPath).absoluteFilePath()));
    appendLog(QStringLiteral("-------------------"));

    QStringList args;
    args << entryScript
         << QStringLiteral("-f") << QDir::toNativeSeparators(ffmpegAbsPath);

    if (ui->checkBoxFolder->isChecked()) {
        args << QStringLiteral("-folder");
    }
    if (ui->checkBoxDanmaku->isChecked()) {
        args << QStringLiteral("-danmaku");
    }
    if (ui->checkBoxNfo->isChecked()) {
        args << QStringLiteral("-nfo");
    }

    args << QStringLiteral("-t") << QString::number(ui->spinBoxThread->value())
         << QDir::toNativeSeparators(inputPath)
         << QStringLiteral("-o") << QDir::toNativeSeparators(outputPath);

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8"); // 强制 Python 输出编码为 utf-8
    process->setProcessEnvironment(env);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        // 改用 fromLocal8Bit 兼容 Windows 系统的本地编码
        const auto out = QString::fromUtf8(process->readAllStandardOutput());

        const auto lines = out.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                     Qt::SkipEmptyParts);
        for (const auto &line : lines) {
            appendLog(line.trimmed());
        }
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int code, QProcess::ExitStatus status) {
                if (status == QProcess::NormalExit && code == 0) {
                    appendLog(QStringLiteral("===== 任务全部完成 ====="));
                } else {
                    appendLog(QStringLiteral("===== 任务异常中止 (退出码: %1) =====").arg(code));
                }

                ui->progressBar->setRange(0, 100);
                ui->progressBar->setValue(100);
                ui->pushButtonStart->setEnabled(true);
                process->deleteLater();
            });

    appendLog(QStringLiteral("启动转换引擎..."));
    process->start(pythonAbsPath, args);
    ui->progressBar->setRange(0, 0);
}

void MainWindow::on_pushButtonClearLog_clicked()
{
    ui->plainTextEditLog->clear();
}