#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPixmap>
#include <qclipboard.h>
#include <QShortcut>
#include <qguiapplication.h>
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
    pciMonitor = new envirconfigPCI();
    setupPowerInfoPanel();
    setupPCIInfoPanel();

    connect(pciTable->horizontalHeader(), &QHeaderView::sectionResized, this, [=](int logicalIndex, int oldSize, int newSize){
        if (logicalIndex == 3) { // Столбец "Название"
            QFontMetrics metrics(pciTable->font());
            for (int row = 0; row < pciTable->rowCount(); ++row) {
                QTableWidgetItem* item = pciTable->item(row, 3);
                if (!item) continue;
                QString fullText = item->data(Qt::UserRole).toString(); // Полный текст
                QString elided = metrics.elidedText(fullText, Qt::ElideRight, newSize - 10); // -10 для паддинга
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
}

MainWindow::~MainWindow() {}

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
    batteryWidget->setStyleSheet("background-color: transparent; border:none;");

    powerSourceLabel = new QLabel("Тип энергопитания: Неизвестно", powerInfoPanel);
    powerSourceLabel->setFont(labelFont);
    powerSourceLabel->setStyleSheet("background-color: transparent; border:none;");

    batteryTypeLabel = new QLabel("Тип батареи: Неизвестно", powerInfoPanel);
    batteryTypeLabel->setFont(labelFont);
    batteryTypeLabel->setStyleSheet("background-color: transparent; border:none;");

    powerModeStatusLabel = new QLabel("Режим работы: Неизвестно", powerInfoPanel);
    powerModeStatusLabel->setFont(labelFont);
    powerModeStatusLabel->setStyleSheet("background-color: transparent; border:none;");

    dischargeDurationLabel = new QLabel("Время работы: 00:00:00", powerInfoPanel);
    dischargeDurationLabel->setFont(labelFont);
    dischargeDurationLabel->setStyleSheet("background-color: transparent; border:none;");

    remainingBatteryTimeLabel = new QLabel("Оставшееся время: 00:00:00", powerInfoPanel);
    remainingBatteryTimeLabel->setFont(labelFont);
    remainingBatteryTimeLabel->setStyleSheet("background-color: transparent; border:none;");

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
    // Начальные ширины столбцов
    pciTable->setColumnWidth(0, 50);  // №
    pciTable->setColumnWidth(1, 100); // VendorID
    pciTable->setColumnWidth(2, 100); // DeviceID
    pciTable->setColumnWidth(3, 200); // Название

    // Настройка режимов изменения размеров столбцов
    pciTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);     // №
    pciTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);     // VendorID
    pciTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);     // DeviceID
    pciTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive); // Название
    pciTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);   // Шина

    pciTable->horizontalHeader()->setStretchLastSection(false); // Отключаем автоматическое растяжение последнего столбца

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
    panelLayout->addStretch(1); // Increased stretch to push button down
    panelLayout->addWidget(backButton, 0, Qt::AlignCenter);

    connect(backButton, &QPushButton::clicked, this, &MainWindow::hidePCIInfo);

    pciInfoPanel->hide();
}

void MainWindow::showPCIInfo() {
    frameTimer->stop();
    resetTimer->stop();
    startBasketballAnimation(); // Запускаем анимацию баскетбола
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
    lab1Activated = false;
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

void MainWindow::loadBasketballFrames() {
    QString assetsPath = getProjectAssetsPath();
    framePaths.clear();
    framePaths << assetsPath + "Basketball_1.svg"
               << assetsPath + "Basketball_2.svg"
               << assetsPath + "Basketball_3.svg"
               << assetsPath + "Basketball_4.svg"
               << assetsPath + "Basketball_5.svg"
               << assetsPath + "Basketball_6.svg"
               << assetsPath + "Basketball_7.svg"
               << assetsPath + "Basketball_8.svg";
}

void MainWindow::loadPointerFrames() {
    QString assetsPath = getProjectAssetsPath();
    framePaths.clear();
    framePaths << assetsPath + "Pointer_1.svg"
               << assetsPath + "Pointer_2.svg"
               << assetsPath + "Pointer_3.svg"
               << assetsPath + "Pointer_4.svg"
               << assetsPath + "Pointer_3.svg"
               << assetsPath + "Pointer_2.svg";
}

void MainWindow::drawBackground() {
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
        qreal xPos = (pciInfoPanel->isVisible()) ? 100 : (lab1Activated ? 250 : (pix.width() - kroshSize.width()) / 2.0);
        QRectF targetRect(xPos, (pix.height() - kroshSize.height()) / 2.0 + 150, kroshSize.width(), kroshSize.height());
        renderer.render(&painter, targetRect);
    } else {
        qDebug() << "Frame1.svg not found:" << framePath;
    }

    animationLabel->setPixmap(pix);
}

void MainWindow::updateFrame() {
    if (currentFrame >= framePaths.size()) {
        if ((isEatAnimationInfinite && currentAnimationType == Eat) || currentAnimationType == Pointer) {
            currentFrame = 0;
        } else {
            frameTimer->stop();
            if (currentAnimationType == Boredom || currentAnimationType == Basketball) {
                drawBackground();
            } else {
                drawBackground();
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

    QSize kroshSize(276, 386);
    if (currentAnimationType == Basketball) {
        kroshSize = QSize(400, 386);
    }
    qreal xPos = (pciInfoPanel->isVisible()) ? 100 : (lab1Activated ? 250 : (pix.width() - kroshSize.width()) / 2.0);
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

void MainWindow::startPointerAnimation() {
    loadPointerFrames();
    currentFrame = 0;
    currentAnimationType = Pointer;
    isPointerAnimationInfinite = true;
    frameTimer->start(400);
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

void MainWindow::startBasketballAnimation() {
    loadBasketballFrames();
    currentFrame = 0;
    currentAnimationType = Basketball;
    isEatAnimationInfinite = false;
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
                startPointerAnimation(); // Запускаем Pointer-анимацию
                activatePCIInfoPanel();  // Открываем панель PCI
            });
        } else {
            frameTimer->stop();
            drawBackground();
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
    const int maxNameWidth = 290; // Max pixel width for "Название"

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
        nameItem->setData(Qt::UserRole, nameText); // Store full text for copying
        nameItem->setToolTip(nameText); // Show full text on hover
        pciTable->setItem(i, 3, nameItem);

        QString busText = devices[i].instanceID;
        QTableWidgetItem *busItem = new QTableWidgetItem(busText);
        busItem->setTextAlignment(Qt::AlignCenter);
        busItem->setData(Qt::UserRole, busText); // Store full text for copying
        busItem->setToolTip(busText); // Show full text on hover
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
    if (currentAnimationType != Eat && currentAnimationType != Sad && currentAnimationType != Boredom && currentAnimationType != Basketball && currentAnimationType != Pointer) {
        frameTimer->stop();
        startBlinkAnimation();
    }
}

void MainWindow::checkBoredom() {
}
