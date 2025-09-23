#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QTableWidget>
#include <QSvgRenderer>
#include "powermonitor.h"
#include "envirconfigpci.h"

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

    enum AnimationType { None, Eat, Sad, Welcome, Blink, Boredom };

private slots:
    void updateFrame();
    void startSadAnimation();
    void startEatAnimation();
    void startWelcomeAnimation();
    void startBoredomAnimation();
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

private:
    void drawBackground(bool center = false);
    void loadSadFrames();
    void loadEatFrames();
    void loadWelcomeFrames();
    void loadBlinkFrames();
    void loadBoredomFrames();
    void setupPowerInfoPanel();
    void setupPCIInfoPanel();
    void activatePowerInfoPanel();

    QLabel *animationLabel;
    QTimer *frameTimer;
    QTimer *resetTimer;
    QTimer *blinkTimer;
    QStringList framePaths;
    int currentFrame;
    bool isEatAnimationInfinite;
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
    QWidget *pciInfoPanel;
    QTableWidget *pciTable;
    envirconfigPCI *pciMonitor;
};

#endif // MAINWINDOW_H
