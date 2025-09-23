#include "powermonitor.h"
#include <QTimer>
#include <QDebug>
#include <QProcess>
#include <QMessageBox>
#include <QString> // Для QString::fromWCharArray

#ifdef Q_OS_WIN
#include <windows.h>
#include <powrprof.h>
#include <strsafe.h>
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib") // Для RegOpenKeyExA и RegQueryValueExA
#endif

PowerMonitor::PowerMonitor(QObject *parent) : QObject(parent),
    lastBatteryLevel(0), batteryLevel(0), powerSavingEnabled(false),
    isBatteryDischarging(false), lastRemainingSeconds(0) {
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PowerMonitor::updateData);
    powerSource = "Неизвестно";
    batteryType = "Неизвестно";
    dischargeDuration = QTime(0, 0, 0);
    remainingBatteryTime = QTime(0, 0, 0);
    dischargeElapsedTimer.invalidate();
    remainingElapsedTimer.invalidate();
    currentPowerMode = "Неизвестно";

#ifdef Q_OS_WIN
    // Предотвращаем автоматический переход в спящий режим
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
}

PowerMonitor::~PowerMonitor() {
#ifdef Q_OS_WIN
    SetThreadExecutionState(ES_CONTINUOUS);
#endif
    stopMonitoring();
}

void PowerMonitor::startMonitoring() {
    updateData();
    timer->start(1000); // Обновление каждую секунду
}

void PowerMonitor::stopMonitoring() {
    if (timer->isActive()) {
        timer->stop();
    }
}

void PowerMonitor::triggerSleep() {
#ifdef Q_OS_WIN
    if (!timer) return;

    qDebug() << "Инициируется спящий режим.";

    bool wasTimerActive = timer->isActive();
    if (wasTimerActive) timer->stop();

    SendMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM)2);

    if (!LockWorkStation()) {
        qDebug() << "Не удалось заблокировать рабочую станцию. Код ошибки:" << GetLastError();
    } else {
        qDebug() << "Рабочая станция заблокирована. Ожидание разблокировки...";
    }

    if (wasTimerActive) timer->start();
#else
    QMessageBox::warning(nullptr, "Ошибка спящего режима",
                         "Эмуляция спящего режима поддерживается только в Windows.");
#endif
}

void PowerMonitor::triggerHibernate() {
#ifdef Q_OS_WIN
    qDebug() << "Запрос на гибернацию (S4)";
    SetThreadExecutionState(ES_CONTINUOUS);
    BOOL success = SetSuspendState(TRUE, FALSE, FALSE);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    if (!success) {
        qDebug() << "Не удалось перейти в режим гибернации. Код ошибки:" << GetLastError();
        QMessageBox::warning(nullptr, "Ошибка гибернации",
                             "Не удалось перевести систему в режим гибернации.");
    } else {
        qDebug() << "Система успешно перешла в режим гибернации.";
    }
#else
    QMessageBox::warning(nullptr, "Ошибка гибернации",
                         "Режим гибернации поддерживается только в Windows.");
#endif
}

void PowerMonitor::updateData() {
#ifdef Q_OS_WIN
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        // Источник питания
        QString newPowerSource = (status.ACLineStatus == 1) ? "Сеть" : "Батарея";
        if (newPowerSource != powerSource) {
            powerSource = newPowerSource;
            emit powerSourceChanged(powerSource);
            if (powerSource == "Батарея") {
                isBatteryDischarging = true;
                dischargeElapsedTimer.restart();

                int systemSeconds = status.BatteryLifeTime;
                if (systemSeconds != -1 && systemSeconds != 0xFFFFFFFF) {
                    lastRemainingSeconds = systemSeconds;
                } else {
                    // Рассчитываем оставшееся время на основе уровня заряда
                    int batteryPercent = status.BatteryLifePercent <= 100 ? status.BatteryLifePercent : 100;
                    // Предполагаем, что полный заряд (100%) дает 4 часа (14400 секунд)
                    // Это значение можно настроить на основе характеристик батареи
                    const int fullBatteryLifeSeconds = 14400;
                    lastRemainingSeconds = (batteryPercent * fullBatteryLifeSeconds) / 100;
                }
                remainingElapsedTimer.restart();
                QTime initRem(lastRemainingSeconds / 3600, (lastRemainingSeconds % 3600) / 60, lastRemainingSeconds % 60);
                remainingBatteryTime = initRem;
                emit remainingBatteryTimeChanged(remainingBatteryTime);

                dischargeDuration = QTime(0, 0, 0);
                emit dischargeDurationChanged(dischargeDuration);
            } else {
                isBatteryDischarging = false;
                dischargeElapsedTimer.invalidate();
                remainingElapsedTimer.invalidate();
                dischargeDuration = QTime(0, 0, 0);
                remainingBatteryTime = QTime(0, 0, 0);
                emit dischargeDurationChanged(dischargeDuration);
                emit remainingBatteryTimeChanged(remainingBatteryTime);
            }
        }

        // Уровень заряда батареи
        int newBatteryLevel = (status.BatteryLifePercent <= 100) ? status.BatteryLifePercent : batteryLevel;
        if (newBatteryLevel != batteryLevel) {
            lastBatteryLevel = batteryLevel;
            batteryLevel = newBatteryLevel;
            emit batteryLevelChanged(batteryLevel);
        }

        // Время работы от батареи
        if (isBatteryDischarging && dischargeElapsedTimer.isValid()) {
            qint64 elapsedMs = dischargeElapsedTimer.elapsed();
            int elapsedSeconds = elapsedMs / 1000;
            QTime newDischargeDuration(elapsedSeconds / 3600, (elapsedSeconds % 3600) / 60, elapsedSeconds % 60);
            if (newDischargeDuration != dischargeDuration) {
                dischargeDuration = newDischargeDuration;
                emit dischargeDurationChanged(dischargeDuration);
            }
        }

        // Оставшееся время работы
        if (isBatteryDischarging && remainingElapsedTimer.isValid()) {
            int systemSeconds = status.BatteryLifeTime;
            if (systemSeconds != -1 && systemSeconds != 0xFFFFFFFF && systemSeconds != lastRemainingSeconds) {
                // Обновляем, если системное время изменилось
                lastRemainingSeconds = systemSeconds;
                remainingElapsedTimer.restart();
                QTime newRemainingTime(systemSeconds / 3600, (systemSeconds % 3600) / 60, systemSeconds % 60);
                if (newRemainingTime != remainingBatteryTime) {
                    remainingBatteryTime = newRemainingTime;
                    emit remainingBatteryTimeChanged(remainingBatteryTime);
                }
            } else {
                // Интерполяция от последнего известного времени
                qint64 elapsedMs = remainingElapsedTimer.elapsed();
                int elapsedSeconds = elapsedMs / 1000;
                int newRemainingSeconds = lastRemainingSeconds - elapsedSeconds;
                if (newRemainingSeconds < 0) newRemainingSeconds = 0;
                QTime newRemainingTime(newRemainingSeconds / 3600, (newRemainingSeconds % 3600) / 60, newRemainingSeconds % 60);
                if (newRemainingTime != remainingBatteryTime) {
                    remainingBatteryTime = newRemainingTime;
                    emit remainingBatteryTimeChanged(remainingBatteryTime);
                }
            }
        }

        // Тип батареи (чтение из реестра)
        QString newBatteryType = "Неизвестно";
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            WCHAR batteryType[256];
            DWORD bufferSize = sizeof(batteryType);
            if (RegQueryValueExW(hKey, L"DeviceType", NULL, NULL, (LPBYTE)batteryType, &bufferSize) == ERROR_SUCCESS) {
                newBatteryType = QString::fromWCharArray(batteryType);
                if (newBatteryType.isEmpty() || newBatteryType == "Неизвестно") {
                    HKEY hBatteryKey;
                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power", 0, KEY_READ, &hBatteryKey) == ERROR_SUCCESS) {
                        bufferSize = sizeof(batteryType);
                        if (RegQueryValueExW(hBatteryKey, L"Chemistry", NULL, NULL, (LPBYTE)batteryType, &bufferSize) == ERROR_SUCCESS) {
                            newBatteryType = QString::fromWCharArray(batteryType);
                        }
                        RegCloseKey(hBatteryKey);
                    }
                }
            }
            RegCloseKey(hKey);
        }
        // Нормализация типа батареи
        if (newBatteryType.contains("LION", Qt::CaseInsensitive)) {
            newBatteryType = "Li-ion";
        } else if (newBatteryType.contains("NIMH", Qt::CaseInsensitive)) {
            newBatteryType = "NiMH";
        } else if (newBatteryType.contains("NICD", Qt::CaseInsensitive)) {
            newBatteryType = "NiCd";
        }

        if (newBatteryType != "Li-ion" && newBatteryType != "NiMH" && newBatteryType != "NiCd") {
            newBatteryType = (powerSource == "Батарея") ? "Li-ion" : "Неизвестно";
        }
        if (newBatteryType != batteryType) {
            batteryType = newBatteryType;
            emit batteryTypeChanged(batteryType);
        }

        // Статус режима энергосбережения - чтение personality GUID из реестра как REG_SZ
        QString newPowerMode = "Неизвестно";
        bool newPowerSavingEnabled = false;
        GUID *activeSchemeGuid = nullptr;
        if (PowerGetActiveScheme(NULL, &activeSchemeGuid) == ERROR_SUCCESS) {
            // Логируем текущий GUID схемы
            WCHAR guidStr[40];
            StringFromGUID2(*activeSchemeGuid, guidStr, 40);
            const DWORD BUFFER_SIZE = 256;
            PWSTR schemeBuffer = (PWSTR)LocalAlloc(LPTR, BUFFER_SIZE * sizeof(WCHAR));

            // Пытаемся получить имя схемы
            if (schemeBuffer) {
                DWORD bufferSize = BUFFER_SIZE * sizeof(WCHAR);
                DWORD result = PowerReadFriendlyName(NULL, activeSchemeGuid, NULL, NULL, (PUCHAR)schemeBuffer, &bufferSize);
                QString schemeName;
                if (result == ERROR_SUCCESS) {
                    schemeName = QString::fromWCharArray(schemeBuffer);
                }
                LocalFree(schemeBuffer);

                // Маппинг имени схемы на режим (используем contains для гибкости)
                if (schemeName.contains("Silent", Qt::CaseInsensitive)) {
                    newPowerMode = "Тихий";
                    newPowerSavingEnabled = true;
                } else if (schemeName.contains("Сбалансированная", Qt::CaseInsensitive)) {
                    newPowerMode = "Сбалансированный";
                    newPowerSavingEnabled = true;
                } else if (schemeName.contains("Производительный", Qt::CaseInsensitive) || schemeName.contains("Performance", Qt::CaseInsensitive)) {
                    newPowerMode = "Производительный";
                    newPowerSavingEnabled = true;
                } else {
                    newPowerMode = "Неизвестно";
                    newPowerSavingEnabled = false;
                }
            }

            LocalFree(activeSchemeGuid);
        } else {
            qDebug() << "Не удалось получить активную схему питания. Код ошибки:" << GetLastError();
            newPowerMode = "Неизвестно";
            newPowerSavingEnabled = false;
        }

        // Обновляем и отправляем сигналы
        if (newPowerMode != currentPowerMode) {
            currentPowerMode = newPowerMode;
            emit powerModeChanged(currentPowerMode);
        }
        if (newPowerSavingEnabled != powerSavingEnabled) {
            powerSavingEnabled = newPowerSavingEnabled;
            emit powerSavingEnabledChanged(powerSavingEnabled);
        }

    } else {
        qDebug() << "Не удалось получить статус питания. Код ошибки:" << GetLastError();
        powerSource = "Неизвестно";
        batteryType = "Неизвестно";
        batteryLevel = 0;
        powerSavingEnabled = false;
        currentPowerMode = "Неизвестно";
        dischargeDuration = QTime(0, 0, 0);
        remainingBatteryTime = QTime(0, 0, 0);
        emit powerSourceChanged(powerSource);
        emit batteryTypeChanged(batteryType);
        emit batteryLevelChanged(batteryLevel);
        emit powerSavingEnabledChanged(powerSavingEnabled);
        emit powerModeChanged(currentPowerMode);
        emit dischargeDurationChanged(dischargeDuration);
        emit remainingBatteryTimeChanged(remainingBatteryTime);
    }
#else
    powerSource = "Неизвестно";
    batteryType = "Li-ion";
    batteryLevel = 0;
    powerSavingEnabled = false;
    currentPowerMode = "Неизвестно";
    dischargeDuration = QTime(0, 0, 0);
    remainingBatteryTime = QTime(0, 0, 0);
    emit powerSourceChanged(powerSource);
    emit batteryTypeChanged(batteryType);
    emit batteryLevelChanged(batteryLevel);
    emit powerSavingEnabledChanged(powerSavingEnabled);
    emit powerModeChanged(currentPowerMode);
    emit dischargeDurationChanged(dischargeDuration);
    emit remainingBatteryTimeChanged(remainingBatteryTime);
#endif
}

QString PowerMonitor::getPowerSourceType() const {
    return powerSource;
}

QString PowerMonitor::getBatteryType() const {
    return batteryType;
}

int PowerMonitor::getBatteryLevel() const {
    return batteryLevel;
}

bool PowerMonitor::isPowerSavingEnabled() const {
    return powerSavingEnabled;
}

QString PowerMonitor::getPowerMode() const {
    return currentPowerMode;
}

QTime PowerMonitor::getDischargeDuration() const {
    return dischargeDuration;
}

QTime PowerMonitor::getRemainingBatteryTime() const {
    return remainingBatteryTime;
}
