#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QFileDialog>
#include <QDir>
#include "client_thread.h"
#include <future>
#include <thread>
#include <atomic>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // connect and disconnect
    void on_connectButton_clicked();
    void on_disconnectButton_clicked();

    // void on_refreshInterfaces_clicked();
    void on_browseConfigPath_clicked();

    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);

    // test connection
    void on_testConnectionBtn_clicked();
    void on_testProcessOutput();
    void on_testProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    Ui::MainWindow *ui;
    QProcess *vpnProcess;
    QProcess *testProcess;
    bool isConnected;
    QString testTarget;

    void saveSettings();
    void loadSettings();
    void setConnectedState(bool connected);
    void startVPNClient();
    void stopVPNClient();
    void runTestCommands();
    void parseTestResult(bool ping_success, bool curl_success);
};

#endif
