#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include <QDebug>
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <QElapsedTimer>
#include <hidsdi.h>
#include <dbt.h>
#include <QMutex>
#include <cfgmgr32.h> // Для DEVINST
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

struct UsbDevice {
    QString description;
    QString type;
    QString driveLetter;
    QString path;
    QString manufacturer;
    QString product;
    QString vid;
    QString pid;
    QString serial;
    bool isRemovable = false;
    DEVINST devInst = 0;
    UsbDevice() = default;
};
class UsbMonitor : public QObject {
    Q_OBJECT
public:
    static UsbMonitor* getInstance();
    QList<UsbDevice> getUsbDevices();
    bool registerNotifications(HWND hWnd);
    void toggleGlobalEjectBlock(bool enable);
    bool handleDeviceChange(UINT message, WPARAM wParam);
public slots:
    void ejectSafe(const UsbDevice& dev);
    void denyEject(const UsbDevice& dev);
signals:
    void devicesChanged();
private:
    QSet<QString> safelyEjectedDevices;
    QList<UsbDevice> m_devices;
    UsbMonitor(QObject* parent = nullptr);
    HDEVNOTIFY hDevNotify = nullptr;
    QList<UsbDevice> findUsbDevices();
    QString getDeviceDescriptionFromSetupAPI(HDEVINFO hDevInfo, SP_DEVINFO_DATA deviceInfoData);
};
