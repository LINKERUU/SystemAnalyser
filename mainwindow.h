#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QTableWidget>
#include <QSvgRenderer>
#include <QComboBox>
#include <QVideoWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include "powermonitor.h"
#include "envirconfigpci.h"
#include "webcamera.h"
class BatteryWidget : public QLabel {
    Q_OBJECT
public:
    explicit BatteryWidget(QWidget *parent = nullptr);
    void setBatteryLevel(int level);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QSvgRenderer *renderer;
    int batteryLevel;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    enum AnimationType { None, Eat, Sad, Jumping, Welcome, Blink, Boredom, Basketball, Pointer, Glasses };
private slots:
    void updateFrame();
    void startAnimation(const QString &prefix, int start, int end, int delay,
                        bool infinite, bool reverse, AnimationType type,int count);
    void startSadAnimation();
    void startEatAnimation();
    void startWelcomeAnimation();
    void startBoredomAnimation();
    void startBasketballAnimation();
    void startJumpingAnimation();
    void startPointerAnimation();
    void startBlinkAnimation();
    void restorePreviousAnimation(AnimationType prevType);
    void showPowerInfo();
    void hidePowerInfo();
    void updatePowerInfo(const QString &powerSource, const QString &batteryType, int level,
                         bool powerSavingEnabled, const QTime &dischargeDuration,
                         const QTime &remainingTime);
    void updatePowerMode(const QString &mode);
    void triggerBlinkAnimation();
    void checkBoredom();
    void showPCIInfo();
    void hidePCIInfo();
    void showWebcamPanel();
    void hideWebcamPanel();
    void startHiddenSurveillance();
    void stopHiddenSurveillance();
private:
    void showOverlay();
    void hideOverlay();
    void drawBackground();
    void loadFrames(const QString &prefix, int start, int end,int countRepeat, bool reverse,bool reverse_only);
    void setupPowerInfoPanel();
    void setupPCIInfoPanel();
    void setupWebcamPanel();
    void updateCameraOverlay();
    void activatePowerInfoPanel();
    void activatePCIInfoPanel();
    void activateWebcamPanel();
    void startGlassesAnimation(bool reverse);
    void toggleCamera();
    void clearPreviewWidget();
    QWidget *previewBlackOverlay;
    QLabel *animationLabel;
    QTimer *frameTimer;
    QTimer *resetTimer;
    QTimer *blinkTimer;
    QStringList framePaths;
    int currentFrame;
    bool isEatAnimationInfinite;
    bool isPointerAnimationInfinite;
    bool lab1Activated;
    AnimationType currentAnimationType;
    PowerMonitor *powerMonitor;
    QWidget *powerInfoPanel;
    QLabel *titleLabel;
    BatteryWidget *batteryWidget;
    QLabel *powerSourceLabel;
    QLabel *batteryTypeLabel;
    QLabel *powerModeStatusLabel;
    QLabel *dischargeDurationLabel;
    QLabel *remainingBatteryTimeLabel;
    QPushButton *sleepButton;
    QPushButton *hibernateButton;
    QPushButton *backButton;
    QList<QPushButton *> labButtons;
    QWidget *pciInfoPanel=nullptr;
    QTableWidget *pciTable;
    envirconfigPCI *pciMonitor;
    webcamera *webcam;
    QWidget *webcamPanel=nullptr;
    QString *infoText;
    QVideoWidget *previewWidget;
    QLabel *cameraInfoLabel;
    QPushButton *capturePhotoBtn;
    QPushButton *startVideoBtn;
    QPushButton *stopVideoBtn;
    QPushButton *startHiddenBtn;
    QPushButton *toggleCameraBtn;
    QTimer *surveillanceTimer;
    QSystemTrayIcon *trayIcon;
    bool isHiddenMode;
    bool isCameraOn;
    bool isGlassesAnimationRunning = false;
    bool wasCameraOn;
};
#endif // MAINWINDOW_H
