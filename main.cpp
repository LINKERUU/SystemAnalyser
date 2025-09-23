#include <QApplication>
#include <QPixmap>
#include <QTransform>
#include <QIcon>
#include <QDir>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QString sourceFilePath = __FILE__;
    QDir sourceDir(sourceFilePath);

    QPixmap pixmap(sourceDir.absoluteFilePath("../assets/") + "/icon.svg");
    QTransform transform;
    QPixmap rotatedPixmap = pixmap.transformed(transform, Qt::SmoothTransformation);

    MainWindow w;
    w.setWindowIcon(QIcon(rotatedPixmap));
    w.show();

    return a.exec();
}
