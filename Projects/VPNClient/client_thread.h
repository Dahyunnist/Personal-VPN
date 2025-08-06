#ifndef CLIENT_THREAD_H
#define CLIENT_THREAD_H

#include <QThread>
#include <QString>

class ClientCallerThread : public QThread{
    Q_OBJECT

public:
    explicit ClientCallerThread(const QString& configPath, const QString& routeIp, QObject* parent = nullptr);
    void run() override;
    void stop();

    signals:
    void finished(int exitCode);
    void logOutput(const QString& log);

private:
    QString m_configPath;
    QString m_routeIp;
    bool m_stopRequested = false;
};





#endif