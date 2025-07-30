#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QFileDialog>
#include <QDir>

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
    // browse and select certificates
    // void on_browseCaCert_clicked();
    // void on_browseClientCert_clicked();
    // void on_browseClientKey_clicked();
    // connect and disconnect
    void on_connectButton_clicked();
    void on_disconnectButton_clicked();

    // void on_refreshInterfaces_clicked();
    void on_browseConfigPath_clicked();

    void readProcessOutput();
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

    // QString tempExtractDir;

    void saveSettings();
    void loadSettings();
    void setConnectedState(bool connected);
    void startVPNClient();
    void stopVPNClient();
    // void updateInterfaceList();

    void runTestCommands();
    void parseTestResult(bool ping_success, bool curl_success);

    bool importConfigFile(const QString &filePath);
    // void exportConfigZip(const QString &zipPath, const QString &destDir);
};

#endif // MAINWINDOW_H
