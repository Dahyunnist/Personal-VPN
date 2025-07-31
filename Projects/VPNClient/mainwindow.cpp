#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QUuid>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , vpnProcess(new QProcess(this))
    , testProcess(new QProcess(this))
    , isConnected(false)
    , testTarget("") 
    // , tempExtractDir("")
{
    ui->setupUi(this);
    // ui->refreshInterfaces->hide();

    // connect(ui->importConfigBtn, &QPushButton::clicked, this, &MainWindow::on_importConfig_clicked);
    connect(vpnProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::readProcessOutput);
    connect(vpnProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::processFinished);
    connect(testProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::on_testProcessOutput);
    connect(testProcess, &QProcess::readyReadStandardError, this, &MainWindow::on_testProcessOutput);
    connect(testProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::on_testProcessFinished);

    // tempExtractDir = QDir::tempPath() + "/vpn_config_temp_" + QString::number(QCoreApplication::applicationPid());
    // QDir().mkpath(tempExtractDir);

    ui->serverIp->setEnabled(false);
    ui->serverPort->setEnabled(false);
    ui->tunIp->setEnabled(false);
    ui->configPath->setEnabled(false);
    ui->routeIp->setEnabled(true);

    // loadSettings();
    // updateInterfaceList();
    setConnectedState(false);

    // ui->tunInterface->hide();

    ui->testOutput->clear();
    ui->testStatus->setText("未测试");
}

MainWindow::~MainWindow()
{
    stopVPNClient();
    if(testProcess->state() == QProcess::Running){
        testProcess->terminate();
        testProcess->waitForFinished(1000);
    }
    // if(!tempExtractDir.isEmpty() && QDir(tempExtractDir).exists()){
    //     QDir(tempExtractDir).removeRecursively();
    // }

    disconnect(vpnProcess, nullptr, this, nullptr);
    disconnect(testProcess, nullptr, this, nullptr);

    delete testProcess;
    testProcess = nullptr;
    delete vpnProcess;
    vpnProcess = nullptr;

    delete ui;
}

void MainWindow::on_browseConfigPath_clicked(){
    QString config_path = QFileDialog::getOpenFileName(this, "选择配置文件", "", "配置文件(*.json);;");
    if(config_path.isEmpty()){
        return;
    }
    ui->configPath->setText(config_path);

    QFile configFile(config_path);
    if(!configFile.open(QIODevice::ReadOnly)){
        QMessageBox::critical(this, "导入失败", "无法读取config.json");
        // QDir(tempExtractDir).removeRecursively();
        return;
    }
    QJsonParseError jsonErr;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(configFile.readAll(), &jsonErr);
    if(jsonErr.error != QJsonParseError::NoError){
        QMessageBox::critical(this, "导入失败", "config.json格式错误：" + jsonErr.errorString());
        // QDir(tempExtractDir).removeRecursively();
        return;
    }

    QJsonObject root = jsonDoc.object();
    QJsonObject server = root["server"].toObject();
    // QJsonObject certPaths = root["cert_paths"].toObject();
    QJsonObject tun = root["tun"].toObject();

    QString server_ip = server["ip"].toString();
    QString server_port = QString::number(server["port"].toInt());
    QString tun_ip = tun["ip"].toString();
    if(server_ip.isEmpty() || server_port.isEmpty() || tun_ip.isEmpty()){
        QMessageBox::critical(this, "导入失败", "config.json中配置信息缺失");
        // QDir(tempExtractDir).removeRecursively();
        return;
    }

    ui->serverIp->setText(server_ip);
    ui->serverPort->setText(server_port);
    ui->tunIp->setText(tun_ip);
    QMessageBox::information(this, "导入成功", "配置文件导入成功，请确认路由IP后连接");
    saveSettings();

}

void MainWindow::on_connectButton_clicked(){
    if(ui->serverIp->text().isEmpty() ||
        ui->serverPort->text().isEmpty() ||
        ui->configPath->text().isEmpty() ||
        // ui->clientCertPath->text().isEmpty() ||
        // ui->clientKeyPath->text().isEmpty() ||
        ui->routeIp->text().isEmpty() ||
        ui->tunIp->text().isEmpty()){
            QMessageBox::warning(this, "信息不完整", "请填写所有必填字段");
            return;
        }
    startVPNClient();
}

void MainWindow::on_disconnectButton_clicked(){
    stopVPNClient();
}

// void MainWindow::on_refreshInterfaces_clicked(){
//     updateInterfaceList();
// }

void MainWindow::readProcessOutput(){
    ui->logOutput->appendPlainText(vpnProcess->readAllStandardOutput());
}

void MainWindow::processFinished(int exitCode, QProcess::ExitStatus exitStatus){
    Q_UNUSED(exitStatus);
    ui->logOutput->appendPlainText(QString("VPN进程已退出，代码：%1").arg(exitCode));
    setConnectedState(false);
}

void MainWindow::saveSettings(){
    QSettings settings("MyCompany", "VPNClient");
    settings.setValue("serverIp", ui->serverIp->text());
    settings.setValue("serverPort", ui->serverPort->text());
    settings.setValue("configPath", ui->configPath->text());
    // settings.setValue("clientCertPath", ui->clientCertPath->text());
    // settings.setValue("clientKeyPath", ui->clientKeyPath->text());
    settings.setValue("routeIp", ui->routeIp->text());
    settings.setValue("tunIp", ui->tunIp->text());
    ui->serverIp->setEnabled(false);
    ui->serverPort->setEnabled(false);
    ui->tunIp->setEnabled(false);
}

void MainWindow::loadSettings(){
    QSettings settings("MyCompany", "VPNClient");
    ui->serverIp->setText(settings.value("serverIp", "").toString());
    ui->serverPort->setText(settings.value("serverPort", "").toString());
    ui->configPath->setText(settings.value("configPath", "").toString());
    // ui->clientCertPath->setText(settings.value("clientCertPath", "").toString());
    // ui->clientKeyPath->setText(settings.value("clientKeyPath", "").toString());
    ui->routeIp->setText(settings.value("routeIp", "1.1.1.1").toString());
    ui->tunIp->setText(settings.value("tunIp", "").toString());
}

void MainWindow::setConnectedState(bool connected){
    isConnected = connected;
    ui->connectButton->setEnabled(!connected);
    ui->disconnectButton->setEnabled(connected);
    ui->testConnectionBtn->setEnabled(connected);
    ui->serverIp->setEnabled(false);
    ui->serverPort->setEnabled(false);
    ui->tunIp->setEnabled(false);
    ui->routeIp->setEnabled(!connected);
    ui->configPath->setEnabled(false);
    ui->browseConfigPath->setEnabled(!connected);
    // ui->browseClientCert->setEnabled(!connected);
    // ui->browseClientKey->setEnabled(!connected);
}

void MainWindow::startVPNClient(){
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("CONFIG_PATH", ui->configPath->text());
    // env.insert("CLIENT_CERT_PATH", ui->clientCertPath->text());
    // env.insert("CLIENT_KEY_PATH", ui->clientKeyPath->text());
    vpnProcess->setProcessEnvironment(env);

    QStringList args;
    args << ui->routeIp->text();
    
    QString vpnExePath = "client.exe";
    if(!QFile::exists(vpnExePath)){
        QMessageBox::critical(this, "错误", "未找到VPN客户端程序：" + vpnExePath);
        return;
    }
    
    vpnProcess->start(vpnExePath, args);

    if(!vpnProcess->waitForStarted()){
        QMessageBox::critical(this, "错误", "无法启动VPN客户端进程");
        return;
    }
    setConnectedState(true);
    ui->logOutput->appendPlainText("VPN客户端已启动");
    saveSettings();
}

void MainWindow::stopVPNClient(){
    if(vpnProcess->state() == QProcess::NotRunning){
        return;
    }
    vpnProcess->terminate();
    if(!vpnProcess->waitForFinished(2000)){
        vpnProcess->kill();
        vpnProcess->waitForFinished(1000);
    }
    setConnectedState(false);
}

// void MainWindow::updateInterfaceList(){
//     ui->tunInterface->clear();
//     foreach(const QNetworkInterface &interface, QNetworkInterface::allInterfaces()){
//         if(interface.name().contains("VPN") || interface.name().contains("TAP") || interface.name().contains("TUN")){
//             ui->tunInterface->addItem(interface.name());
//         }
//     }
//     if(ui->tunInterface->count() == 0){
//         ui->tunInterface->addItem("未找到TUN设备");
//         ui->tunInterface->setEnabled(false);
//     }
// }

void MainWindow::on_testConnectionBtn_clicked(){
    if(!isConnected){
        QMessageBox::warning(this, "提示", "请先连接VPN再进行测试");
        return;
    }
    testTarget = ui->routeIp->text().trimmed();
    if(testTarget.isEmpty()){
        testTarget = "1.1.1.1";
        QMessageBox::information(this, "提示", "使用默认测试目标" + testTarget);
    }

    // initialize test UI
    ui->testOutput->clear();
    ui->testStatus->setText("测试中……");
    ui->testStatus->setStyleSheet("color: black;");
    ui->testConnectionBtn->setEnabled(false);

    // start test commands
    runTestCommands();
}

void MainWindow::on_testProcessOutput(){
    QByteArray output = testProcess->readAllStandardOutput() + testProcess->readAllStandardError();
    ui->testOutput->appendPlainText(QString::fromLocal8Bit(output));
}

void MainWindow::on_testProcessFinished(int exitCode, QProcess::ExitStatus exitStatus){
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);

    ui->testOutput->appendPlainText("\n=== 测试结束 ===");
    ui->testConnectionBtn->setEnabled(true);

    // resolve test result
    QString output = ui->testOutput->toPlainText();
    bool ping_success = output.contains(QRegularExpression("TTL=|往返行程的估计时间|Average =", QRegularExpression::CaseInsensitiveOption));
    // bool curl_success = output.contains(QRegularExpression("HTTP/\\d+\\.\\d+ (200|301|302)", QRegularExpression::CaseInsensitiveOption));
    bool curl_success = true;
    parseTestResult(ping_success, curl_success);
}


void MainWindow::runTestCommands(){
    // 1. 明确指定要执行的程序为 cmd.exe
    testProcess->setProgram("cmd.exe");

    // 2. 参数列表：用 /c 执行命令，参数通过 QStringList 传递（Qt 自动转义引号和空格）
    QString pingCmd = QString("ping -n 4 -w 2000 %1").arg(testTarget);
    QString curlCmd = QString("curl -v -m 5 http://%1").arg(testTarget);
    QString command = QString("(%1 && %2) || echo 测试失败; exit 0").arg(pingCmd).arg(curlCmd);

    // 参数列表：/c 后面直接跟要执行的命令（无需手动加引号，Qt 会自动处理）
    QStringList args;
    args << "/c" << command;

    // 3. 设置参数并启动进程
    testProcess->setArguments(args);

    // 输出命令到日志（用于验证）
    ui->testOutput->appendPlainText("=== 生成测试命令 ===");
    ui->testOutput->appendPlainText("程序: cmd.exe");
    ui->testOutput->appendPlainText("参数: " + args.join(" "));

    testProcess->start();  // 启动进程（无返回值）

    // 检查启动状态
    QProcess::ProcessState state = testProcess->state();
    bool startSuccess = (state == QProcess::Running || state == QProcess::Starting);
    ui->testOutput->appendPlainText("testProcess 启动结果: " + QString(startSuccess ? "成功" : "失败"));
}

void MainWindow::parseTestResult(bool ping_success, bool curl_success){
    if(ping_success && curl_success){
        ui->testStatus->setText("测试通过✅");
        ui->testStatus->setStyleSheet("color: green; font-weight: bold;");
        QMessageBox::information(this, "测试结果", "网络连通性测试通过！");
    }
    else{
        QString errorMsg = "测试失败：\n";
        if(!ping_success){
            errorMsg += "   ping 失败\n";
        }
        if(!curl_success){
            errorMsg += "   curl 失败\n";
        }
        ui->testStatus->setText("测试失败❌");
        ui->testStatus->setStyleSheet("color: red; font-weight: bold;");
        QMessageBox::critical(this, "测试结果", errorMsg);
    }
}