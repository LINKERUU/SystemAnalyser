#include "usbmonitor.h"
// Windows API
#include <windows.h>
#include <QMessageBox>
#include <setupapi.h>
#include <cfgmgr32.h> // DEVINST, CM_Locate_DevNode, CM_Get_DevNode_Property
#include <devpkey.h> // DEVPKEY_Device_BusTypeGuid
#include <usbiodef.h> // содержит GUID_DEVINTERFACE_USB_DEVICE
#include <initguid.h>
#include <devguid.h>
#include <Dbt.h>
#include <winioctl.h>
#include <QRegularExpression>
// Определение GUID для HID-устройств (должно быть только в одном .cpp)
DEFINE_GUID(GUID_DEVINTERFACE_HID, 0x4d1e55b2, 0xf16f, 0x11cf, 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30);
// --- Singleton Implementation ---
UsbMonitor::UsbMonitor(QObject *parent) : QObject(parent)
{
    // Инициализация
}
UsbMonitor* UsbMonitor::getInstance()
{
    static UsbMonitor instance;
    return &instance;
}
bool isUsbDevice(const QString& devicePath)
{
    DEVINST devInst = 0;
    if (CM_Locate_DevNodeW(&devInst, (DEVINSTID_W)devicePath.utf16(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return false;
    ULONG busType = 0;
    ULONG size = sizeof(busType);
    if (CM_Get_DevNode_Registry_PropertyW(devInst, CM_DRP_BUSTYPEGUID, nullptr, &busType, &size, 0) != CR_SUCCESS)
        return false;
    // GUID шины USB = {36FC9E60-C465-11CF-8056-444553540000}
    static const GUID GUID_BUS_TYPE_USB = {0x36FC9E60, 0xC465, 0x11CF, {0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    GUID busGuid;
    size = sizeof(busGuid);
    if (CM_Get_DevNode_PropertyW(devInst, &DEVPKEY_Device_BusTypeGuid, nullptr, (PBYTE)&busGuid, &size, 0) != CR_SUCCESS)
        return false;
    return IsEqualGUID(busGuid, GUID_BUS_TYPE_USB);
}
bool isUsbStorage(const QString& driveLetter)
{
    QString device = QString("\\\\.\\%1:").arg(driveLetter.left(1));
    HANDLE hDevice = CreateFileW(device.toStdWString().c_str(), 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDevice == INVALID_HANDLE_VALUE)
        return false;
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    STORAGE_DEVICE_DESCRIPTOR buffer[2];
    DWORD bytesReturned = 0;
    bool result = false;
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                        &buffer, sizeof(buffer), &bytesReturned, nullptr))
    {
        result = (buffer->BusType == BusTypeUsb);
    }
    CloseHandle(hDevice);
    return result;
}
// ---------------------------------
// --- Вспомогательные функции Windows API ---
// Функция для получения описания устройства из SetupAPI
QString UsbMonitor::getDeviceDescriptionFromSetupAPI(HDEVINFO hDevInfo, SP_DEVINFO_DATA deviceInfoData)
{
    DWORD requiredSize = 0;
    DWORD propertyType;
    QString description;
    // 1. Попытка получить FriendlyName (если есть)
    if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, &propertyType, NULL, 0, &requiredSize) ||
        requiredSize > 0)
    {
        WCHAR* buffer = (WCHAR*)LocalAlloc(LMEM_FIXED, requiredSize);
        if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, &propertyType, (PBYTE)buffer, requiredSize, NULL)) {
            description = QString::fromWCharArray(buffer).trimmed();
        }
        LocalFree(buffer);
    }
    // 2. Если FriendlyName пустой, пытаемся получить Device Description
    if (description.isEmpty()) {
        requiredSize = 0;
        if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &deviceInfoData, SPDRP_DEVICEDESC, &propertyType, NULL, 0, &requiredSize) ||
            requiredSize > 0)
        {
            WCHAR* buffer = (WCHAR*)LocalAlloc(LMEM_FIXED, requiredSize);
            if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &deviceInfoData, SPDRP_DEVICEDESC, &propertyType, (PBYTE)buffer, requiredSize, NULL)) {
                description = QString::fromWCharArray(buffer).trimmed();
            }
            LocalFree(buffer);
        }
    }
    return description;
}
QList<UsbDevice> UsbMonitor::findUsbDevices()
{
    QList<UsbDevice> devices;
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE)
        return devices;
    SP_DEVINFO_DATA deviceInfoData{};
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData{};
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    // --- Подсчёт составных устройств ---
    int totalCompositeCount = 0;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &GUID_DEVINTERFACE_USB_DEVICE, i, &deviceInterfaceData); ++i)
    {
        DWORD detailSize = 0;
        SetupDiGetDeviceInterfaceDetailW(hDevInfo, &deviceInterfaceData, nullptr, 0, &detailSize, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            continue;
        auto pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)LocalAlloc(LMEM_FIXED, detailSize);
        if (!pDetail) continue;
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &deviceInterfaceData, pDetail, detailSize, nullptr, &deviceInfoData))
        {
            QString desc = getDeviceDescriptionFromSetupAPI(hDevInfo, deviceInfoData).toLower();
            if (desc.contains("composite device") || desc.contains("составное usb"))
                ++totalCompositeCount;
        }
        LocalFree(pDetail);
    }
    int compositeSeen = 0;
    // --- Основной проход устройств ---
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &GUID_DEVINTERFACE_USB_DEVICE, i, &deviceInterfaceData); ++i)
    {
        DWORD detailSize = 0;
        SetupDiGetDeviceInterfaceDetailW(hDevInfo, &deviceInterfaceData, nullptr, 0, &detailSize, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            continue;
        auto pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)LocalAlloc(LMEM_FIXED, detailSize);
        if (!pDetail) continue;
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &deviceInterfaceData, pDetail, detailSize, nullptr, &deviceInfoData))
        {
            QString devicePath = QString::fromWCharArray(pDetail->DevicePath);
            QString descLower = getDeviceDescriptionFromSetupAPI(hDevInfo, deviceInfoData).toLower();
            // --- Фильтрация встроенных устройств ---
            WCHAR instanceBuffer[MAX_DEVICE_ID_LEN];
            QString instanceId;
            if (CM_Get_Device_IDW(deviceInfoData.DevInst, instanceBuffer, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS)
                instanceId = QString::fromWCharArray(instanceBuffer).toUpper();
            if (instanceId.contains("VID_2B7E&PID_B597") ||
                instanceId.contains("VID_0B05&PID_6206") ||
                instanceId.contains("VID_8087&PID_0026"))
            {
                LocalFree(pDetail);
                continue;
            }
            // --- Создаём объект ---
            UsbDevice dev;
            dev.path = devicePath;
            dev.description = getDeviceDescriptionFromSetupAPI(hDevInfo, deviceInfoData);
            // Определяем тип
            if (descLower.contains("mouse") || descLower.contains("keyboard") ||
                descLower.contains("input") || descLower.contains("hid"))
            {
                dev.type = "HID-устройство";
            }
            else
            {
                dev.type = "USB-устройство";
            }
            dev.devInst = deviceInfoData.DevInst;
            // Определяем removability
            DWORD removalPolicy;
            DWORD propertyType;
            if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &deviceInfoData, SPDRP_REMOVAL_POLICY, &propertyType, (PBYTE)&removalPolicy, sizeof(DWORD), NULL))
            {
                if (removalPolicy == CM_REMOVAL_POLICY_EXPECT_ORDERLY_REMOVAL || removalPolicy == CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL)
                {
                    dev.isRemovable = true;
                }
            }
            devices.append(dev);
        }
        LocalFree(pDetail);
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    // --- После добавления всех устройств, ищем USB-накопители и обновляем их ---
    DWORD drives = GetLogicalDrives();
    for (char drive = 'A'; drive <= 'Z'; ++drive)
    {
        if (!(drives & (1 << (drive - 'A'))))
            continue;
        QString letter = QString(QChar(drive)) + ":\\";
        if (!isUsbStorage(letter))
            continue;
        // Находим соответствующее устройство по USB
        for (auto &dev : devices)
        {
            if (dev.driveLetter.isEmpty())
            {
                dev.driveLetter = letter;
                dev.type = "USB-накопитель";
                WCHAR volumeName[MAX_PATH + 1] = {0};
                GetVolumeInformationW(letter.toStdWString().c_str(), volumeName,
                                      MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0);
                dev.description = (wcslen(volumeName) > 0)
                                      ? QString::fromWCharArray(volumeName)
                                      : "USB-накопитель";
                break; // присваиваем только одному устройству
            }
        }
    }
    return devices;
}
// --- Основной сборщик ---
QList<UsbDevice> UsbMonitor::getUsbDevices()
{
    QList<UsbDevice> list = findUsbDevices();
    return list;
}
// --- Регистрация уведомлений ---
bool UsbMonitor::registerNotifications(HWND hWnd)
{
    DEV_BROADCAST_DEVICEINTERFACE devBroadcastInterface = {0};
    devBroadcastInterface.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    devBroadcastInterface.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    devBroadcastInterface.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;
    hDevNotify = RegisterDeviceNotificationW(hWnd, &devBroadcastInterface, DEVICE_NOTIFY_WINDOW_HANDLE);
    return hDevNotify != nullptr;
}

static bool isValidDeviceName(const QString &name) {
    if (name.isEmpty())
        return false;

    QString trimmed = name.trimmed();

    // если содержит управляющие символы (нулевые байты и т.п.)
    if (trimmed.contains(QRegularExpression("[\\x00-\\x1F]")))
        return false;

    // если вообще нет букв/цифр (например, только мусор)
    if (!trimmed.contains(QRegularExpression("[A-Za-zА-Яа-я0-9]")))
        return false;

    return true;
}

// --- Обработчик изменений ---
bool UsbMonitor::handleDeviceChange(UINT message, WPARAM wParam)
{
    if (message != WM_DEVICECHANGE)
        return false;

    switch (wParam)
    {
    case DBT_DEVICEARRIVAL:
        qDebug() << "🔌 Подключено новое устройство";
        m_devices = findUsbDevices();
        emit devicesChanged();
        break;

    case DBT_DEVICEREMOVECOMPLETE:
        qDebug() << "❌ Устройство удалено";
        {
            // Получаем текущий список устройств
            QList<UsbDevice> current = findUsbDevices();

            // Находим, кого удалили
            for (const auto &oldDev : m_devices)
            {
                bool stillConnected = std::any_of(current.begin(), current.end(), [&](const UsbDevice &d) {
                    return d.path == oldDev.path;
                });

                if (!stillConnected)
                {
                    QString name = oldDev.description + (oldDev.driveLetter.isEmpty() ? "" : " (" + oldDev.driveLetter + ")");

                    // 💥 фильтруем битые имена
                    if (!isValidDeviceName(name)) {
                        qDebug() << "⚙️ Пропущено битое уведомление:" << name;
                        continue;
                    }

                    if (safelyEjectedDevices.contains(oldDev.path)) {
                        safelyEjectedDevices.remove(oldDev.path);
                        qDebug() << "✅ Устройство было безопасно извлечено ранее:" << name;
                    } else {
                        qWarning() << "⚠️ Устройство извлечено небезопасно:" << name;
                        QMessageBox::warning(nullptr,
                                             "Небезопасное извлечение!",
                                             "Устройство " + name + " было извлечено небезопасным способом.");
                    }
                }

            }

            // Обновляем текущий список устройств
            m_devices = current;
            emit devicesChanged();
        }
        break;

    default:
        break;
    }

    return true;
}

// Реальная функция безопасного извлечения
bool lockAndDismountVolume(const QString& driveLetter)
{
    QString path = QStringLiteral("\\\\.\\%1").arg(driveLetter.left(2)); // "\\.\F:"
    HANDLE hVolume = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, 0, nullptr);

    if (hVolume == INVALID_HANDLE_VALUE) {
        qWarning() << "❌ Не удалось открыть том для блокировки:" << path;
        return false;
    }

    DWORD bytesReturned;
    BOOL result = DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
    if (!result) {
        qWarning() << "⚠️ Не удалось заблокировать том:" << path;
        CloseHandle(hVolume);
        return false;
    }

    result = DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
    if (!result) {
        qWarning() << "⚠️ Не удалось размонтировать том:" << path;
        CloseHandle(hVolume);
        return false;
    }

    qDebug() << "✅ Том заблокирован и размонтирован:" << path;
    CloseHandle(hVolume);
    return true;
}

void UsbMonitor::ejectSafe(const UsbDevice& dev)
{
    QString name = dev.description;
    if (!dev.driveLetter.isEmpty()) name += " (" + dev.driveLetter + ")";
    qDebug() << "🔒 Попытка безопасного извлечения устройства" << name;

    // 1️⃣ Пытаемся заблокировать и размонтировать
    if (!dev.driveLetter.isEmpty()) {
        if (!lockAndDismountVolume(dev.driveLetter)) {
            qWarning() << "⚠️ Не удалось подготовить том к извлечению:" << dev.driveLetter;
        }
    }

    // 2️⃣ Пробуем извлечь (с подъёмом по дереву)
    bool success = false;
    DEVINST currentDevInst = dev.devInst;

    for (int i = 0; i < 5 && !success; ++i) {
        WCHAR deviceId[MAX_DEVICE_ID_LEN];
        if (CM_Get_Device_IDW(currentDevInst, deviceId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
            break;

        PNP_VETO_TYPE vetoType;
        WCHAR vetoName[MAX_PATH] = {0};
        ULONG vetoNameLen = MAX_PATH;

        CONFIGRET res = CM_Request_Device_EjectW(currentDevInst, &vetoType, vetoName, vetoNameLen, 0);
        if (res == CR_SUCCESS) {
            success = true;
            safelyEjectedDevices.insert(dev.path);
            qDebug() << "✅ Устройство безопасно извлечено:" << name;
            QMessageBox::information(nullptr, "Успех",
                                     "Устройство " + name + " успешно извлечено безопасным способом.");
            break;
        } else {
            QString vetoReason = QString::fromWCharArray(vetoName);
            qDebug() << "⚠️ Ошибка извлечения, код:" << res << ", veto:" << vetoReason;
        }

        DEVINST parent;
        if (CM_Get_Parent(&parent, currentDevInst, 0) != CR_SUCCESS)
            break;
        currentDevInst = parent;
    }

    if (!success) {
        qWarning() << "❌ Не удалось безопасно извлечь устройство:" << name;
        QMessageBox::warning(nullptr, "Ошибка",
                             "Не удалось извлечь устройство " + name +
                                 ". Возможно, оно используется системой или другими программами.");
    }
}




void UsbMonitor::denyEject(const UsbDevice& dev)
{
    qDebug()<<"DDDDDDDD";
    QString name = dev.description;
    if (!dev.driveLetter.isEmpty()) name += " (" + dev.driveLetter + ")";
    qDebug() << "🚫 Отказ в безопасном извлечении" << name;
    // Симулируем отказ, показывая сообщение
    QMessageBox::critical(nullptr, "Отказ в извлечении",
                          "Извлечение устройства \"" + name + "\" невозможно.\n\n"
                                                              "Возможные причины:\n"
                                                              "• Устройство используется другими приложениями\n"
                                                              "• Файлы открыты на устройстве\n"
                                                              "• Системные ограничения\n\n"
                                                              "Закройте все программы и попробуйте снова.");
}
