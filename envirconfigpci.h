#ifndef ENVIRCONFIGPCI_H
#define ENVIRCONFIGPCI_H

#include <QList>
#include <QString>
#include <QMap>

struct PCIDevice {
    QString vendorID;  // 4 символа, например, "8086"
    QString deviceID;  // 4 символа, например, "3E92"
    QString title;     // Название устройства, например, "Intel HD Graphics"
    QString busInfo;   // Информация о шине, например, "00:02.0"
};

class envirconfigPCI
{
public:
    envirconfigPCI();
    ~envirconfigPCI();
    QList<PCIDevice> getPCIDevices();  // Метод для получения списка устройств
    QString getDeviceTitle(const QString &vendorId, const QString &deviceId);  // Метод для получения названия устройства

private:
    bool readPCIConfig(uint32_t bus, uint32_t device, uint32_t function, uint32_t reg, uint32_t &value);
    QMap<QString, QString> vendorNames;  // Карта VendorID -> Название вендора
    QMap<QString, QMap<QString, QString>> deviceNames;  // Карта VendorID -> (DeviceID -> Название устройства)
};

#endif // ENVIRCONFIGPCI_H
