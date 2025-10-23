#ifndef WEBCAMERA_H
#define WEBCAMERA_H

#include <QObject>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QMediaRecorder>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QUrl>
#include <QVideoWidget>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>

class webcamera : public QObject
{
    Q_OBJECT
public:
    explicit webcamera(QObject *parent = nullptr);
    ~webcamera();

    QStringList getAvailableCameras();
    QList<QCameraDevice> getCameraDevices();
    void setCamera(const QCameraDevice &device);
    void capturePhoto(const QString &filePath);
    void startVideoRecord(const QString &filePath);
    void stopVideoRecord();
    void stopCamera();
    void setVideoOutput(QVideoWidget *output);


private:
    QCamera *camera = nullptr;
    QMediaCaptureSession *session;
    QImageCapture *imageCapture;
    QMediaRecorder *recorder;
};

#endif // WEBCAMERA_H
