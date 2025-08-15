/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.5.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
   public:
    QWidget *centralWidget;
    QVBoxLayout *verticalLayout;
    QTabWidget *tabWidget;
    QWidget *tab;
    QVBoxLayout *verticalLayout_3;
    QGroupBox *serverGroup;
    QFormLayout *formLayout;
    QLabel *label;
    QLineEdit *serverIp;
    QLabel *label_2;
    QLineEdit *serverPort;
    QLabel *label1;
    QLineEdit *tunIp;
    QLabel *label_3;
    QLineEdit *routeIp;
    QGroupBox *certGroup;
    QFormLayout *formLayout_2;
    QLabel *label_4;
    QHBoxLayout *horizontalLayout;
    QLineEdit *configPath;
    QPushButton *browseConfigPath;
    QHBoxLayout *horizontalLayout_5;
    QPushButton *connectButton;
    QPushButton *disconnectButton;
    QWidget *tab_2;
    QVBoxLayout *verticalLayout_4;
    QPlainTextEdit *logOutput;
    QGroupBox *testGroup;
    QVBoxLayout *verticalLayout_2;
    QPushButton *testConnectionBtn;
    QLabel *testStatus;
    QPlainTextEdit *testOutput;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(600, 650);
        centralWidget = new QWidget(MainWindow);
        centralWidget->setObjectName("centralWidget");
        verticalLayout = new QVBoxLayout(centralWidget);
        verticalLayout->setObjectName("verticalLayout");
        tabWidget = new QTabWidget(centralWidget);
        tabWidget->setObjectName("tabWidget");
        tab = new QWidget();
        tab->setObjectName("tab");
        verticalLayout_3 = new QVBoxLayout(tab);
        verticalLayout_3->setObjectName("verticalLayout_3");
        serverGroup = new QGroupBox(tab);
        serverGroup->setObjectName("serverGroup");
        formLayout = new QFormLayout(serverGroup);
        formLayout->setObjectName("formLayout");
        label = new QLabel(serverGroup);
        label->setObjectName("label");

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        serverIp = new QLineEdit(serverGroup);
        serverIp->setObjectName("serverIp");

        formLayout->setWidget(0, QFormLayout::FieldRole, serverIp);

        label_2 = new QLabel(serverGroup);
        label_2->setObjectName("label_2");

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        serverPort = new QLineEdit(serverGroup);
        serverPort->setObjectName("serverPort");

        formLayout->setWidget(1, QFormLayout::FieldRole, serverPort);

        label1 = new QLabel(serverGroup);
        label1->setObjectName("label1");

        formLayout->setWidget(2, QFormLayout::LabelRole, label1);

        tunIp = new QLineEdit(serverGroup);
        tunIp->setObjectName("tunIp");

        formLayout->setWidget(2, QFormLayout::FieldRole, tunIp);

        label_3 = new QLabel(serverGroup);
        label_3->setObjectName("label_3");

        formLayout->setWidget(3, QFormLayout::LabelRole, label_3);

        routeIp = new QLineEdit(serverGroup);
        routeIp->setObjectName("routeIp");

        formLayout->setWidget(3, QFormLayout::FieldRole, routeIp);

        verticalLayout_3->addWidget(serverGroup);

        certGroup = new QGroupBox(tab);
        certGroup->setObjectName("certGroup");
        formLayout_2 = new QFormLayout(certGroup);
        formLayout_2->setObjectName("formLayout_2");
        label_4 = new QLabel(certGroup);
        label_4->setObjectName("label_4");

        formLayout_2->setWidget(0, QFormLayout::LabelRole, label_4);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        configPath = new QLineEdit(certGroup);
        configPath->setObjectName("configPath");

        horizontalLayout->addWidget(configPath);

        browseConfigPath = new QPushButton(certGroup);
        browseConfigPath->setObjectName("browseConfigPath");

        horizontalLayout->addWidget(browseConfigPath);

        formLayout_2->setLayout(0, QFormLayout::FieldRole, horizontalLayout);

        verticalLayout_3->addWidget(certGroup);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        connectButton = new QPushButton(tab);
        connectButton->setObjectName("connectButton");
        connectButton->setStyleSheet(QString::fromUtf8("background-color: rgb(73, 188, 119);"));

        horizontalLayout_5->addWidget(connectButton);

        disconnectButton = new QPushButton(tab);
        disconnectButton->setObjectName("disconnectButton");
        disconnectButton->setEnabled(false);
        disconnectButton->setStyleSheet(QString::fromUtf8("background-color: rgb(239, 71, 111);"));

        horizontalLayout_5->addWidget(disconnectButton);

        verticalLayout_3->addLayout(horizontalLayout_5);

        tabWidget->addTab(tab, QString());
        tab_2 = new QWidget();
        tab_2->setObjectName("tab_2");
        verticalLayout_4 = new QVBoxLayout(tab_2);
        verticalLayout_4->setObjectName("verticalLayout_4");
        logOutput = new QPlainTextEdit(tab_2);
        logOutput->setObjectName("logOutput");
        logOutput->setReadOnly(true);

        verticalLayout_4->addWidget(logOutput);

        tabWidget->addTab(tab_2, QString());

        verticalLayout->addWidget(tabWidget);

        testGroup = new QGroupBox(centralWidget);
        testGroup->setObjectName("testGroup");
        verticalLayout_2 = new QVBoxLayout(testGroup);
        verticalLayout_2->setObjectName("verticalLayout_2");
        testConnectionBtn = new QPushButton(testGroup);
        testConnectionBtn->setObjectName("testConnectionBtn");
        testConnectionBtn->setEnabled(false);
        testConnectionBtn->setStyleSheet(QString::fromUtf8("background-color: rgb(255, 209, 77);"));

        verticalLayout_2->addWidget(testConnectionBtn);

        testStatus = new QLabel(testGroup);
        testStatus->setObjectName("testStatus");
        testStatus->setStyleSheet(QString::fromUtf8("font-weight: bold;"));

        verticalLayout_2->addWidget(testStatus);

        testOutput = new QPlainTextEdit(testGroup);
        testOutput->setObjectName("testOutput");
        testOutput->setReadOnly(true);

        verticalLayout_2->addWidget(testOutput);

        verticalLayout->addWidget(testGroup);

        MainWindow->setCentralWidget(centralWidget);
        statusBar = new QStatusBar(MainWindow);
        statusBar->setObjectName("statusBar");
        MainWindow->setStatusBar(statusBar);

        retranslateUi(MainWindow);

        tabWidget->setCurrentIndex(0);

        QMetaObject::connectSlotsByName(MainWindow);
    }    // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "VPN\345\256\242\346\210\267\347\253\257", nullptr));
        serverGroup->setTitle(QCoreApplication::translate("MainWindow", "\350\256\276\345\244\207\351\205\215\347\275\256", nullptr));
        label->setText(QCoreApplication::translate("MainWindow", "\346\234\215\345\212\241\345\231\250IP:", nullptr));
        serverIp->setText(QString());
        label_2->setText(QCoreApplication::translate("MainWindow", "\347\253\257\345\217\243:", nullptr));
        serverPort->setText(QString());
        label1->setText(QCoreApplication::translate("MainWindow", "\345\256\242\346\210\267\347\253\257TUN\350\256\276\345\244\207IP:", nullptr));
        tunIp->setText(QString());
        label_3->setText(QCoreApplication::translate("MainWindow", "\350\267\257\347\224\261IP:", nullptr));
        routeIp->setText(QCoreApplication::translate("MainWindow", "1.1.1.1", nullptr));
        certGroup->setTitle(QCoreApplication::translate("MainWindow", "\351\205\215\347\275\256\345\257\274\345\205\245", nullptr));
        label_4->setText(QCoreApplication::translate("MainWindow", "\351\205\215\347\275\256\346\226\207\344\273\266:", nullptr));
        browseConfigPath->setText(QCoreApplication::translate("MainWindow", "\346\265\217\350\247\210", nullptr));
        connectButton->setText(QCoreApplication::translate("MainWindow", "\350\277\236\346\216\245VPN", nullptr));
        disconnectButton->setText(QCoreApplication::translate("MainWindow", "\346\226\255\345\274\200\350\277\236\346\216\245", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab), QCoreApplication::translate("MainWindow", "\350\277\236\346\216\245\351\205\215\347\275\256", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_2), QCoreApplication::translate("MainWindow", "\346\227\245\345\277\227\350\276\223\345\207\272", nullptr));
        testGroup->setTitle(QCoreApplication::translate("MainWindow", "\347\275\221\347\273\234\350\277\236\351\200\232\346\265\213\350\257\225", nullptr));
        testConnectionBtn->setText(QCoreApplication::translate("MainWindow", "\346\265\213\350\257\225\350\277\236\351\200\232", nullptr));
        testStatus->setText(QCoreApplication::translate("MainWindow", "\346\234\252\346\265\213\350\257\225", nullptr));
    }    // retranslateUi
};

namespace Ui
{
class MainWindow : public Ui_MainWindow
{
};
}    // namespace Ui

QT_END_NAMESPACE

#endif    // UI_MAINWINDOW_H
