#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    this->lab = new QLabel("Hello, World!", this);
}

MainWindow::~MainWindow() {}
