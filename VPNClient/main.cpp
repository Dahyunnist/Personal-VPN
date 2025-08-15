#include "mainwindow.h"

#include <QApplication>
#include "client.h"

int main(int argc, char* argv[])
{
    if (argc >= 2 && QString(argv[1]) == "--vpn-service")
    {
        const char* config_path = argv[2];
        const char* route_ip = argv[3];

        int exitCode = start_vpn_client(config_path, route_ip);
        return exitCode;
    }

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
