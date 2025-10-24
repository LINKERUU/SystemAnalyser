#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPixmap>
#include <QClipboard>
#include <QShortcut>
#include <QGuiApplication>
#include <QPainter>
#include <QSvgRenderer>
#include <QLabel>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QTableWidget>
#include <QHeaderView>
#include <QFontMetrics>
QString getProjectAssetsPath() {
    QString sourceFilePath = __FILE__;
    QDir sourceDir(sourceFilePath);
    return sourceDir.absoluteFilePath("../assets/") + "/";
}
static const QString ASSETS_PATH = getProjectAssetsPath();
BatteryWidget::BatteryWidget(QWidget *parent) : QLabel(parent), batteryLevel(0) {
    setFixedSize(350, 150);
    renderer = new QSvgRenderer(this);
}
void BatteryWidget::setBatteryLevel(int level) {
    batteryLevel = qBound(0, level, 100);
    update();
}
void BatteryWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    int imageIndex = (100 - batteryLevel) / 10 + 1;
    if (imageIndex < 1) imageIndex = 1;
    if (imageIndex > 10) imageIndex = 10;
    QString svgPath = ASSETS_PATH + "battery" + QString::number(imageIndex) + ".svg";
    if (!QFile::exists(svgPath)) {
        qDebug() << "Battery SVG file not found:" << svgPath;
        return;
    }
    if (!renderer->load(svgPath)) {
        qDebug() << "Failed to load battery SVG:" << svgPath;
        return;
    }
    QRect targetRect(0, 0, width(), height());
    renderer->render(&painter, targetRect);
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 20, QFont::Bold));
    QString levelText = QString::number(batteryLevel) + "%";
    QRect textRect = rect().adjusted(10, 10, -10, -10);
    painter.drawText(textRect, Qt::AlignCenter, levelText);
}
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), currentFrame(0), isEatAnimationInfinite(false),
    lab1Activated(false), currentAnimationType(None), isHiddenMode(false), isCameraOn(false), wasCameraOn(false) {
    setFixedSize(1245, 720);
    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    isCameraOn = false;
    animationLabel = new QLabel(this);
    animationLabel->setFixedSize(1245, 720);
    animationLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(animationLabel);
    setCentralWidget(central);
    QString carrotPath = ASSETS_PATH + "carrot.svg";
    QSvgRenderer renderer(carrotPath);
    QPixmap carrotPixmap(64, 64);
    carrotPixmap.fill(Qt::transparent);
    QPainter painter(&carrotPixmap);
    renderer.render(&painter);
    painter.end();
    QCursor carrotCursor(carrotPixmap, 0, 0); // (0,0) — горячая точка
    setCursor(carrotCursor);
    QStringList labNames = { "Лабораторная 1", "Лабораторная 2", "Лабораторная 3", "Лабораторная 4", "Лабораторная 5", "Лабораторная 6" };
    int startX = 40;
    int startY = 30;
    int spacing = 40;
    QPushButton *lab1Button = nullptr;
    QPushButton *lab2Button = nullptr;
    QPushButton *lab4Button = nullptr;
    for (int i = 0; i < labNames.size(); ++i) {
        QPushButton *btn = new QPushButton(labNames[i], animationLabel);
        btn->setFixedSize(160, 40);
        btn->move(startX + i * (160 + spacing), startY);
        btn->setStyleSheet(R"(
            QPushButton {
                background-color: rgba(74, 144, 226, 220);
                color: white;
                border-radius: 8px;
                font-size: 15px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: rgba(53, 122, 189, 220);
            }
            QPushButton:pressed {
                background-color: rgba(44, 90, 160, 240);
            }
        )");
        labButtons.append(btn);
        if (i == 0) lab1Button = btn;
        if (i == 1) lab2Button = btn;
        if (i == 3) lab4Button = btn;
    }
    connect(lab1Button, &QPushButton::clicked, this, &MainWindow::showPowerInfo);
    connect(lab2Button, &QPushButton::clicked, this, &MainWindow::showPCIInfo);
    connect(lab4Button, &QPushButton::clicked, this, &MainWindow::showWebcamPanel);
    drawBackground();
    frameTimer = new QTimer(this);
    connect(frameTimer, &QTimer::timeout, this, &MainWindow::updateFrame);
    resetTimer = new QTimer(this);
    resetTimer->setSingleShot(true);
    connect(resetTimer, &QTimer::timeout, this, [this]() { drawBackground(); });
    blinkTimer = new QTimer(this);
    connect(blinkTimer, &QTimer::timeout, this, &MainWindow::triggerBlinkAnimation);
    blinkTimer->start(5000);
    QTimer *welcomeTimer = new QTimer(this);
    welcomeTimer->setSingleShot(true);
    connect(welcomeTimer, &QTimer::timeout, this, &MainWindow::startWelcomeAnimation);
    welcomeTimer->start(2000);
    powerMonitor = new PowerMonitor(this);
    pciMonitor = new envirconfigPCI();
    webcam = new webcamera(this);
    setupPowerInfoPanel();
    setupPCIInfoPanel();
    setupWebcamPanel();
    connect(pciTable->horizontalHeader(), &QHeaderView::sectionResized, this, [=](int logicalIndex, int newSize) {
        if (logicalIndex == 3) {
            QFontMetrics metrics(pciTable->font());
            for (int row = 0; row < pciTable->rowCount(); ++row) {
                QTableWidgetItem* item = pciTable->item(row, 3);
                if (!item) continue;
                QString fullText = item->data(Qt::UserRole).toString();
                QString elided = metrics.elidedText(fullText, Qt::ElideRight, newSize - 10);
                item->setText(elided);
            }
        }
    });
    connect(powerMonitor, &PowerMonitor::powerSourceChanged, this, [this](const QString &type) {
        updatePowerInfo(type, powerMonitor->getBatteryType(), powerMonitor->getBatteryLevel(),
                        powerMonitor->isPowerSavingEnabled(), powerMonitor->getDischargeDuration(),
                        powerMonitor->getRemainingBatteryTime());
        if (lab1Activated) {
            frameTimer->stop();
            resetTimer->stop();
            if (type == "Сеть") {
                isEatAnimationInfinite = true;
                startEatAnimation();
            } else if (type == "Батарея") {
                isEatAnimationInfinite = false;
                startSadAnimation();
            }
        }
    });
    connect(powerMonitor, &PowerMonitor::batteryTypeChanged, this, [this](const QString &type) {
        updatePowerInfo(powerMonitor->getPowerSourceType(), type, powerMonitor->getBatteryLevel(),
                        powerMonitor->isPowerSavingEnabled(), powerMonitor->getDischargeDuration(),
                        powerMonitor->getRemainingBatteryTime());
    });
    connect(powerMonitor, &PowerMonitor::batteryLevelChanged, this, [this](int level) {
        updatePowerInfo(powerMonitor->getPowerSourceType(), powerMonitor->getBatteryType(), level,
                        powerMonitor->isPowerSavingEnabled(), powerMonitor->getDischargeDuration(),
                        powerMonitor->getRemainingBatteryTime());
    });
    connect(powerMonitor, &PowerMonitor::powerSavingEnabledChanged, this, [this](bool enabled) {
        updatePowerInfo(powerMonitor->getPowerSourceType(), powerMonitor->getBatteryType(),
                        powerMonitor->getBatteryLevel(), enabled, powerMonitor->getDischargeDuration(),
                        powerMonitor->getRemainingBatteryTime());
    });
    connect(powerMonitor, &PowerMonitor::dischargeDurationChanged, this, [this](const QTime &duration) {
        updatePowerInfo(powerMonitor->getPowerSourceType(), powerMonitor->getBatteryType(),
                        powerMonitor->getBatteryLevel(), powerMonitor->isPowerSavingEnabled(), duration,
                        powerMonitor->getRemainingBatteryTime());
    });
    connect(powerMonitor, &PowerMonitor::remainingBatteryTimeChanged, this, [this](const QTime &time) {
        updatePowerInfo(powerMonitor->getPowerSourceType(), powerMonitor->getBatteryType(),
                        powerMonitor->getBatteryLevel(), powerMonitor->isPowerSavingEnabled(),
                        powerMonitor->getDischargeDuration(), time);
    });
    connect(powerMonitor, &PowerMonitor::powerModeChanged, this, &MainWindow::updatePowerMode);
    powerMonitor->startMonitoring();
    surveillanceTimer = new QTimer(this);
    connect(surveillanceTimer, &QTimer::timeout, this, [this]() {
        QString dirPath = QDir::currentPath() + "/surveillance/";
        QDir().mkpath(dirPath);
        QString filePath = dirPath + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".jpg";
        webcam->capturePhoto(filePath);
    });
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon(ASSETS_PATH + "icon.png"));
    QMenu *trayMenu = new QMenu(this);
    QAction *showAction = trayMenu->addAction("Показать");
    connect(showAction, &QAction::triggered, this, &MainWindow::stopHiddenSurveillance);
    QAction *quitAction = trayMenu->addAction("Выход");
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
    trayIcon->setContextMenu(trayMenu);
    // Add hotkey to stop hidden surveillance
    QShortcut *stopSurveillanceShortcut = new QShortcut(QKeySequence("Ctrl+Shift+S"), this);
    connect(stopSurveillanceShortcut, &QShortcut::activated, this, &MainWindow::stopHiddenSurveillance);
}
MainWindow::~MainWindow() {}
void MainWindow::startAnimation(const QString &prefix, int start, int end, int delay,
                                bool infinite = false, bool reverse = false, AnimationType type = None,int count=1) {
    loadFrames(prefix, start, end, count, reverse,false);
    currentFrame = 0;
    currentAnimationType = type;
    isEatAnimationInfinite = infinite;
    frameTimer->start(delay);
}
void MainWindow::updatePowerInfo(const QString &powerSourceType,
                                 const QString &batteryType,
                                 int batteryLevel,
                                 bool powerSavingEnabled,
                                 const QTime &dischargeDuration,
                                 const QTime &remainingTime)
{
    powerSourceLabel->setText("Тип энергопитания: " + powerSourceType);
    batteryTypeLabel->setText("Тип батареи: " + batteryType);
    powerModeStatusLabel->setText("Режим работы: " + QString(powerSavingEnabled ? "Энергосбережение" : "Обычный"));
    dischargeDurationLabel->setText("Время работы: " + dischargeDuration.toString("hh:mm:ss"));
    remainingBatteryTimeLabel->setText("Оставшееся время: " + remainingTime.toString("hh:mm:ss"));
    batteryWidget->setBatteryLevel(batteryLevel);
}
void MainWindow::setupPowerInfoPanel() {
    powerInfoPanel = new QWidget(animationLabel);
    powerInfoPanel->setFixedSize(500, 640);
    powerInfoPanel->move(700, 30);
    powerInfoPanel->setStyleSheet("background-color: rgba(255, 255, 255, 220); border-radius: 12px; border: 1px solid rgba(74, 144, 226, 100);");
    QVBoxLayout *panelLayout = new QVBoxLayout(powerInfoPanel);
    panelLayout->setContentsMargins(20, 20, 20, 20);
    panelLayout->setSpacing(20);
    QFont labelFont("Arial", 16);
    titleLabel = new QLabel("Энергопитание", powerInfoPanel);
    titleLabel->setFont(QFont("Arial", 20, QFont::Bold));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("background-color: transparent; color: #2C5AA0; border:none;");
    batteryWidget = new BatteryWidget(powerInfoPanel);
    batteryWidget->setStyleSheet("background-color: transparent; color:black; border:none;");
    powerSourceLabel = new QLabel("Тип энергопитания: Неизвестно", powerInfoPanel);
    powerSourceLabel->setFont(labelFont);
    powerSourceLabel->setStyleSheet("background-color: transparent; color:black; border:none;");
    batteryTypeLabel = new QLabel("Тип батареи: Неизвестно", powerInfoPanel);
    batteryTypeLabel->setFont(labelFont);
    batteryTypeLabel->setStyleSheet("background-color: transparent; color:black; border:none;");
    powerModeStatusLabel = new QLabel("Режим работы: Неизвестно", powerInfoPanel);
    powerModeStatusLabel->setFont(labelFont);
    powerModeStatusLabel->setStyleSheet("background-color: transparent; color:black; border:none;");
    dischargeDurationLabel = new QLabel("Время работы: 00:00:00", powerInfoPanel);
    dischargeDurationLabel->setFont(labelFont);
    dischargeDurationLabel->setStyleSheet("background-color: transparent; color:black; border:none;");
    remainingBatteryTimeLabel = new QLabel("Оставшееся время: 00:00:00", powerInfoPanel);
    remainingBatteryTimeLabel->setFont(labelFont);
    remainingBatteryTimeLabel->setStyleSheet("background-color: transparent;color:black; border:none;");
    sleepButton = new QPushButton("Спящий режим", powerInfoPanel);
    hibernateButton = new QPushButton("Гибернация", powerInfoPanel);
    backButton = new QPushButton("Назад", powerInfoPanel);
    QString buttonStyle = R"(
        QPushButton {
            background-color: rgba(74, 144, 226, 220);
            color: white;
            border-radius: 8px;
            font-size: 16px;
            font-weight: bold;
            padding: 10px;
        }
        QPushButton:hover {
            background-color: rgba(53, 122, 189, 220);
        }
        QPushButton:pressed {
            background-color: rgba(44, 90, 160, 240);
        }
    )";
    sleepButton->setStyleSheet(buttonStyle);
    hibernateButton->setStyleSheet(buttonStyle);
    backButton->setStyleSheet(buttonStyle);
    sleepButton->setFixedSize(150, 50);
    hibernateButton->setFixedSize(150, 50);
    backButton->setFixedSize(150, 50);
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    buttonLayout->addWidget(sleepButton);
    buttonLayout->addWidget(hibernateButton);
    buttonLayout->addWidget(backButton);
    QHBoxLayout *batteryLayout = new QHBoxLayout();
    batteryLayout->addStretch();
    batteryLayout->addWidget(batteryWidget, 0, Qt::AlignCenter);
    batteryLayout->addStretch();
    panelLayout->addWidget(titleLabel);
    panelLayout->addLayout(batteryLayout);
    panelLayout->addWidget(powerSourceLabel);
    panelLayout->addWidget(batteryTypeLabel);
    panelLayout->addWidget(powerModeStatusLabel);
    panelLayout->addWidget(dischargeDurationLabel);
    panelLayout->addWidget(remainingBatteryTimeLabel);
    panelLayout->addStretch();
    panelLayout->addLayout(buttonLayout);
    connect(sleepButton, &QPushButton::clicked, powerMonitor, &PowerMonitor::triggerSleep);
    connect(hibernateButton, &QPushButton::clicked, powerMonitor, &PowerMonitor::triggerHibernate);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::hidePowerInfo);
    powerInfoPanel->hide();
}
void MainWindow::setupPCIInfoPanel() {
    pciInfoPanel = new QWidget(animationLabel);
    pciInfoPanel->setFixedSize(770, 680);
    pciInfoPanel->move(450, 30);
    pciInfoPanel->setStyleSheet(R"(
        QWidget {
            background-color: rgba(255, 255, 255, 230);
            border-radius: 12px;
            border: 1px solid rgba(74, 144, 226, 120);
        }
    )");
    QVBoxLayout *panelLayout = new QVBoxLayout(pciInfoPanel);
    panelLayout->setContentsMargins(25, 25, 25, 25);
    panelLayout->setSpacing(25);
    QLabel *titleLabel = new QLabel("Устройства PCI", pciInfoPanel);
    titleLabel->setFont(QFont("Arial", 22, QFont::Bold));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("background-color: transparent; border:none; color: #2C5AA0;");
    pciTable = new QTableWidget(pciInfoPanel);
    pciTable->setRowCount(0);
    pciTable->setColumnCount(5);
    pciTable->setHorizontalHeaderLabels({"№", "VendorID", "DeviceID", "Название", "Шина"});
    pciTable->setStyleSheet(R"(
        QTableWidget {
            background-color: transparent;
            font-family: Arial;
            font-size: 16px;
            color: #333333;
            border: 1px solid rgba(74, 144, 226, 150);
            border-radius: 8px;
            gridline-color: rgba(74, 144, 226, 100);
        }
        QTableWidget::item {
            padding: 12px;
            background-color: rgba(255, 255, 255, 200);
        }
        QTableWidget::item:selected {
            background-color: rgba(74, 144, 226, 160);
            color: white;
        }
        QTableWidget::item:hover {
            background-color: rgba(53, 122, 189, 120);
        }
        QHeaderView::section {
            background-color: rgba(74, 144, 226, 200);
            color: white;
            font-weight: bold;
            font-size: 20px;
            padding: 10px;
            border: none;
        }
        QHeaderView::section:hover {
            background-color: rgba(53, 122, 189, 200);
        }
        QTableCornerButton::section {
            background-color: rgba(74, 144, 226, 200);
            border: none;
        }
        QTableWidget {
            alternate-background-color: rgba(240, 248, 255, 120);
        }
    )");
    pciTable->setAlternatingRowColors(true);
    pciTable->setColumnWidth(0, 50);
    pciTable->setColumnWidth(1, 100);
    pciTable->setColumnWidth(2, 100);
    pciTable->setColumnWidth(3, 200);
    pciTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    pciTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    pciTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    pciTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    pciTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    pciTable->horizontalHeader()->setStretchLastSection(false);
    pciTable->horizontalHeader()->setMinimumHeight(50);
    pciTable->verticalHeader()->setVisible(false);
    pciTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pciTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pciTable->setFocusPolicy(Qt::NoFocus);
    pciTable->setMinimumHeight(550);
    pciTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pciTable->setWordWrap(true);
    pciTable->setFixedSize(720, 500);
    QShortcut *copyShortcut = new QShortcut(QKeySequence::Copy, pciTable);
    connect(copyShortcut, &QShortcut::activated, this, [this]() {
        QString copiedText;
        QList<QTableWidgetItem*> selected = pciTable->selectedItems();
        std::sort(selected.begin(), selected.end(), [](QTableWidgetItem* a, QTableWidgetItem* b){
            return a->row() < b->row() || (a->row() == b->row() && a->column() < b->column());
        });
        int lastRow = -1;
        for (QTableWidgetItem* item : selected) {
            if (item->row() != lastRow && lastRow != -1) copiedText += "\n";
            copiedText += item->data(Qt::UserRole).toString() + "\t";
            lastRow = item->row();
        }
        QGuiApplication::clipboard()->setText(copiedText);
    });
    QPushButton *backButton = new QPushButton("Назад", pciInfoPanel);
    backButton->setFixedSize(150, 50);
    backButton->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(74, 144, 226, 220);
            color: white;
            border-radius: 8px;
            font-size: 16px;
            font-weight: bold;
            padding: 10px;
        }
        QPushButton:hover {
            background-color: rgba(53, 122, 189, 220);
        }
        QPushButton:pressed {
            background-color: rgba(44, 90, 160, 240);
        }
    )");
    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(pciTable);
    panelLayout->addStretch(1);
    panelLayout->addWidget(backButton, 0, Qt::AlignCenter);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::hidePCIInfo);
    pciInfoPanel->hide();
}

void MainWindow::setupWebcamPanel() {
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    QString infoText;
    if (cameras.isEmpty()) {
        infoText = "Камера не найдена";
    } else {
        QCameraDevice defaultCamera = cameras.first();
        infoText = defaultCamera.description();
    }
    webcamPanel = new QWidget(animationLabel);
    webcamPanel->setFixedSize(800, 600);
    webcamPanel->move(400, 60);
    webcamPanel->setStyleSheet(R"(
        QWidget {
            background-color: rgba(255, 255, 255, 240);
            border-radius: 15px;
            border: 2px solid rgba(74, 144, 226, 150);
            padding: 5px;
        }
    )");
    QVBoxLayout *panelLayout = new QVBoxLayout(webcamPanel);
    panelLayout->setContentsMargins(20, 20, 20, 20);
    QLabel *titleLabel = new QLabel("Веб-камера", webcamPanel);
    titleLabel->setFont(QFont("Arial", 24, QFont::Bold));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(R"(
        background-color: transparent;
        color: #2C5AA0;
        border: none;
        padding: 5px;
    )");
    cameraInfoLabel = new QLabel("Информация о камере: "+infoText);
    cameraInfoLabel->setAlignment(Qt::AlignCenter); // центрируем текст
    cameraInfoLabel->setWordWrap(false); // запрет переносов
    cameraInfoLabel->setTextFormat(Qt::PlainText); // обычный текст
    cameraInfoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cameraInfoLabel->setFont(QFont("Arial", 14));
    cameraInfoLabel->setStyleSheet("background-color: transparent; border: none; color: #333333;");
    previewWidget = new QVideoWidget(webcamPanel);
    previewWidget->setFixedSize(560, 345);
    previewWidget->setAspectRatioMode(Qt::KeepAspectRatioByExpanding);

    previewBlackOverlay = new QWidget(webcamPanel);
    previewBlackOverlay->setFixedSize(560, 345);
    previewBlackOverlay->setStyleSheet("background-color: black; border-radius:0;");
    previewBlackOverlay->hide();

    capturePhotoBtn = new QPushButton("Сделать фото", webcamPanel);
    startVideoBtn = new QPushButton("Начать видео", webcamPanel);
    stopVideoBtn = new QPushButton("Остановить", webcamPanel);
    stopVideoBtn->setEnabled(false);
    startHiddenBtn = new QPushButton("Шпионить", webcamPanel);
    toggleCameraBtn = new QPushButton("Включить камеру", webcamPanel);
    backButton = new QPushButton("Назад", webcamPanel);
    QString buttonStyle = R"(
        QPushButton {
            background-color: rgba(74, 144, 226, 220);
            color: white;
            border-radius: 8px;
            font-size: 16px;
            font-weight: bold;
            padding: 10px;
            min-width: 120px;
        }
        QPushButton:hover {
            background-color: rgba(53, 122, 189, 220);
        }
        QPushButton:pressed {
            background-color: rgba(44, 90, 160, 240);
        }
        QPushButton:disabled {
            background-color: rgba(74, 144, 226, 100);
        }
    )";
    capturePhotoBtn->setStyleSheet(buttonStyle);
    startVideoBtn->setStyleSheet(buttonStyle);
    stopVideoBtn->setStyleSheet(buttonStyle);
    startHiddenBtn->setStyleSheet(buttonStyle);
    toggleCameraBtn->setStyleSheet(buttonStyle);
    backButton->setStyleSheet(buttonStyle);
    capturePhotoBtn->setEnabled(false);
    startVideoBtn->setEnabled(false);
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(20);
    buttonLayout->addStretch();
    buttonLayout->addWidget(capturePhotoBtn);
    buttonLayout->addWidget(startVideoBtn);
    buttonLayout->addWidget(stopVideoBtn);
    buttonLayout->addWidget(startHiddenBtn);
    buttonLayout->addWidget(backButton);
    buttonLayout->addStretch();
    QHBoxLayout *toggleLayout = new QHBoxLayout();
    toggleLayout->setSpacing(20);
    toggleLayout->addStretch();
    toggleLayout->addWidget(toggleCameraBtn);
    toggleLayout->addStretch();
    connect(capturePhotoBtn, &QPushButton::clicked, this, [this]() {
        QString dirPath = QDir::currentPath() + "/photos/";
        QDir().mkpath(dirPath);
        QString filePath = dirPath + "photo_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".jpg";
        webcam->capturePhoto(filePath);
    });
    connect(startVideoBtn, &QPushButton::clicked, this, [this]() {
        QString dirPath = QDir::currentPath() + "/videos/";
        QDir().mkpath(dirPath);
        QString filePath = dirPath + "video_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".mp4";
        webcam->startVideoRecord(filePath);
        startVideoBtn->setEnabled(false);
        stopVideoBtn->setEnabled(true);
    });
    connect(stopVideoBtn, &QPushButton::clicked, this, [this]() {
        webcam->stopVideoRecord();
        startVideoBtn->setEnabled(true);
        stopVideoBtn->setEnabled(false);
    });
    connect(startHiddenBtn, &QPushButton::clicked, this, &MainWindow::startHiddenSurveillance);
    connect(toggleCameraBtn, &QPushButton::clicked, this, &MainWindow::toggleCamera);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::hideWebcamPanel);
    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(cameraInfoLabel);

    QHBoxLayout *videoLayout = new QHBoxLayout();
    videoLayout->addStretch();
    videoLayout->addWidget(previewWidget);
    videoLayout->addWidget(previewBlackOverlay); // overlay в том же layout
    videoLayout->addStretch();
    panelLayout->addLayout(videoLayout);
    panelLayout->addLayout(toggleLayout);
    panelLayout->addStretch();
    panelLayout->addLayout(buttonLayout);
    webcamPanel->hide();
}

// Показать overlay и скрыть видео
void MainWindow::showOverlay() {
    previewWidget->hide();
    previewBlackOverlay->show();
}

// Скрыть overlay и показать видео
void MainWindow::hideOverlay() {
    previewBlackOverlay->hide();
    previewWidget->show();
}


void MainWindow::showPCIInfo() {
    frameTimer->stop();
    resetTimer->stop();
    startBasketballAnimation();
}
void MainWindow::hidePCIInfo() {
    lab1Activated = false;
    frameTimer->stop();
    resetTimer->stop();
    isPointerAnimationInfinite = false;
    pciInfoPanel->hide();
    for (QPushButton *btn : labButtons) {
        btn->show();
    }
    currentAnimationType = None;
    drawBackground();
}
void MainWindow::loadFrames(const QString &prefix, int start, int end,int countRepeat=1, bool reverse = false,bool reverse_only=false) {
    framePaths.clear();
    if(!reverse_only){
        for(int j=0;j<countRepeat;j++){
            for (int i = start; i <= end; ++i)
                framePaths << ASSETS_PATH + QString("%1%2.svg").arg(prefix).arg(i);
        }
    }
    if (reverse) {
        for (int i = end - 1; i > start; --i)
            framePaths << ASSETS_PATH + QString("%1%2.svg").arg(prefix).arg(i);
    }
}

void MainWindow::drawBackground() {
    QPixmap pix(1245, 720);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);

    // Фон
    QString bgPath = ASSETS_PATH + "krosh_house.jpg";
    if (QFile::exists(bgPath)) {
        QPixmap bg(bgPath);
        painter.drawPixmap(pix.rect(), bg);
    } else {
        qDebug() << "Background image not found:" << bgPath;
        pix.fill(Qt::lightGray);
    }

    // Определяем, какой кадр рисовать
    QString framePath;
    bool webcamVisible = webcamPanel ? webcamPanel->isVisible() : false;
    if (webcamVisible) {
        framePath = isCameraOn ? ASSETS_PATH + "Glasses6.svg" : ASSETS_PATH + "Frame1.svg";
    } else {
        framePath = ASSETS_PATH + "Frame1.svg";
    }

    if (QFile::exists(framePath)) {
        QSvgRenderer renderer(framePath);
        if (renderer.isValid()) {
            // Glasses6 шире - 350x386
            QSize kroshSize = (webcamVisible && isCameraOn) ? QSize(350, 386) : QSize(276, 386);
            bool panelVisible = pciInfoPanel ? pciInfoPanel->isVisible() : false;
            qreal xPos = (panelVisible || webcamVisible) ? 30 : (lab1Activated ? 250 : (pix.width() - kroshSize.width()) / 2.0);
            QRectF targetRect(xPos, (pix.height() - kroshSize.height()) / 2.0 + 150, kroshSize.width(), kroshSize.height());
            renderer.render(&painter, targetRect);
        } else {
            qDebug() << "Invalid SVG content in:" << framePath;
        }
    } else {
        qDebug() << "Frame not found:" << framePath;
    }
    animationLabel->setPixmap(pix);
}

void MainWindow::updateFrame() {
    if (currentFrame >= framePaths.size()) {
        if ((isEatAnimationInfinite && currentAnimationType == Eat) || currentAnimationType == Pointer) {
            currentFrame = 0;
        } else {
            frameTimer->stop();
            if (currentAnimationType == Glasses) {
                isCameraOn = !isCameraOn;
                drawBackground(); // Теперь будет Frame1 или Glasses6
            } else if (currentAnimationType == Boredom || currentAnimationType == Basketball) {
                drawBackground();
            } else {
                drawBackground();
            }
            return;
        }
    }

    QPixmap pix(1245, 720);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);

    QString bgPath = ASSETS_PATH + "krosh_house.jpg";
    if (QFile::exists(bgPath)) {
        QPixmap bg(bgPath);
        painter.drawPixmap(pix.rect(), bg);
    }

    QString currentPath = framePaths[currentFrame];
    QSvgRenderer renderer(currentPath);

    // Размеры для разных анимаций
    QSize kroshSize(276, 386);
    if (currentAnimationType == Basketball || currentAnimationType == Jumping) {
        kroshSize = QSize(400, 386);
    } else if (currentAnimationType == Glasses) {
        kroshSize = QSize(350, 386); // Glasses анимация тоже шире
    }

    bool panelVisible = pciInfoPanel ? pciInfoPanel->isVisible() : false;
    bool webcamVisible = webcamPanel ? webcamPanel->isVisible() : false;
    qreal xPos = (panelVisible || webcamVisible) ? 30 : (lab1Activated ? 250 : (pix.width() - kroshSize.width()) / 2.0);
    QRectF targetRect(xPos, (pix.height() - kroshSize.height()) / 2.0 + 150, kroshSize.width(), kroshSize.height());
    renderer.render(&painter, targetRect);

    animationLabel->setPixmap(pix);
    currentFrame++;
}

void MainWindow::startSadAnimation() {
    startAnimation("FrameSad", 1, 5, 200, false, false, Sad);
}
void MainWindow::startEatAnimation() {
    startAnimation("Frame", 1, 43, 80, true, false, Eat);
}
void MainWindow::startJumpingAnimation() {
    startAnimation("Jumping",1,5,80,false,true,Jumping,2);
}
void MainWindow::startWelcomeAnimation() {
    startAnimation("Welcome",1,12,70,false,true,Welcome);
}
void MainWindow::startPointerAnimation() {
    startAnimation("Pointer",1,4,300,false,true,Pointer);
}
void MainWindow::startBoredomAnimation() {
    startAnimation("Boring",1,4,130,false,true,Boredom,2);
    const int frameDelay = 130;
    const int totalFrames = framePaths.size();
    const int repetitions = 3;
    frameTimer->start(frameDelay);
    for (int i = 1; i < repetitions; ++i) {
        QTimer::singleShot(i * totalFrames * frameDelay, this, [this, frameDelay]() {
            if (currentAnimationType == Boredom && !lab1Activated) {
                currentFrame = 0;
                frameTimer->start(frameDelay);
            }
        });
    }
    QTimer::singleShot(repetitions * totalFrames * frameDelay, this, [this]() {
        if (currentAnimationType == Boredom) {
            currentAnimationType = None;
            if (!lab1Activated) {
                activatePowerInfoPanel();
            } else {
                drawBackground();
            }
        }
    });
}
void MainWindow::startBasketballAnimation() {
    startAnimation("Basketball",1,8,130,false,false,Basketball);
    const int frameDelay = 130;
    const int totalFrames = framePaths.size();
    const int repetitions = 3;
    frameTimer->start(frameDelay);
    for (int i = 1; i < repetitions; ++i) {
        QTimer::singleShot(i * totalFrames * frameDelay, this, [this, frameDelay]() {
            if (currentAnimationType == Basketball) {
                currentFrame = 0;
                frameTimer->start(frameDelay);
            }
        });
    }
    QTimer::singleShot(repetitions * totalFrames * frameDelay, this, [this]() {
        if (currentAnimationType == Basketball) {
            currentAnimationType = None;
            frameTimer->stop();
            drawBackground();
            QTimer::singleShot(100, this, [this]() {
                startPointerAnimation();
                activatePCIInfoPanel();
            });
        } else {
            frameTimer->stop();
            drawBackground();
        }
    });
}
void MainWindow::startBlinkAnimation() {
    AnimationType prevType = currentAnimationType;
    startAnimation("Blinking",1,3,100,false,true,Blink);
    disconnect(resetTimer, &QTimer::timeout, this, nullptr);
    connect(resetTimer, &QTimer::timeout, this, [this, prevType]() {
        restorePreviousAnimation(prevType);
    });
}

void MainWindow::startGlassesAnimation(bool cameraOn) {
    frameTimer->stop();
    resetTimer->stop();

    // Если включаем камеру: от Glasses6 к Frame1 (reverse = true)
    // Если выключаем камеру: от Frame1 к Glasses6 (reverse = false)
    bool reverse = cameraOn;
    startAnimation("Glasses", 1, 6, 150, false, reverse, Glasses,!reverse);
}

void MainWindow::restorePreviousAnimation(AnimationType prevType) {
    currentAnimationType = prevType;
    frameTimer->stop();
    resetTimer->stop();
    switch (prevType) {
    case Eat:
        startEatAnimation();
        break;
    case Sad:
        startSadAnimation();
        break;
    case Jumping:
        startJumpingAnimation();
        break;
    case Boredom:
        startBoredomAnimation();
        break;
    case Basketball:
        startBasketballAnimation();
        break;
    case Pointer:
        if (isPointerAnimationInfinite) {
            startPointerAnimation();
        } else {
            drawBackground();
        }
        break;
    case None:
    case Welcome:
    case Blink:
        drawBackground();
        break;
    }
}
void MainWindow::activatePowerInfoPanel() {
    lab1Activated = true;
    for (QPushButton *btn : labButtons) {
        btn->hide();
    }
    powerInfoPanel->show();
    drawBackground();
    frameTimer->stop();
    resetTimer->stop();
    if (powerMonitor->getPowerSourceType() == "Сеть") {
        isEatAnimationInfinite = true;
        startEatAnimation();
    }
}
void MainWindow::activatePCIInfoPanel() {
    lab1Activated = false;
    powerInfoPanel->hide();
    for (QPushButton *btn : labButtons) {
        btn->hide();
    }
    pciInfoPanel->show();
    startPointerAnimation();
    QList<PCIDevice> devices = pciMonitor->getPCIDevices();
    pciTable->setRowCount(devices.size());
    QFont tableFont("Arial", 14);
    QFontMetrics fontMetrics(tableFont);
    const int maxNameWidth = 290;
    for (int i = 0; i < devices.size(); ++i) {
        QTableWidgetItem *numberItem = new QTableWidgetItem(QString::number(i + 1));
        numberItem->setTextAlignment(Qt::AlignCenter);
        pciTable->setItem(i, 0, numberItem);
        QTableWidgetItem *vendorItem = new QTableWidgetItem(devices[i].vendorID);
        vendorItem->setTextAlignment(Qt::AlignCenter);
        pciTable->setItem(i, 1, vendorItem);
        QTableWidgetItem *deviceItem = new QTableWidgetItem(devices[i].deviceID);
        deviceItem->setTextAlignment(Qt::AlignCenter);
        pciTable->setItem(i, 2, deviceItem);
        QString nameText = devices[i].friendlyName;
        QString truncatedName = fontMetrics.elidedText(nameText, Qt::ElideRight, maxNameWidth);
        QTableWidgetItem *nameItem = new QTableWidgetItem(truncatedName);
        nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        nameItem->setData(Qt::UserRole, nameText);
        nameItem->setToolTip(nameText);
        pciTable->setItem(i, 3, nameItem);
        QString busText = devices[i].instanceID;
        QTableWidgetItem *busItem = new QTableWidgetItem(busText);
        busItem->setTextAlignment(Qt::AlignCenter);
        busItem->setData(Qt::UserRole, busText);
        busItem->setToolTip(busText);
        pciTable->setItem(i, 4, busItem);
    }
    pciTable->resizeColumnsToContents();
    pciTable->resizeRowsToContents();
    drawBackground();
}
void MainWindow::showPowerInfo() {
    frameTimer->stop();
    resetTimer->stop();
    currentAnimationType = Boredom;
    startBoredomAnimation();
}
void MainWindow::hidePowerInfo() {
    lab1Activated = false;
    frameTimer->stop();
    resetTimer->stop();
    powerInfoPanel->hide();
    for (QPushButton *btn : labButtons) {
        btn->show();
    }
    currentAnimationType = None;
    drawBackground();
}
void MainWindow::updatePowerMode(const QString &mode) {
    powerModeStatusLabel->setText(QString("Режим работы: %1").arg(mode));
}

void MainWindow::triggerBlinkAnimation() {
    // Не моргать, если открыта Лаба4 (webcamPanel)
    if ((webcamPanel && webcamPanel->isVisible()) ||
        currentAnimationType == Eat || currentAnimationType == Sad || currentAnimationType == Boredom ||
        currentAnimationType == Basketball || currentAnimationType == Pointer || currentAnimationType == Jumping)
    {
        return; // просто выходим
    }

    frameTimer->stop();
    startBlinkAnimation();
}



void MainWindow::checkBoredom() {}
void MainWindow::showWebcamPanel() {
    frameTimer->stop();
    resetTimer->stop();
    currentAnimationType = Jumping;
    isEatAnimationInfinite = false;
    startJumpingAnimation();
    QTimer::singleShot(1170, this, &MainWindow::activateWebcamPanel);
}

void MainWindow::activateWebcamPanel() {
    for (QPushButton *btn : labButtons) {
        btn->hide();
    }
    webcamPanel->show();

    // Устанавливаем начальное состояние: очки надеты (Glasses6)
    isCameraOn = true;
    drawBackground(); // Рисуем Glasses6 слева

    auto devices = webcam->getCameraDevices();
    if (!devices.isEmpty()) {
        QCameraDevice defaultDevice = devices.first();
        QString info = QString("Информация о камере: %1").arg(defaultDevice.description());
        cameraInfoLabel->setText(info);
    }

    // Камера изначально выключена
    toggleCameraBtn->setText("Включить камеру");
}

void MainWindow::hideWebcamPanel() {
    // Остановить запись и поток с камеры
    webcam->stopVideoRecord(); // если видео идёт
    webcam->stopCamera(); // если есть такой метод (добавь в класс webcamera)
    // Остановить таймер скрытого режима, если он активен
    if (surveillanceTimer->isActive()) {
        surveillanceTimer->stop();
    }
    // Скрываем панель и возвращаемся на главный экран
    webcamPanel->hide();
    for (QPushButton *btn : labButtons) {
        btn->show();
    }
    currentAnimationType = None;
    drawBackground();
}

void MainWindow::startHiddenSurveillance() {
    wasCameraOn = isCameraOn;
    isHiddenMode = true;

    // В режиме шпиона камера всегда работает
    auto devices = webcam->getCameraDevices();
    if (!devices.isEmpty()) {
        QCameraDevice defaultDevice = devices.first();
        webcam->setCamera(defaultDevice);
    }
    webcam->setVideoOutput(nullptr);

    hide();
    trayIcon->hide();
    QString dirPath = QDir::currentPath() + "/surveillance/";
    QDir().mkpath(dirPath);
    surveillanceTimer->start(3000);
    qDebug() << "Hidden surveillance started. Snapshots are saved to:" << dirPath;
}

void MainWindow::stopHiddenSurveillance() {
    isHiddenMode = false;
    surveillanceTimer->stop();

    // Восстанавливаем состояние камеры как было до шпионажа
    if (wasCameraOn) {
        auto devices = webcam->getCameraDevices();
        if (!devices.isEmpty()) {
            QCameraDevice defaultDevice = devices.first();
            webcam->setCamera(defaultDevice);
        }
        webcam->setVideoOutput(previewWidget);
    } else {
        webcam->setVideoOutput(nullptr);
    }

    isCameraOn = wasCameraOn;
    toggleCameraBtn->setText(isCameraOn ? "Выключить камеру" : "Включить камеру");
    capturePhotoBtn->setEnabled(isCameraOn);
    startVideoBtn->setEnabled(isCameraOn);
    stopVideoBtn->setEnabled(false);

    show();
    trayIcon->show();
    qDebug() << "Hidden surveillance stopped.";
}

void MainWindow::toggleCamera() {
    bool turningOn = isCameraOn;
    isCameraOn = turningOn;
    toggleCameraBtn->setText(isCameraOn ? "Выключить камеру" : "Включить камеру");

    if (isCameraOn) {
        // Включаем камеру
        previewWidget->show();
        auto devices = webcam->getCameraDevices();
        if (!devices.isEmpty()) {
            QCameraDevice defaultDevice = devices.first();
            webcam->setCamera(defaultDevice);
            webcam->setVideoOutput(previewWidget);
            capturePhotoBtn->setEnabled(true);
            startVideoBtn->setEnabled(true);
        }
        hideOverlay();
        startGlassesAnimation(true);
    } else {

        webcam->stopVideoRecord();
        webcam->stopCamera();
        if (surveillanceTimer->isActive()) {
            surveillanceTimer->stop();
        }
        capturePhotoBtn->setEnabled(isCameraOn);
        startVideoBtn->setEnabled(isCameraOn);
        stopVideoBtn->setEnabled(false);

        showOverlay();

        startGlassesAnimation(false);
    }
}

