#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSvgRenderer>
#include <QTimer>
#include <QLabel>
#include <QStringList>
#include <QWidget>
#include <QPushButton>
#include "powermonitor.h"

class BatteryWidget : public QLabel {
    Q_OBJECT
public:
    explicit BatteryWidget(QWidget *parent = nullptr);
    void setBatteryLevel(int level);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int batteryLevel;
    QSvgRenderer *renderer;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    enum AnimationType {
        None,
        Eat,
        Sad,
        Welcome,
        Blink,
        Boredom
    };

private:
    QLabel *animationLabel;
    QTimer *frameTimer;
    QTimer *resetTimer;
    QTimer *blinkTimer;

    QTimer *boredomTimer; // Таймер для анимации скуки

    QStringList framePaths;
    int currentFrame;
    bool isEatAnimationInfinite;
    bool lab1Activated;
    QList<QPushButton*> labButtons;
    AnimationType currentAnimationType;

    QWidget *powerInfoPanel;
    QLabel *titleLabel;
    QLabel *powerSourceLabel;
    QLabel *batteryTypeLabel;
    BatteryWidget *batteryWidget;
    QLabel *powerModeStatusLabel;
    QLabel *dischargeDurationLabel;
    QLabel *remainingBatteryTimeLabel;
    QPushButton *sleepButton;
    QPushButton *hibernateButton;
    QPushButton *backButton;
    PowerMonitor *powerMonitor;

    void loadSadFrames();
    void loadEatFrames();
    void loadWelcomeFrames();
    void loadBlinkFrames();
    void loadBoredomFrames();
    void drawBackground(bool center = true);
    void updateFrame();
    void setupPowerInfoPanel();
    void startBlinkAnimation();
    void restorePreviousAnimation(AnimationType prevType);


    void activatePowerInfoPanel();


private slots:
    void startSadAnimation();
    void startEatAnimation();
    void startWelcomeAnimation();
    void startBoredomAnimation();
    void showPowerInfo();
    void hidePowerInfo();
    void updatePowerInfo(const QString &powerSource, const QString &batteryType, int level,
                         bool powerSavingEnabled, const QTime &dischargeDuration,
                         const QTime &remainingTime);
    void updatePowerMode(const QString &mode);
    void triggerBlinkAnimation();

    void checkBoredom(); // Новый слот для проверки и запуска анимации скуки

};

#endif // MAINWINDOW_H
