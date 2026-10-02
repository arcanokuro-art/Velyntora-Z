/* This file is part of KimageShop^WKrayon^WKrita
 *
 *  SPDX-FileCopyrightText: 2006 Boudewijn Rempt <boud@valdyas.org>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_statusbar.h"

#include <QToolButton>
#include <QAction>
#include <QToolTip>
#include <QStatusBar>
#include <QColor>
#include <QFrame>
#include <QColorDialog>
#include <QSettings>
#include <QMenu>
#include <QTimer>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QTabletEvent>

#include <utility>

#include <ksqueezedtextlabel.h>
#include <klocalizedstring.h>
#include <kformat.h>

#include <KoColor.h>
#include <KoColorProfile.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>
#include <KoToolManager.h>
#include <KoViewConverter.h>
#include <QHBoxLayout>

#include <KisUsageLogger.h>

#include <kis_icon_utils.h>

#include <kis_types.h>
#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_selection.h>
#include <kis_paint_device.h>
#include <kis_selection_manager.h>
#include "kis_memory_statistics_server.h"

#include "KisView.h"
#include "KisDocument.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "canvas/kis_canvas2.h"
#include "canvas/kis_coordinates_converter.h"
#include "kis_progress_widget.h"
#include "kis_zoom_manager.h"
#include <KisAngleSelector.h>
#include <kis_canvas_controller.h>
#include <kis_signals_blocker.h>

#include "KisMainWindow.h"
#include "kis_config.h"

#include "widgets/KisMemoryReportButton.h"

enum {
    IMAGE_SIZE_ID,
    POINTER_POSITION_ID
};

KisStatusBar::KisStatusBar(KisViewManager *viewManager)
    : m_viewManager(viewManager)
    , m_imageView(0)
    , m_statusBar(0)
{
}

void KisStatusBar::setup()
{
    m_selectionStatus = new QToolButton();
    m_selectionStatus->setObjectName("selection status");
    m_selectionStatus->setIconSize(QSize(16,16));
    m_selectionStatus->setAutoRaise(true);
    m_selectionStatus->setEnabled(false);
    updateSelectionIcon();

    m_statusBar = m_viewManager->mainWindow()->statusBar();

    // Functional active-tool indicator for Velyntora Z. The label is fed by
    // KoToolManager, so it always reflects the tool Krita actually activated.
    m_velyntoraActiveToolLabel = new QLabel(m_statusBar);
    m_velyntoraActiveToolLabel->setObjectName("VelyntoraActiveTool");
    m_velyntoraActiveToolLabel->setContentsMargins(6, 0, 8, 0);
    m_velyntoraActiveToolLabel->setMinimumWidth(130);
    m_velyntoraActiveToolLabel->setMaximumWidth(240);
    m_velyntoraActiveToolLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_velyntoraActiveToolLabel->setAccessibleName(i18n("Active tool"));
    addStatusBarItem(m_velyntoraActiveToolLabel);
    connect(KoToolManager::instance(), &KoToolManager::changedTool,
            this, [this](KoCanvasController *) { updateVelyntoraActiveTool(); });
    connect(KoToolManager::instance(), &KoToolManager::changedCanvas,
            this, [this](const KoCanvasBase *) { updateVelyntoraActiveTool(); });
    updateVelyntoraActiveTool();

    // Velyntora Z: compact Pinta-like quick color strip.  These buttons use
    // Krita's own canvas resource provider, so choosing a swatch changes the
    // actual foreground painting color instead of maintaining a second color
    // system just for the UI.
    QWidget *quickColors = new QWidget(m_statusBar);
    m_velyntoraForegroundColor = new QToolButton(quickColors);
    m_velyntoraBackgroundColor = new QToolButton(quickColors);
    m_velyntoraForegroundColor->setObjectName("VelyntoraForegroundColor");
    m_velyntoraBackgroundColor->setObjectName("VelyntoraBackgroundColor");
    m_velyntoraForegroundColor->setFixedSize(26, 26);
    m_velyntoraBackgroundColor->setFixedSize(26, 26);
    m_velyntoraForegroundColor->setToolTip(i18n("Foreground color"));
    m_velyntoraBackgroundColor->setToolTip(i18n("Background color"));
    quickColors->setObjectName("VelyntoraQuickColors");
    QHBoxLayout *quickColorsLayout = new QHBoxLayout(quickColors);
    quickColorsLayout->setContentsMargins(4, 1, 6, 1);
    quickColorsLayout->setSpacing(2);

    auto updateColorButton = [](QToolButton *button, const KoColor &color) {
        const QColor displayColor = color.toQColor();
        button->setStyleSheet(QStringLiteral(
            "QToolButton { background:%1; border:2px solid palette(mid); padding:0px; }")
            .arg(displayColor.name(QColor::HexArgb)));
    };

    updateColorButton(m_velyntoraForegroundColor, m_viewManager->canvasResourceProvider()->fgColor());
    updateColorButton(m_velyntoraBackgroundColor, m_viewManager->canvasResourceProvider()->bgColor());
    // Pinta-like overlapping foreground/background chips.  Keep them as
    // separate real Krita resources while presenting them as one compact
    // control that is easy to recognize on touch screens.
    QWidget *dualColor = new QWidget(quickColors);
    dualColor->setFixedSize(42, 34);
    QGridLayout *dualColorLayout = new QGridLayout(dualColor);
    dualColorLayout->setContentsMargins(0, 0, 0, 0);
    dualColorLayout->setSpacing(0);
    m_velyntoraBackgroundColor->setFixedSize(26, 26);
    m_velyntoraForegroundColor->setFixedSize(26, 26);
    dualColorLayout->addWidget(m_velyntoraBackgroundColor, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    dualColorLayout->addWidget(m_velyntoraForegroundColor, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    m_velyntoraBackgroundColor->lower();
    m_velyntoraForegroundColor->raise();
    quickColorsLayout->addWidget(dualColor);
    quickColorsLayout->addSpacing(4);

    // Clicking either large swatch opens an unrestricted color picker.
    // The chosen QColor is converted into Krita's RGB8 KoColor and written
    // back through the normal canvas resource provider.
    connect(m_velyntoraForegroundColor, &QToolButton::clicked, this, [this]() {
        const QColor initial = m_viewManager->canvasResourceProvider()->fgColor().toQColor();
        const QColor selected = QColorDialog::getColor(initial, m_viewManager->mainWindow(),
                                                       i18n("Choose foreground color"),
                                                       QColorDialog::ShowAlphaChannel);
        if (selected.isValid()) {
            m_viewManager->canvasResourceProvider()->setFGColor(
                KoColor(selected, KoColorSpaceRegistry::instance()->rgb8()));
        }
    });
    connect(m_velyntoraBackgroundColor, &QToolButton::clicked, this, [this]() {
        const QColor initial = m_viewManager->canvasResourceProvider()->bgColor().toQColor();
        const QColor selected = QColorDialog::getColor(initial, m_viewManager->mainWindow(),
                                                       i18n("Choose background color"),
                                                       QColorDialog::ShowAlphaChannel);
        if (selected.isValid()) {
            m_viewManager->canvasResourceProvider()->setBGColor(
                KoColor(selected, KoColorSpaceRegistry::instance()->rgb8()));
        }
    });

    connect(m_viewManager->canvasResourceProvider(), &KisCanvasResourceProvider::sigFGColorChanged,
            this, [this, updateColorButton](const KoColor &color) {
                updateColorButton(m_velyntoraForegroundColor, color);
            });
    connect(m_viewManager->canvasResourceProvider(), &KisCanvasResourceProvider::sigBGColorChanged,
            this, [this, updateColorButton](const KoColor &color) {
                updateColorButton(m_velyntoraBackgroundColor, color);
            });

    const QList<QColor> velyntoraColors {
        QColor("#000000"), QColor("#404040"), QColor("#808080"), QColor("#c0c0c0"),
        QColor("#ffffff"), QColor("#7f0000"), QColor("#ff0000"), QColor("#ff7f00"),
        QColor("#ffff00"), QColor("#7fff00"), QColor("#00a000"), QColor("#00ffff"),
        QColor("#007fff"), QColor("#0000ff"), QColor("#7f00ff"), QColor("#ff00ff"),
        QColor("#ff7fbf")
    };

    auto makeColorButton = [this, quickColors](const QColor &color, bool removable) {
        QToolButton *swatch = new QToolButton(quickColors);
        swatch->setFixedSize(22, 22);
        swatch->setToolTip(color.name(QColor::HexArgb));
        swatch->setAccessibleName(i18n("Quick color %1", color.name(QColor::HexArgb)));
        swatch->setStyleSheet(QStringLiteral(
            "QToolButton { background:%1; border:1px solid palette(mid); padding:0px; }"
            "QToolButton:pressed { border:2px solid palette(highlight); }")
            .arg(color.name(QColor::HexArgb)));

        connect(swatch, &QToolButton::clicked, this, [this, color]() {
            m_viewManager->canvasResourceProvider()->setFGColor(
                KoColor(color, KoColorSpaceRegistry::instance()->rgb8()));
        });

        if (removable) {
            auto removeCustomColor = [swatch, color]() {
                QSettings settings;
                QStringList colors =
                    settings.value(QStringLiteral("Velyntora/CustomQuickColors")).toStringList();
                colors.removeAll(color.name(QColor::HexArgb));
                settings.setValue(QStringLiteral("Velyntora/CustomQuickColors"), colors);
                swatch->deleteLater();
            };

            swatch->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(swatch, &QWidget::customContextMenuRequested, this,
                    [swatch, removeCustomColor](const QPoint &pos) {
                QMenu menu;
                QAction *remove = menu.addAction(i18n("Remove custom color"));
                if (menu.exec(swatch->mapToGlobal(pos)) == remove) {
                    removeCustomColor();
                }
            });

            // Android/touch: holding a custom swatch for 650 ms opens the
            // same delete action without requiring a desktop right click.
            QTimer *holdTimer = new QTimer(swatch);
            holdTimer->setSingleShot(true);
            holdTimer->setInterval(650);
            connect(swatch, &QToolButton::pressed, holdTimer,
                    qOverload<>(&QTimer::start));
            connect(swatch, &QToolButton::released, holdTimer, &QTimer::stop);
            connect(holdTimer, &QTimer::timeout, this, [swatch, removeCustomColor]() {
                // A long press must not also activate the swatch when the
                // finger is released after closing the context menu.
                swatch->setDown(false);
                const bool signalsWereBlocked = swatch->blockSignals(true);
                QMenu menu;
                QAction *remove = menu.addAction(i18n("Remove custom color"));
                const bool shouldRemove =
                    menu.exec(swatch->mapToGlobal(swatch->rect().center())) == remove;
                swatch->blockSignals(signalsWereBlocked);
                if (shouldRemove) {
                    removeCustomColor();
                }
            });
        }
        return swatch;
    };

    for (const QColor &color : velyntoraColors) {
        quickColorsLayout->addWidget(makeColorButton(color, false));
    }

    // User colors are persistent. They are restored on every launch and can
    // be removed with the platform context-menu gesture (right click on
    // desktop, long-press where Qt exposes it on touch platforms).
    QSettings settings;
    QStringList savedCustomColors =
        settings.value(QStringLiteral("Velyntora/CustomQuickColors")).toStringList();
    constexpr int maxCustomQuickColors = 8;

    // Normalize old settings as well: discard invalid entries and duplicates
    // before applying the compact mobile limit.
    QStringList normalizedCustomColors;
    for (const QString &name : std::as_const(savedCustomColors)) {
        const QColor color(name);
        if (color.isValid()) {
            const QString normalizedName = color.name(QColor::HexArgb);
            if (!normalizedCustomColors.contains(normalizedName)) {
                normalizedCustomColors.append(normalizedName);
            }
        }
    }
    if (normalizedCustomColors.size() > maxCustomQuickColors) {
        normalizedCustomColors = normalizedCustomColors.mid(0, maxCustomQuickColors);
    }
    if (normalizedCustomColors != savedCustomColors) {
        settings.setValue(QStringLiteral("Velyntora/CustomQuickColors"), normalizedCustomColors);
    }
    savedCustomColors = normalizedCustomColors;
    for (const QString &name : std::as_const(savedCustomColors)) {
        const QColor color(name);
        if (color.isValid()) {
            quickColorsLayout->addWidget(makeColorButton(color, true));
        }
    }

    QToolButton *addQuickColor = new QToolButton(quickColors);
    addQuickColor->setObjectName("VelyntoraAddQuickColor");
    addQuickColor->setText(QStringLiteral("+"));
    addQuickColor->setFixedSize(22, 22);
    addQuickColor->setToolTip(i18n("Add a custom color"));
    addQuickColor->setAccessibleName(i18n("Add a custom quick color"));
    connect(addQuickColor, &QToolButton::clicked, this,
            [this, quickColorsLayout, addQuickColor, makeColorButton]() {
        const QColor initial = m_viewManager->canvasResourceProvider()->fgColor().toQColor();
        const QColor selected = QColorDialog::getColor(initial, m_viewManager->mainWindow(),
                                                       i18n("Add custom color"),
                                                       QColorDialog::ShowAlphaChannel);
        if (!selected.isValid()) {
            return;
        }

        const QString name = selected.name(QColor::HexArgb);
        QSettings settings;
        QStringList colors = settings.value(QStringLiteral("Velyntora/CustomQuickColors")).toStringList();

        // Keep the compact status bar bounded on phones/tablets.  A small
        // persistent custom palette is enough here; Krita's full Palette
        // docker remains available for larger collections.
        if (!colors.contains(name) && colors.size() < maxCustomQuickColors) {
            colors.append(name);
            settings.setValue(QStringLiteral("Velyntora/CustomQuickColors"), colors);
            const int addButtonIndex = quickColorsLayout->indexOf(addQuickColor);
            if (addButtonIndex >= 0) {
                quickColorsLayout->insertWidget(addButtonIndex, makeColorButton(selected, true));
            } else {
                quickColorsLayout->addWidget(makeColorButton(selected, true));
            }
        } else if (!colors.contains(name)) {
            QToolTip::showText(addQuickColor->mapToGlobal(addQuickColor->rect().center()),
                               i18n("Quick colors are full. Remove a custom color before adding another."),
                               addQuickColor);
        }

        m_viewManager->canvasResourceProvider()->setFGColor(
            KoColor(selected, KoColorSpaceRegistry::instance()->rgb8()));
    });
    quickColorsLayout->addWidget(addQuickColor);

    addStatusBarItem(quickColors);

    connect(m_selectionStatus, SIGNAL(clicked()), m_viewManager->selectionManager(), SLOT(slotToggleSelectionDecoration()));
    connect(m_viewManager->selectionManager(), SIGNAL(displaySelectionChanged()), SLOT(updateSelectionToolTip()));
    connect(m_viewManager->mainWindow(), SIGNAL(themeChanged()), this, SLOT(updateSelectionIcon()));

    addStatusBarItem(m_selectionStatus);
    m_selectionStatus->setVisible(false);

    m_statusBarStatusLabel = new KSqueezedTextLabel();
    m_statusBarStatusLabel->setObjectName("statsBarStatusLabel");
    m_statusBarStatusLabel->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    m_statusBarStatusLabel->setContentsMargins(5, 5, 5, 5);
    connect(KoToolManager::instance(), SIGNAL(changedStatusText(QString)),
            m_statusBarStatusLabel, SLOT(setText(QString)));
    addStatusBarItem(m_statusBarStatusLabel, 2);
    m_statusBarStatusLabel->setVisible(false);

    m_statusBarProfileLabel = new KSqueezedTextLabel();
    m_statusBarProfileLabel->setObjectName("statsBarProfileLabel");
    m_statusBarProfileLabel->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    m_statusBarProfileLabel->setContentsMargins(5, 5, 5, 5);
    addStatusBarItem(m_statusBarProfileLabel, 3);
    m_statusBarProfileLabel->setVisible(false);

    m_progress = new KisProgressWidget();
    m_progress->setObjectName("ProgressBar");
    addStatusBarItem(m_progress);
    m_progress->setVisible(false);
    connect(m_progress, SIGNAL(sigCancellationRequested()), this, SIGNAL(sigCancellationRequested()));

    m_progressUpdater.reset(new KisProgressUpdater(m_progress, m_progress->progressProxy()));
    m_progressUpdater->setAutoNestNames(true);

    m_extraWidgetsParent = new QFrame;
    m_extraWidgetsParent->setMinimumWidth(50);
    m_extraWidgetsParent->setObjectName("Extra Widgets Parent");
    m_extraWidgetsLayout = new QHBoxLayout;
    m_extraWidgetsLayout->setContentsMargins(0, 0, 0, 0);
    m_extraWidgetsLayout->setObjectName("Extra Widgets Layout");
    m_extraWidgetsParent->setLayout(m_extraWidgetsLayout);
    addStatusBarItem(m_extraWidgetsParent);

    m_memoryReportBox = new KisMemoryReportButton();
    m_memoryReportBox->setObjectName("memoryReportBox");
    m_memoryReportBox->setFlat(true);
    m_memoryReportBox->setContentsMargins(5, 5, 5, 5);
    m_memoryReportBox->setMinimumWidth(120);
    addStatusBarItem(m_memoryReportBox);
    m_memoryReportBox->setVisible(false);

    // Velyntora Z: show document dimensions directly in the compact bottom
    // bar, matching the Pinta-like reference without exposing memory details.
    m_velyntoraImageSizeLabel = new QLabel(m_statusBar);
    m_velyntoraImageSizeLabel->setObjectName("VelyntoraImageSize");
    m_velyntoraImageSizeLabel->setContentsMargins(6, 0, 6, 0);
    m_velyntoraImageSizeLabel->setToolTip(i18n("Canvas size"));
    m_velyntoraImageSizeLabel->setAccessibleName(i18n("Canvas size"));
    m_velyntoraImageSizeLabel->setAlignment(Qt::AlignCenter);
    m_velyntoraImageSizeLabel->setMinimumWidth(96);
    m_velyntoraImageSizeLabel->setMaximumWidth(150);
    m_velyntoraImageSizeLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_velyntoraImageSizeLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_velyntoraImageSizeLabel->setVisible(false);
    addStatusBarItem(m_velyntoraImageSizeLabel);

    // Live image-space cursor coordinates. This is driven by the actual
    // canvas widget, not placeholder text, and follows zoom/rotation through
    // Krita's coordinates converter.
    m_velyntoraCursorPositionLabel = new QLabel(m_statusBar);
    m_velyntoraCursorPositionLabel->setObjectName("VelyntoraCursorPosition");
    m_velyntoraCursorPositionLabel->setContentsMargins(6, 0, 6, 0);
    m_velyntoraCursorPositionLabel->setAlignment(Qt::AlignCenter);
    m_velyntoraCursorPositionLabel->setMinimumWidth(112);
    m_velyntoraCursorPositionLabel->setToolTip(i18n("Cursor position on canvas"));
    m_velyntoraCursorPositionLabel->setAccessibleName(i18n("Cursor coordinates"));
    m_velyntoraCursorPositionLabel->setVisible(false);
    addStatusBarItem(m_velyntoraCursorPositionLabel);

    QFrame *velyntoraStatusSeparator = new QFrame(m_statusBar);
    velyntoraStatusSeparator->setObjectName(QStringLiteral("VelyntoraStatusSeparator"));
    velyntoraStatusSeparator->setFrameShape(QFrame::VLine);
    velyntoraStatusSeparator->setFrameShadow(QFrame::Sunken);
    velyntoraStatusSeparator->setFixedHeight(20);
    velyntoraStatusSeparator->setVisible(false);
    addStatusBarItem(velyntoraStatusSeparator);

    // Separator visibility is synchronized in imageSizeChanged().

    connect(m_memoryReportBox, SIGNAL(clicked()), SLOT(showMemoryInfoToolTip()));

    connect(KisMemoryStatisticsServer::instance(),
            SIGNAL(sigUpdateMemoryStatistics()),
            SLOT(imageSizeChanged()));

    m_canvasAngleSelector = new KisAngleSelector;
    m_canvasAngleSelector->setRange(-360.00, 360.0);
    m_canvasAngleSelector->setIncreasingDirection(KisAngleGauge::IncreasingDirection_Clockwise);
    m_canvasAngleSelector->setFlipOptionsMode(KisAngleSelector::FlipOptionsMode_ContextMenu);
    m_canvasAngleSelector->useFlatSpinBox(true);
    addStatusBarItem(m_canvasAngleSelector);

    connect(m_canvasAngleSelector, SIGNAL(angleChanged(qreal)), SLOT(slotCanvasAngleSelectorAngleChanged(qreal)));
    m_canvasAngleSelector->setVisible(false);
    m_canvasAngleSelector->setToolTip(i18n("Canvas rotation"));
    m_canvasAngleSelector->setAccessibleName(i18n("Canvas rotation"));
    m_canvasAngleSelector->setMinimumWidth(76);
    m_canvasAngleSelector->setMaximumWidth(104);

    // Keep the compact drawing bar usable on Android and narrow windows.
    // Fixed swatches keep their touch targets while the palette itself yields
    // space before Krita's zoom/status controls are squeezed.
    quickColors->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    quickColors->setMaximumHeight(36);
    quickColors->setToolTip(i18n("Quick colors"));
    quickColors->setAccessibleName(i18n("Quick colors"));

    // Keep the compact strip visually quiet like Pinta: no extra container
    // background or frame, only the color controls themselves.
    quickColors->setAttribute(Qt::WA_StyledBackground, false);
    quickColors->setStyleSheet(QStringLiteral(
        "#VelyntoraQuickColors { background: transparent; border: none; }"
        "#VelyntoraAddQuickColor { font-weight: bold; padding: 0px; }"));
}

KisStatusBar::~KisStatusBar()
{
}

void KisStatusBar::setView(QPointer<KisView> imageView)
{
    if (m_imageView) {
        if (m_velyntoraCanvasWidget) {
            m_velyntoraCanvasWidget->removeEventFilter(this);
            m_velyntoraCanvasWidget.clear();
        }
        if (m_imageView->canvasBase()) {
            m_imageView->canvasBase()->canvasController()->proxyObject->disconnect(this);
        }
        m_imageView->disconnect(this);
        removeStatusBarItem(m_imageView->zoomManager()->zoomActionWidget());
        m_imageView = 0;
    }

    if (imageView) {
        m_imageView = imageView;
        // Rotation is part of the compact Velyntora Z bottom bar so the
        // current canvas angle stays directly accessible beside size/zoom.
        m_canvasAngleSelector->setVisible(true);
        if (KisCanvas2 *canvas = m_viewManager->canvasBase()) {
            m_velyntoraCanvasWidget = canvas->canvasWidget();
            if (m_velyntoraCanvasWidget) {
                m_velyntoraCanvasWidget->setMouseTracking(true);
                m_velyntoraCanvasWidget->installEventFilter(this);
            }
        }
        connect(m_imageView, SIGNAL(sigColorSpaceChanged(const KoColorSpace*)),
                this, SLOT(updateStatusBarProfileLabel()));
        connect(m_imageView, SIGNAL(sigProfileChanged(const KoColorProfile*)),
                this, SLOT(updateStatusBarProfileLabel()));
        connect(m_imageView, SIGNAL(sigSizeChanged(QPointF,QPointF)),
                this, SLOT(imageSizeChanged()));
        connect(m_imageView->canvasController()->proxyObject, &KoCanvasControllerProxyObject::documentRotationChanged,
                this, &KisStatusBar::slotCanvasRotationChanged);
        updateStatusBarProfileLabel();
        slotCanvasRotationChanged();
        addStatusBarItem(m_imageView->zoomManager()->zoomActionWidget());
    }
    else {
        m_canvasAngleSelector->setVisible(false);
        if (m_velyntoraCursorPositionLabel) {
            m_velyntoraCursorPositionLabel->clear();
            m_velyntoraCursorPositionLabel->setVisible(false);
        }
    }

    imageSizeChanged();
}

bool KisStatusBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_velyntoraCanvasWidget && m_velyntoraCursorPositionLabel && m_imageView) {
        QPointF widgetPos;
        bool hasPosition = false;

        if (event->type() == QEvent::MouseMove) {
            widgetPos = static_cast<QMouseEvent*>(event)->localPos();
            hasPosition = true;
        } else if (event->type() == QEvent::TabletMove) {
            widgetPos = static_cast<QTabletEvent*>(event)->posF();
            hasPosition = true;
        } else if (event->type() == QEvent::Leave) {
            m_velyntoraCursorPositionLabel->clear();
            m_velyntoraCursorPositionLabel->setVisible(false);
        }

        if (hasPosition) {
            if (KisCanvas2 *canvas = m_viewManager->canvasBase()) {
                const QPointF imagePos = canvas->coordinatesConverter()->widgetToImage(widgetPos);
                KisImageWSP image = m_imageView->image();
                if (image && imagePos.x() >= 0.0 && imagePos.y() >= 0.0 &&
                    imagePos.x() < image->width() && imagePos.y() < image->height()) {
                    m_velyntoraCursorPositionLabel->setText(
                        i18nc("@info:status cursor coordinates", "X: %1   Y: %2",
                              qFloor(imagePos.x()), qFloor(imagePos.y())));
                    m_velyntoraCursorPositionLabel->setVisible(true);
                } else {
                    m_velyntoraCursorPositionLabel->clear();
                    m_velyntoraCursorPositionLabel->setVisible(false);
                }
            }
        }
    }

    return QObject::eventFilter(watched, event);
}

void KisStatusBar::addStatusBarItem(QWidget *widget, int stretch, bool permanent)
{
    StatusBarItem sbItem(widget);
    if (permanent) {
        m_statusBar->addPermanentWidget(widget, stretch);
    }
    else {
        m_statusBar->addWidget(widget, stretch);
    }
    widget->setVisible(true);
    m_statusBarItems.append(sbItem);
}

void KisStatusBar::removeStatusBarItem(QWidget *widget)
{
    int i = 0;
    Q_FOREACH(const StatusBarItem& sbItem, m_statusBarItems) {
        if (sbItem.widget() == widget) {
            break;
        }
        i++;
    }

    if (i < m_statusBarItems.count()) {
        m_statusBar->removeWidget(m_statusBarItems[i].widget());
        m_statusBarItems.remove(i);
    }
}

void KisStatusBar::hideAllStatusBarItems()
{
    Q_FOREACH(const StatusBarItem& sbItem, m_statusBarItems) {
        sbItem.hide();
    }
}

void KisStatusBar::showAllStatusBarItems()
{
    Q_FOREACH(const StatusBarItem& sbItem, m_statusBarItems) {
        sbItem.show();
    }
}


void KisStatusBar::imageSizeChanged()
{
    updateMemoryStatus();

    QString sizeText;
    KisImageWSP image = m_imageView ? m_imageView->image() : 0;
    if (image) {
        qint32 w = image->width();
        qint32 h = image->height();
        sizeText = i18nc("@info:status width x height (file size)", "%1 &x %2 (%3)", w, h, m_shortMemoryTag);
    } else {
        sizeText = m_shortMemoryTag;
    }

    m_memoryReportBox->setIcon(m_memoryStatusIcon);
    m_memoryReportBox->setText(sizeText);
    m_memoryReportBox->setToolTip(m_longMemoryTag);

    if (m_velyntoraImageSizeLabel) {
        if (image) {
            m_velyntoraImageSizeLabel->setText(
                i18nc("@info:status canvas dimensions", "%1 × %2 px", image->width(), image->height()));
            m_velyntoraImageSizeLabel->setVisible(true);
            if (QWidget *separator = m_statusBar->findChild<QWidget*>(QStringLiteral("VelyntoraStatusSeparator"))) {
                separator->setVisible(true);
            }
        } else {
            m_velyntoraImageSizeLabel->clear();
            m_velyntoraImageSizeLabel->setVisible(false);
            if (QWidget *separator = m_statusBar->findChild<QWidget*>(QStringLiteral("VelyntoraStatusSeparator"))) {
                separator->setVisible(false);
            }
        }
    }
}

void KisStatusBar::updateVelyntoraActiveTool()
{
    if (!m_velyntoraActiveToolLabel) return;

    const QString activeId = KoToolManager::instance()->activeToolId();
    QString toolName;
    QString iconName;
    const QList<KoToolAction*> actions = KoToolManager::instance()->toolActionList();
    for (KoToolAction *action : actions) {
        if (action && action->id() == activeId) {
            toolName = action->iconText();
            if (toolName.isEmpty()) toolName = action->toolTip();
            iconName = action->iconName();
            break;
        }
    }

    if (toolName.isEmpty()) toolName = activeId;
    if (toolName.isEmpty()) toolName = i18n("None");
    m_velyntoraActiveToolLabel->setText(i18n("Tool: %1", toolName));
    m_velyntoraActiveToolLabel->setToolTip(toolName);
    if (!iconName.isEmpty()) {
        m_velyntoraActiveToolLabel->setPixmap(KisIconUtils::loadIcon(iconName).pixmap(16, 16));
        m_velyntoraActiveToolLabel->setText(i18n("  Tool: %1", toolName));
    } else {
        m_velyntoraActiveToolLabel->setPixmap(QPixmap());
    }
}

void KisStatusBar::updateSelectionIcon()
{
    QIcon icon;
    if (!m_viewManager->selectionManager()->displaySelection()) {
        icon = KisIconUtils::loadIcon("selection-mode_invisible");
    } else if (m_viewManager->selectionManager()->showSelectionAsMask()) {
        icon = KisIconUtils::loadIcon("selection-mode_mask");
    } else /* if (!m_view->selectionManager()->showSelectionAsMask()) */ {
        icon = KisIconUtils::loadIcon("selection-mode_ants");
    }
    m_selectionStatus->setIcon(icon);
}

namespace {
QVector<KisNodeSP> fetchLayersWithSlowThumbnailGeneration(KisImageSP image)
{
    QVector<KisNodeSP> result;

    KisLayerUtils::recursiveApplyNodes(image->root(),
    [&result] (KisNodeSP node) {
        if (node->preferredThumbnailBoundsMode() == KisThumbnailBoundsMode::Coarse) {
            result.append(node);
        }
    });

    return result;
}
}


void KisStatusBar::updateMemoryStatus()
{
    KisMemoryStatisticsServer::Statistics stats =
            KisMemoryStatisticsServer::instance()
            ->fetchMemoryStatistics(m_imageView ? m_imageView->image() : 0);
    const KFormat format;

    const QString imageStatsMsg =
            i18nc("tooltip on statusbar memory reporting button (image stats)",
                  "Image size:\t %1\n"
                  "  - layers:\t\t %2\n"
                  "  - projections:\t %3\n"
                  "  - instant preview:\t %4\n",
                  format.formatByteSize(stats.imageSize),
                  format.formatByteSize(stats.layersSize),
                  format.formatByteSize(stats.projectionsSize),
                  format.formatByteSize(stats.lodSize));

    const QString memoryStatsMsg =
            i18nc("tooltip on statusbar memory reporting button (total stats)",
                  "Memory used:\t %1 / %2\n"
                  "  image data:\t %3 / %4\n"
                  "  pool:\t\t %5 / %6\n"
                  "  undo data:\t %7\n"
                  "\n"
                  "Swap used:\t %8",
                  format.formatByteSize(stats.totalMemorySize),
                  format.formatByteSize(stats.totalMemoryLimit),

                  format.formatByteSize(stats.realMemorySize),
                  format.formatByteSize(stats.tilesHardLimit),

                  format.formatByteSize(stats.poolSize),
                  format.formatByteSize(stats.tilesPoolLimit),

                  format.formatByteSize(stats.historicalMemorySize),
                  format.formatByteSize(stats.swapSize));

    QString longStats = imageStatsMsg + "\n" + memoryStatsMsg;

    QString shortStats = format.formatByteSize(stats.imageSize);
    bool shouldUseWarningIcon = false;
    const qint64 warnLevel = stats.tilesHardLimit - stats.tilesHardLimit / 8;

    if (stats.imageSize > warnLevel ||
            stats.realMemorySize > warnLevel) {

        if (!m_memoryWarningLogged) {
            m_memoryWarningLogged = true;
            KisUsageLogger::log(QString("WARNING: %1 is running out of memory:%2\n").arg(m_imageView->document()->path()).arg(longStats));
        }

        shouldUseWarningIcon = true;
        QString suffix =
                i18nc("tooltip on statusbar memory reporting button",
                      "\n\nWARNING:\tOut of memory! Swapping has been started.\n"
                      "\t\tPlease configure more RAM for Krita in Settings dialog");
        longStats += suffix;


    }

    if (m_imageView) {
        const QVector<KisNodeSP> slowThumbnailNodes = fetchLayersWithSlowThumbnailGeneration(m_imageView->image());
        if (!slowThumbnailNodes.isEmpty()) {
            QString suffix =
                i18nc("tooltip on statusbar memory reporting button",
                      "\n\nWARNING:\tSome layers took too much time to calculate\n"
                      "\t\ttheir thumbnails. They were switched into low-quality\n"
                      "\t\tthumbnails mode. Try purging unused image data with\n"
                      "\t\tImage->Purge Unused Image Data action.\n"
                    "Slow layers: ");

            bool needsSeparator = false;
            Q_FOREACH(KisNodeSP node, slowThumbnailNodes) {
                if (needsSeparator) {
                    suffix += ", ";
                }
                suffix.append(QString("\"%1\"").arg(node->name()));
                needsSeparator = true;
            }
            longStats += suffix;
            shouldUseWarningIcon = true;
        }
    }

    m_shortMemoryTag = shortStats;
    m_longMemoryTag = longStats;

    QIcon icon;
    if (shouldUseWarningIcon) {
        icon = KisIconUtils::loadIcon("warning");
    }

    m_memoryStatusIcon = icon;

    m_memoryReportBox->setMaximumMemory(stats.totalMemoryLimit);
    m_memoryReportBox->setCurrentMemory(stats.totalMemorySize);
    m_memoryReportBox->setImageWeight(stats.imageSize);
}

void KisStatusBar::showMemoryInfoToolTip()
{
    QToolTip::showText(QCursor::pos(), m_memoryReportBox->toolTip(), m_memoryReportBox);
}

void KisStatusBar::slotCanvasAngleSelectorAngleChanged(qreal angle)
{
    KisCanvas2 *canvas = m_viewManager->canvasBase();
    if (!canvas) return;

    KisCanvasController *canvasController = dynamic_cast<KisCanvasController*>(canvas->canvasController());
    if (canvasController) {
        canvasController->rotateCanvas(angle - canvas->rotationAngle());
    }
}

void KisStatusBar::slotCanvasRotationChanged()
{
    KisCanvas2 *canvas = m_viewManager->canvasBase();
    if (!canvas) return;

    const qreal angleDiff = qAbs(m_canvasAngleSelector->angle()) -
                            qAbs(canvas->rotationAngle());

    // Only update the UI if the angle difference is big enough. This improves the performance.
    if (qAbs(angleDiff) >= 0.01) {
        KisSignalsBlocker l(m_canvasAngleSelector);
        m_canvasAngleSelector->setAngle(canvas->rotationAngle());
    }
}

void KisStatusBar::updateSelectionToolTip()
{
    updateSelectionIcon();

    KisSelectionSP selection = m_viewManager->selection();
    if (selection) {
        m_selectionStatus->setEnabled(true);

        QRect r = selection->selectedExactRect();

        QString displayMode =
                !m_viewManager->selectionManager()->displaySelection() ?
                    i18n("Hidden") :
                    (m_viewManager->selectionManager()->showSelectionAsMask() ?
                         i18n("Mask") : i18n("Ants"));

        m_selectionStatus->setToolTip(
                    i18n("Selection: x = %1 y = %2 width = %3 height = %4\n"
                         "Display Mode: %5",
                         r.x(), r.y(), r.width(), r.height(), displayMode));
    } else {
        m_selectionStatus->setEnabled(false);
        m_selectionStatus->setToolTip(i18n("No Selection"));
    }
}

void KisStatusBar::setSelection(KisImageWSP image)
{
    Q_UNUSED(image);
    updateSelectionToolTip();
}

void KisStatusBar::setProfile(KisImageWSP image)
{
    if (m_statusBarProfileLabel == 0) {
        return;
    }

    if (!image) return;
    if (image->profile() == 0) {
        m_statusBarProfileLabel->setText(i18n("No profile"));
    } else {
        m_statusBarProfileLabel->setText(i18nc("<color space> <image profile>", "%1  %2", image->colorSpace()->name(), image->profile()->name()));
    }

}

void KisStatusBar::setHelp(const QString &t)
{
    Q_UNUSED(t);
}

void KisStatusBar::updateStatusBarProfileLabel()
{
    if (!m_imageView) return;

    setProfile(m_imageView->image());
}

KoProgressUpdater *KisStatusBar::progressUpdater()
{
    return m_progressUpdater.data();
}

void KisStatusBar::addExtraWidget(QWidget *widget)
{
    m_extraWidgetsLayout->addWidget(widget);
}

void KisStatusBar::removeExtraWidget(QWidget *widget)
{
    m_extraWidgetsLayout->removeWidget(widget);
}

