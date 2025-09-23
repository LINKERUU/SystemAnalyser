#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPixmap>
#include <QPainter>
#include <QSvgRenderer>
#include <QLabel>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QTableWidget>
#include <QHeaderView> // Added for QTableWidget::horizontalHeader()

QString getProjectAssetsPath() {
    QString sourceFilePath = __FILE__;
    QDir sourceDir(sourceFilePath);
    return sourceDir.absoluteFilePath("../assets/") + "/";
}

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

    QString assetsPath = getProjectAssetsPath();
    QString svgPath = assetsPath + "battery_" + QString::number(imageIndex) + ".svg";

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
    lab1Activated(false), currentAnimationType(None) {
    setFixedSize(1245, 720);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    animationLabel = new QLabel(this);
    animationLabel->setFixedSize(1245, 720);
    animationLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(animationLabel);
    setCentralWidget(central);

    QStringList labNames = { "Лабораторная 1", "Лабораторная 2", "Лабораторная 3", "Лабораторная 4", "Лабораторная 5", "Лабораторная 6" };
    int startX = 40;
    int startY = 30;
    int spacing = 40;

    QPushButton *lab1Button = nullptr;
    QPushButton *lab2Button = nullptr;

    QString assetsPath = getProjectAssetsPath();



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
    }

    connect(lab1Button, &QPushButton::clicked, this, &MainWindow::showPowerInfo);
    connect(lab2Button, &QPushButton::clicked, this, &MainWindow::showPCIInfo);

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
    pciMonitor = new envirconfigPCI(); // Removed parent parameter
    setupPowerInfoPanel();
    setupPCIInfoPanel();

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
}

MainWindow::~MainWindow() {}

void MainWindow::setupPowerInfoPanel() {
    powerInfoPanel = new QWidget(animationLabel);
    powerInfoPanel->setFixedSize(500, 640);
    powerInfoPanel->move(700, 30);
    powerInfoPanel->setStyleSheet("background-color: rgba(255, 255, 255, 200); border-radius: 10px;");

    QVBoxLayout *panelLayout = new QVBoxLayout(powerInfoPanel);
    panelLayout->setContentsMargins(20, 20, 20, 20);
    panelLayout->setSpacing(20);

    QFont labelFont("Arial", 16);

    titleLabel = new QLabel("Энергопитание", powerInfoPanel);
    titleLabel->setFont(QFont("Arial", 20, QFont::Bold));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("background-color:transparent;");

    batteryWidget = new BatteryWidget(powerInfoPanel);
    batteryWidget->setStyleSheet("background-color:transparent;");

    powerSourceLabel = new QLabel("Тип энергопитания: Неизвестно", powerInfoPanel);
    powerSourceLabel->setFont(labelFont);
    powerSourceLabel->setStyleSheet("background-color:transparent;");

    batteryTypeLabel = new QLabel("Тип батареи: Неизвестно", powerInfoPanel);
    batteryTypeLabel->setFont(labelFont);
    batteryTypeLabel->setStyleSheet("background-color:transparent;");

    powerModeStatusLabel = new QLabel("Режим работы: Неизвестно", powerInfoPanel);
    powerModeStatusLabel->setFont(labelFont);
    powerModeStatusLabel->setStyleSheet("background-color:transparent;");

    dischargeDurationLabel = new QLabel("Время работы: 00:00:00", powerInfoPanel);
    dischargeDurationLabel->setFont(labelFont);
    dischargeDurationLabel->setStyleSheet("background-color:transparent;");

    remainingBatteryTimeLabel = new QLabel("Оставшееся время: 00:00:00", powerInfoPanel);
    remainingBatteryTimeLabel->setFont(labelFont);
    remainingBatteryTimeLabel->setStyleSheet("background-color:transparent;");

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
    pciInfoPanel->setFixedSize(500, 640);
    pciInfoPanel->move(700, 30);
    pciInfoPanel->setStyleSheet("background-color: rgba(255, 255, 255, 200); border-radius: 10px;");

    QVBoxLayout *panelLayout = new QVBoxLayout(pciInfoPanel);
    panelLayout->setContentsMargins(20, 20, 20, 20);
    panelLayout->setSpacing(20);

    QFont labelFont("Arial", 16);

    QLabel *titleLabel = new QLabel("Устройства PCI", pciInfoPanel);
    titleLabel->setFont(QFont("Arial", 20, QFont::Bold));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("background-color:transparent;");

    pciTable = new QTableWidget(pciInfoPanel);
    pciTable->setRowCount(0);
    pciTable->setColumnCount(5); // Changed to 5 columns
    pciTable->setHorizontalHeaderLabels({"№", "VendorID", "DeviceID", "Название", "Шина"});
    pciTable->setStyleSheet("background-color: transparent; font-size: 14px;");
    pciTable->setColumnWidth(0, 50);
    pciTable->setColumnWidth(1, 100);
    pciTable->setColumnWidth(2, 100);
    pciTable->setColumnWidth(3, 150);
    pciTable->setColumnWidth(4, 100);
    pciTable->horizontalHeader()->setStretchLastSection(true);
    pciTable->setSelectionMode(QAbstractItemView::NoSelection);
    pciTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

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
    panelLayout->addStretch();
    panelLayout->addWidget(backButton, 0, Qt::AlignCenter);

    connect(backButton, &QPushButton::clicked, this, &MainWindow::hidePCIInfo);

    pciInfoPanel->hide();
}

void MainWindow::showPCIInfo() {
    frameTimer->stop();
    resetTimer->stop();
    lab1Activated = false;
    powerInfoPanel->hide();
    for (QPushButton *btn : labButtons) {
        btn->hide();
    }
    pciInfoPanel->show();

    QList<PCIDevice> devices = pciMonitor->getPCIDevices();
    pciTable->setRowCount(devices.size());
    for (int i = 0; i < devices.size(); ++i) {
        pciTable->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        pciTable->setItem(i, 1, new QTableWidgetItem(devices[i].vendorID));
        pciTable->setItem(i, 2, new QTableWidgetItem(devices[i].deviceID));
        pciTable->setItem(i, 3, new QTableWidgetItem(devices[i].title));
        pciTable->setItem(i, 4, new QTableWidgetItem(devices[i].busInfo));
    }

    drawBackground(true);
    currentAnimationType = Sad;
    startSadAnimation();
}

void MainWindow::hidePCIInfo() {
    lab1Activated = false;
    frameTimer->stop();
    resetTimer->stop();
    pciInfoPanel->hide();
    for (QPushButton *btn : labButtons) {
        btn->show();
    }
    currentAnimationType = None;
    drawBackground();
}

void MainWindow::loadSadFrames() {
    QString assetsPath = getProjectAssetsPath();
    framePaths.clear();
    framePaths << assetsPath + "Frame1_sad.svg"
               << assetsPath + "Frame2_sad.svg"
               << assetsPath + "Frame3_sad.svg"
               << assetsPath + "Frame4_sad.svg"
               << assetsPath + "Frame5_sad.svg";
}

void MainWindow::loadEatFrames() {
    QString assetsPath = getProjectAssetsPath();
    framePaths.clear();
    framePaths << assetsPath + "Frame1.svg"
               << assetsPath + "Frame2.svg"
               << assetsPath + "Frame3.svg"
               << assetsPath + "Frame4.svg"
               << assetsPath + "Frame5.svg"
               << assetsPath + "Frame6.svg"
               << assetsPath + "Frame7.svg"
               << assetsPath + "Frame8.svg"
               << assetsPath + "Frame9.svg"
               << assetsPath + "Frame10.svg"
               << assetsPath + "Frame11.svg"
               << assetsPath + "Frame12.svg"
               << assetsPath + "Frame13.svg"
               << assetsPath + "Frame14.svg"
               << assetsPath + "Frame15.svg"
               << assetsPath + "Frame16.svg"
               << assetsPath + "Frame17.svg"
               << assetsPath + "Frame18.svg"
               << assetsPath + "Frame19.svg"
               << assetsPath + "Frame20.svg"
               << assetsPath + "Frame21.svg"
               << assetsPath + "Frame22.svg"
               << assetsPath + "Frame23.svg"
               << assetsPath + "Frame24.svg"
               << assetsPath + "Frame25.svg"
               << assetsPath + "Frame26.svg"
               << assetsPath + "Frame27.svg"
               << assetsPath + "Frame28.svg"
               << assetsPath + "Frame29.svg"
               << assetsPath + "Frame30.svg"
               << assetsPath + "Frame31.svg"
               << assetsPath + "Frame32.svg"
               << assetsPath + "Frame33.svg"
               << assetsPath + "Frame34.svg"
               << assetsPath + "Frame35.svg"
               << assetsPath + "Frame36.svg"
               << assetsPath + "Frame37.svg"
               << assetsPath + "Frame38.svg"
               << assetsPath + "Frame39.svg"
               << assetsPath + "Frame40.svg"
               << assetsPath + "Frame41.svg"
               << assetsPath + "Frame42.svg"
               << assetsPath + "Frame43.svg"
               << assetsPath + "Frame1.svg";
}

void MainWindow::loadWelcomeFrames() {
    QString assetsPath = getProjectAssetsPath();
    framePaths.clear();
    framePaths << assetsPath + "Welcome_1.svg"
               << assetsPath + "Welcome_2.svg"
               << assetsPath + "Welcome_3.svg"
               << assetsPath + "Welcome_4.svg"
               << assetsPath + "Welcome_5.svg"
               << assetsPath + "Welcome_6.svg"
               << assetsPath + "Welcome_7.svg"
               << assetsPath + "Welcome_8.svg"
               << assetsPath + "Welcome_9.svg"
               << assetsPath + "Welcome_10.svg"
               << assetsPath + "Welcome_11.svg"
               << assetsPath + "Welcome_12.svg"
               << assetsPath + "Welcome_7.svg"
               << assetsPath + "Welcome_6.svg"
               << assetsPath + "Welcome_5.svg"
               << assetsPath + "Welcome_4.svg"
               << assetsPath + "Welcome_3.svg"
               << assetsPath + "Welcome_2.svg"
               << assetsPath + "Welcome_1.svg";
}

void MainWindow::loadBlinkFrames() {
    QString assetsPath = getProjectAssetsPath();
    framePaths.clear();
    framePaths << assetsPath + "Blinking_1.svg"
               << assetsPath + "Blinking_2.svg"
               << assetsPath + "Blinking_3.svg"
               << assetsPath + "Blinking_2.svg"
               << assetsPath + "Blinking_1.svg";
}

void MainWindow::loadBoredomFrames() {
    QString assetsPath = getProjectAssetsPath();
    framePaths.clear();
    framePaths << assetsPath + "Boring_1.svg"
               << assetsPath + "Boring_2.svg"
               << assetsPath + "Boring_3.svg"
               << assetsPath + "Boring_4.svg"
               << assetsPath + "Boring_3.svg"
               << assetsPath + "Boring_2.svg"
               << assetsPath + "Boring_1.svg";
}

void MainWindow::drawBackground(bool center) {
    QString assetsPath = getProjectAssetsPath();
    QPixmap pix(1245, 720);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    QString bgPath = assetsPath + "krosh_house.jpg";
    if (QFile::exists(bgPath)) {
        QPixmap bg(bgPath);
        painter.drawPixmap(pix.rect(), bg);
    } else {
        qDebug() << "Background image not found:" << bgPath;
        pix.fill(Qt::lightGray);
    }

    QString framePath = assetsPath + "Frame1.svg";
    if (QFile::exists(framePath)) {
        QSvgRenderer renderer(framePath);
        const QSize kroshSize(276, 386);
        qreal xPos = (lab1Activated || pciInfoPanel->isVisible()) ? 250 : (pix.width() - kroshSize.width()) / 2.0;
        QRectF targetRect(xPos, (pix.height() - kroshSize.height()) / 2.0 + 150, kroshSize.width(), kroshSize.height());
        renderer.render(&painter, targetRect);
    } else {
        qDebug() << "Frame1.svg not found:" << framePath;
    }

    animationLabel->setPixmap(pix);
}

void MainWindow::updateFrame() {
    if (currentFrame >= framePaths.size()) {
        if (isEatAnimationInfinite && currentAnimationType == Eat) {
            currentFrame = 0;
        } else {
            frameTimer->stop();
            if (currentAnimationType == Boredom) {
                // Не останавливаем, singleShot обработает окончание
            } else {
                resetTimer->start(3000);
            }
            return;
        }
    }

    QString assetsPath = getProjectAssetsPath();
    QPixmap pix(1245, 720);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    QString bgPath = assetsPath + "krosh_house.jpg";
    if (QFile::exists(bgPath)) {
        QPixmap bg(bgPath);
        painter.drawPixmap(pix.rect(), bg);
    } else {
        qDebug() << "Background image not found:" << bgPath;
        pix.fill(Qt::lightGray);
    }

    QString currentPath = framePaths[currentFrame];
    if (!QFile::exists(currentPath)) {
        qDebug() << "Animation file not found:" << currentPath << "- Stopping animation";
        frameTimer->stop();
        drawBackground();
        return;
    }

    QSvgRenderer renderer(currentPath);
    if (!renderer.isValid()) {
        qDebug() << "Failed to load SVG for animation:" << currentPath << "- Stopping animation";
        frameTimer->stop();
        return;
    }

    const QSize kroshSize(276, 386);
    qreal xPos = (lab1Activated || pciInfoPanel->isVisible()) ? 250 : (pix.width() - kroshSize.width()) / 2.0;
    QRectF targetRect(xPos, (pix.height() - kroshSize.height()) / 2.0 + 150, kroshSize.width(), kroshSize.height());
    renderer.render(&painter, targetRect);

    animationLabel->setPixmap(pix);
    currentFrame++;
}

void MainWindow::startSadAnimation() {
    loadSadFrames();
    currentFrame = 0;
    currentAnimationType = Sad;
    isEatAnimationInfinite = false;
    frameTimer->start(200);
}

void MainWindow::startEatAnimation() {
    loadEatFrames();
    currentFrame = 0;
    currentAnimationType = Eat;
    frameTimer->start(80);
}

void MainWindow::startWelcomeAnimation() {
    loadWelcomeFrames();
    currentFrame = 0;
    currentAnimationType = Welcome;
    isEatAnimationInfinite = false;
    frameTimer->start(70);
}

void MainWindow::startBoredomAnimation() {
    loadBoredomFrames();
    currentFrame = 0;
    currentAnimationType = Boredom;
    isEatAnimationInfinite = false;
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

void MainWindow::startBlinkAnimation() {
    AnimationType prevType = currentAnimationType;
    loadBlinkFrames();
    currentFrame = 0;
    currentAnimationType = Blink;
    frameTimer->start(100);

    disconnect(resetTimer, &QTimer::timeout, this, nullptr);
    connect(resetTimer, &QTimer::timeout, this, [this, prevType]() {
        restorePreviousAnimation(prevType);
    });
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
    case Boredom:
        startBoredomAnimation();
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
    drawBackground(true);
    frameTimer->stop();
    resetTimer->stop();
    if (powerMonitor->getPowerSourceType() == "Сеть") {
        isEatAnimationInfinite = true;
        startEatAnimation();
    }
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

void MainWindow::updatePowerInfo(const QString &powerSource, const QString &batteryType, int level,
                                 bool powerSavingEnabled, const QTime &dischargeDuration,
                                 const QTime &remainingTime) {
    powerSourceLabel->setText("Тип энергопитания: " + powerSource);
    batteryTypeLabel->setText("Тип батареи: " + batteryType);
    batteryWidget->setBatteryLevel(level);
    dischargeDurationLabel->setText("Время работы: " + dischargeDuration.toString("hh:mm:ss"));
    remainingBatteryTimeLabel->setText("Оставшееся время: " + remainingTime.toString("hh:mm:ss"));
}

void MainWindow::updatePowerMode(const QString &mode) {
    powerModeStatusLabel->setText(QString("Режим работы: %1").arg(mode));
}

void MainWindow::triggerBlinkAnimation() {
    if (currentAnimationType != Eat && currentAnimationType != Sad && currentAnimationType != Boredom) {
        frameTimer->stop();
        startBlinkAnimation();
    }
}

void MainWindow::checkBoredom() {
    // No longer used, as boredom animation is triggered only by showPowerInfo
}
