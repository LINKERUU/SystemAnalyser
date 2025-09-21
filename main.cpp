#include <QApplication>
#include <QPixmap>
#include <QTransform>
#include <QIcon>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QPixmap pixmap("D:/Coding/C++/SystmeAnalyser/assets/icon.svg");
    QTransform transform;
    QPixmap rotatedPixmap = pixmap.transformed(transform, Qt::SmoothTransformation);

    MainWindow w;
    w.setWindowIcon(QIcon(rotatedPixmap));
    w.show();

    return a.exec();
}
