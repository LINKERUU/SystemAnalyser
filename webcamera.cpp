#include "webcamera.h"
#include <QDebug>

webcamera::webcamera(QObject *parent) : QObject(parent)
{
    session = new QMediaCaptureSession(this);
    imageCapture = new QImageCapture(this);
    session->setImageCapture(imageCapture);
    recorder = new QMediaRecorder(this);
    session->setRecorder(recorder);
}

webcamera::~webcamera()
{
    if (camera) {
        camera->stop();
        delete camera;
    }
}

QStringList webcamera::getAvailableCameras()
{
    QStringList list;
    auto devices = QMediaDevices::videoInputs();
    for (const auto &dev : devices) {
        list << dev.description();
    }
    return list;
}

QList<QCameraDevice> webcamera::getCameraDevices()
{
    return QMediaDevices::videoInputs();
}

void webcamera::setCamera(const QCameraDevice &device)
{
    if (camera) {
        camera->stop();
        delete camera;
    }
    camera = new QCamera(device, this);
    session->setCamera(camera);
    camera->start();
}

void webcamera::capturePhoto(const QString &filePath)
{
    imageCapture->captureToFile(filePath);
}

void webcamera::startVideoRecord(const QString &filePath)
{
    recorder->setOutputLocation(QUrl::fromLocalFile(filePath));
    recorder->setQuality(QMediaRecorder::HighQuality);
    recorder->record();
}

void webcamera::stopCamera() {
    if (camera && camera->isActive()) {
        camera->stop();
    }
}

void webcamera::stopVideoRecord()
{
    recorder->stop();
}

void webcamera::setVideoOutput(QVideoWidget *output)
{
    session->setVideoOutput(output);
}
