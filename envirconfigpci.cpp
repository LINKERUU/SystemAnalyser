#include "envirconfigpci.h"
#include <vector>
#include <string>
#include <regex>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#endif

static std::vector<std::string> splitMultiSz(const std::vector<char>& buf) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < buf.size() && buf[i] != '\0') {
        std::string s(&buf[i]);
        out.push_back(s);
        i += s.size() + 1;
    }
    return out;
}

envirconfigPCI::envirconfigPCI() {}

QList<PCIDevice> envirconfigPCI::getPCIDevices() {
    QList<PCIDevice> list;

#ifdef Q_OS_WIN
    HDEVINFO devInfo = SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) return list;

    SP_DEVINFO_DATA devData;
    devData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD index = 0; SetupDiEnumDeviceInfo(devInfo, index, &devData); ++index) {
        DWORD required = 0;
        std::vector<char> buffer(4096);
        if (!SetupDiGetDeviceRegistryPropertyA(devInfo, &devData, SPDRP_HARDWAREID, nullptr,
                                               (PBYTE)buffer.data(), (DWORD)buffer.size(), &required)) {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                buffer.resize(required);
                if (!SetupDiGetDeviceRegistryPropertyA(devInfo, &devData, SPDRP_HARDWAREID, nullptr,
                                                       (PBYTE)buffer.data(), (DWORD)buffer.size(), &required)) continue;
            } else continue;
        }

        auto hwids = splitMultiSz(buffer);
        QString vendor, device;
        bool foundPCI = false;
        std::regex pci_re(R"(PCI\\VEN_([0-9A-Fa-f]{4})&DEV_([0-9A-Fa-f]{4}))");

        for (const auto& s : hwids) {
            std::smatch m;
            if (std::regex_search(s, m, pci_re) && m.size() >= 3) {
                vendor = QString::fromStdString(m[1]).toUpper();
                device = QString::fromStdString(m[2]).toUpper();
                foundPCI = true;
                break;
            }
        }
        if (!foundPCI) continue;

        char instanceIdBuf[512] = {};
        SetupDiGetDeviceInstanceIdA(devInfo, &devData, instanceIdBuf, sizeof(instanceIdBuf), nullptr);

        char friendly[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyA(devInfo, &devData, SPDRP_FRIENDLYNAME, nullptr,
                                               (PBYTE)friendly, sizeof(friendly), nullptr)) {
            SetupDiGetDeviceRegistryPropertyA(devInfo, &devData, SPDRP_DEVICEDESC, nullptr,
                                              (PBYTE)friendly, sizeof(friendly), nullptr);
        }

        list.append({vendor, device,
                     QString::fromLocal8Bit(instanceIdBuf),
                     QString::fromLocal8Bit(friendly)});
    }
    SetupDiDestroyDeviceInfoList(devInfo);
#endif
    return list;
}
