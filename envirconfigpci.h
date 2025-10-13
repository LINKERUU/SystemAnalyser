#ifndef ENVIRCONFIGPCI_H
#define ENVIRCONFIGPCI_H

#include <QString>
#include <QList>

struct PCIDevice {
    QString vendorID;
    QString deviceID;
    QString instanceID;
    QString friendlyName;
};

class envirconfigPCI {
public:
    envirconfigPCI();
    QList<PCIDevice> getPCIDevices();
};

#endif // ENVIRCONFIGPCI_H
