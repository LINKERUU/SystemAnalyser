#include "usbmonitor.h"
// Windows API
#include <windows.h>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
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
                    QString safeDescription = oldDev.description;
                    QString safeDrive = oldDev.driveLetter;
                    QString safePath = oldDev.path;

                    // ⚙️ защита от битых данных — иногда QString внутри структуры уже невалиден
                    auto sanitize = [](const QString &s) -> QString {
                        if (s.isEmpty()) return "";
                        QString trimmed = s.trimmed();
                        // если содержит невалидные символы (в том числе мусор)
                        if (trimmed.contains(QRegularExpression("[\\x00-\\x1F\\x7F]"))) return "(битое имя)";
                        return trimmed;
                    };

                    safeDescription = sanitize(safeDescription);
                    safeDrive = sanitize(safeDrive);
                    safePath = sanitize(safePath);

                    QString name = safeDescription;
                    if (!safeDrive.isEmpty())
                        name += " (" + safeDrive + ")";

                    // 💥 фильтруем битые имена — двойная защита
                    if (!isValidDeviceName(name)) {
                        qDebug() << "⚙️ Пропущено битое уведомление:" << name;
                        continue;
                    }

                    if (safelyEjectedDevices.contains(oldDev.path) ||
                        safelyEjectedDevices.contains(oldDev.description))
                    {
                        qDebug() << "✅ Устройство было безопасно извлечено:" << name;
                        safelyEjectedDevices.remove(oldDev.path);
                        safelyEjectedDevices.remove(oldDev.description);
                        continue;
                    }

                    // 🧠 Показываем сообщение безопасно через очередь GUI (на случай фоновых сигналов)
                    QMetaObject::invokeMethod(qApp, [name]() {
                        QMessageBox::warning(nullptr,
                                             "Небезопасное извлечение!",
                                             "Устройство " + name + " было извлечено небезопасным способом.");
                    }, Qt::QueuedConnection);

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
    // Открываем том как \\.\F:
    QString path = QStringLiteral("\\\\.\\%1").arg(driveLetter.left(2)); // "\\.\F:"
    HANDLE hVolume = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hVolume == INVALID_HANDLE_VALUE) {
        qWarning() << "❌ Не удалось открыть том для блокировки:" << path << " Error:" << GetLastError();
        return false;
    }

    // Сбрасываем буферы — важно
    if (!FlushFileBuffers(hVolume)) {
        qWarning() << "⚠️ FlushFileBuffers failed:" << GetLastError();
        // не прерываем, пробуем дальше
    }

    DWORD bytesReturned = 0;

    // Попытка заблокировать том (FSCTL_LOCK_VOLUME)
    if (!DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr)) {
        qWarning() << "⚠️ FSCTL_LOCK_VOLUME failed:" << GetLastError();
        // Попытка ещё раз после небольшой паузы — иногда помогает
        Sleep(50);
        if (!DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr)) {
            CloseHandle(hVolume);
            return false;
        }
    }

    // Dismount
    if (!DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr)) {
        qWarning() << "⚠️ FSCTL_DISMOUNT_VOLUME failed:" << GetLastError();
        // разблокируем и выходим
        DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
        CloseHandle(hVolume);
        return false;
    }

    // Eject media (если поддерживается)
    DeviceIoControl(hVolume, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);

    // Закрываем дескриптор — система должна отпустить объем
    DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
    CloseHandle(hVolume);

    qDebug() << "✅ Том заблокирован, размонтирован и готов к извлечению:" << path;
    return true;
}

// Унифицированная и асинхронная ejectSafe — для HID и для USB-накопителей
void UsbMonitor::ejectSafe(const UsbDevice& dev)
{
    QMetaObject::invokeMethod(qApp, [dev]() {
        UsbMonitor::getInstance()->safelyEjectedDevices.insert(dev.path);
        UsbMonitor::getInstance()->safelyEjectedDevices.insert(dev.description);
    });

    QString name = dev.description;
    if (!dev.driveLetter.isEmpty()) name += " (" + dev.driveLetter + ")";
    qDebug() << "[Async] Попытка безопасного извлечения устройства" << name;

    // Запускаем всю работу в фоновой задаче — UI не будет виснуть
    QtConcurrent::run([dev]() {


        bool success = false;
        DEVINST currentDevInst = dev.devInst;
        for (int depth = 0; depth < 8 && !success; ++depth) {
            // 2️⃣ Поднимаемся по дереву до USBSTOR\ или USB\VID_... (это правильный devInst для eject)
            DEVINST ejectInst = dev.devInst;
            for (int i = 0; i < 10; ++i) {
                WCHAR deviceId[MAX_DEVICE_ID_LEN] = {0};
                if (CM_Get_Device_IDW(ejectInst, deviceId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
                    break;
                QString id = QString::fromWCharArray(deviceId).toUpper();
                if (id.contains("USBSTOR") || id.contains("USB\\VID_")) {
                    qDebug() << "[Async] Найден подходящий узел для извлечения:" << id;
                    break;
                }
                DEVINST parent;
                if (CM_Get_Parent(&parent, ejectInst, 0) != CR_SUCCESS) break;
                ejectInst = parent;
            }

            if (!dev.driveLetter.isEmpty()) {
                if (!lockAndDismountVolume(dev.driveLetter)) {
                    // ошибка
                    return;
                }
                // ✔️ Отмечаем как безопасно извлечённое здесь
                QMetaObject::invokeMethod(qApp, [dev]() {
                    UsbMonitor::getInstance()->safelyEjectedDevices.insert(dev.path);
                });
            }


            // 3️⃣ Запрос на безопасное извлечение именно USBSTOR\... или USB\VID\...
            PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
            WCHAR vetoName[MAX_PATH] = {0};
            ULONG vetoLen = MAX_PATH;
            CONFIGRET cres = CM_Request_Device_EjectW(ejectInst, &vetoType, vetoName, vetoLen, 0);

            if (cres == CR_SUCCESS) {
                qDebug() << "[Async] CM_Request_Device_EjectW success for devInst" << ejectInst;

                // помечаем до того, как Windows пошлёт WM_DEVICECHANGE
                QMetaObject::invokeMethod(qApp, [=]() {
                    UsbMonitor::getInstance()->safelyEjectedDevices.insert(dev.path);
                    QMessageBox::information(nullptr, "Успех",
                                             "Устройство " + dev.description + " успешно извлечено.");
                });
                return;
            }

            qDebug() << "[Async] CM_Request_Device_EjectW failed:" << cres
                     << "veto:" << QString::fromWCharArray(vetoName);

            // Альтернатива: CM_Query_And_Remove_SubTreeW на этом же ejectInst
            CONFIGRET cres2 = CM_Query_And_Remove_SubTreeW(ejectInst, &vetoType, vetoName, vetoLen, CM_REMOVE_NO_RESTART);
            if (cres2 == CR_SUCCESS) {
                qDebug() << "[Async] CM_Query_And_Remove_SubTreeW success for" << ejectInst;
                QMetaObject::invokeMethod(qApp, [=]() {
                    UsbMonitor::getInstance()->safelyEjectedDevices.insert(dev.path);
                    QMessageBox::information(nullptr, "Успех",
                                             "Устройство " + dev.description + " успешно извлечено.");
                });
            }

        }

        if (!success) {
            // Попробуем альтернативно CM_Query_And_Remove_SubTreeW (иногда срабатывает)
            currentDevInst = dev.devInst;
            for (int depth = 0; depth < 4 && !success; ++depth) {
                PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
                WCHAR vetoName[MAX_PATH] = {0};
                ULONG vetoLen = MAX_PATH;
                CONFIGRET cres2 = CM_Query_And_Remove_SubTreeW(currentDevInst, &vetoType, vetoName, vetoLen, CM_REMOVE_NO_RESTART);
                if (cres2 == CR_SUCCESS) {
                    success = true;
                    qDebug() << "[Async] CM_Query_And_Remove_SubTreeW success";
                    QMetaObject::invokeMethod(qApp, [dev]() {
                        QMessageBox::information(nullptr, "Успех", "Устройство " + dev.description + " успешно извлечено (QueryAndRemoveSubTree).");
                    });
                    break;
                } else {
                    qDebug() << "[Async] CM_Query_And_Remove_SubTreeW failed code:" << cres2;
                }
                DEVINST parent;
                if (CM_Get_Parent(&parent, currentDevInst, 0) != CR_SUCCESS) break;
                currentDevInst = parent;
            }
        }

        if (!dev.driveLetter.isEmpty()) {
            // Попытка размонтирования/подготовки тома
            if (!lockAndDismountVolume(dev.driveLetter)) {
                qWarning() << "[Async] Не удалось подготовить том" << dev.driveLetter;
                QMetaObject::invokeMethod(qApp, [dev]() {
                    QMessageBox::warning(nullptr, "Ошибка",
                                         "Не удалось подготовить том " + dev.description + " к извлечению. Закройте все файлы и попробуйте снова.");
                });
                return;
            }

            // Небольшая пауза чтобы система успела отпустить хендлы
            Sleep(50);
        }

        if (success) {

            // Отмечаем как успешно извлечённое (если у тебя есть коллекция safelyEjectedDevices)
            // Делать изменение коллекции в GUI-потоке:
            QMetaObject::invokeMethod(qApp, [dev]() {
                // пример: UsbMonitor::getInstance()->markSafelyEjected(dev.path);
                qDebug() << "[Async->GUI] отметка как безопасно извлеченное:" << dev.path;
            });
        }
    }); // QtConcurrent::run
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
