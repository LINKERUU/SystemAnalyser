#ifndef POWERMONITOR_H
#define POWERMONITOR_H

#include <QObject>
#include <QTime>
#include <QElapsedTimer>

class QTimer;

class PowerMonitor : public QObject {
    Q_OBJECT
public:
    explicit PowerMonitor(QObject *parent = nullptr);
    ~PowerMonitor();

    void startMonitoring();
    void stopMonitoring();
    void triggerSleep();
    void triggerHibernate();

    QString getPowerSourceType() const;
    QString getBatteryType() const;
    int getBatteryLevel() const;
    bool isPowerSavingEnabled() const;
    QString getPowerMode() const;
    QTime getDischargeDuration() const;
    QTime getRemainingBatteryTime() const;

signals:
    void powerSourceChanged(const QString &type);
    void batteryTypeChanged(const QString &type);
    void batteryLevelChanged(int level);
    void powerSavingEnabledChanged(bool enabled);
    void powerModeChanged(const QString &mode);
    void dischargeDurationChanged(const QTime &duration);
    void remainingBatteryTimeChanged(const QTime &time);

private slots:
    void updateData();

private:
    QTimer *timer;
    QString powerSource;
    QString batteryType;
    QString currentPowerMode;
    int lastBatteryLevel;
    int batteryLevel;
    bool powerSavingEnabled;
    bool isBatteryDischarging;
    QTime dischargeDuration;
    QTime remainingBatteryTime;
    QElapsedTimer dischargeElapsedTimer;
    QElapsedTimer remainingElapsedTimer;
    int lastRemainingSeconds;
};

#endif // POWERMONITOR_H
