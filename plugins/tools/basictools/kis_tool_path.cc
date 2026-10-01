/*
 *  SPDX-FileCopyrightText: 2007 Sven Langkamp <sven.langkamp@gmail.com>
 *  SPDX-FileCopyrightText: 2010 Cyrille Berger <cberger@cberger.net>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_tool_path.h"
#include <KoPathShape.h>
#include <KoPathPoint.h>
#include <KoCanvasBase.h>
#include <kis_cursor.h>
#include <KisViewManager.h>
#include <canvas/kis_canvas2.h>
#include <kis_canvas_resource_provider.h>


KisToolPath::KisToolPath(KoCanvasBase * canvas)
    : DelegatedPathTool(canvas, Qt::ArrowCursor,
                        new __KisToolPathLocalTool(canvas, this))
{
    setIsOpacityPresetMode(true);
    KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2*>(canvas);

    connect(kritaCanvas->viewManager()->canvasResourceProvider(), SIGNAL(sigEffectiveCompositeOpChanged()), SLOT(resetCursorStyle()));

}

void KisToolPath::resetCursorStyle()
{
    if (isEraser() && (nodePaintAbility() == PAINT)) {
        useCursor(KisCursor::eraserCursor());
    } else {
        DelegatedPathTool::resetCursorStyle();
    }

    overrideCursorIfNotEditable();
}

void KisToolPath::requestStrokeEnd()
{
    // Line/Curve commits the currently previewed segment.  Do not use
    // endPathWithoutLastPoint(): that behavior belongs to the legacy
    // multi-click Bezier workflow and can discard the line endpoint.
    if (localTool()->pathStarted()) {
        localTool()->endPath();
    }
}

void KisToolPath::requestStrokeCancellation()
{
    localTool()->cancelPath();
}

KisPopupWidgetInterface* KisToolPath::popupWidget()
{
    return localTool()->pathStarted() ? nullptr : DelegatedPathTool::popupWidget();
}

void KisToolPath::mousePressEvent(KoPointerEvent *event)
{
    // Primary input is handled by beginPrimaryAction(). Keeping the old
    // KoCreatePathTool mouse-press path disabled prevents accidental
    // re-entry into multi-click Bezier construction.
    Q_UNUSED(event)
}

// Install an event filter to catch right-click events.
// The simplest way to accommodate the popup palette binding.
// This code is duplicated in kis_tool_select_path.cc
bool KisToolPath::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);
    if (!localTool()->pathStarted()) {
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            localTool()->removeLastPoint();
            return true;
        }
    } else if (event->type() == QEvent::TabletPress) {
        QTabletEvent *tabletEvent = static_cast<QTabletEvent*>(event);
        if (tabletEvent->button() == Qt::RightButton) {
            localTool()->removeLastPoint();
            return true;
        }
    }
    return false;
}

void KisToolPath::beginAlternateAction(KoPointerEvent *event, AlternateAction action) {
    DelegatedPathTool::beginAlternateAction(event, action);
    if (!nodeEditable()) return;

    if (nodePaintAbility() == KisToolPath::MYPAINTBRUSH_UNPAINTABLE) {
        KisCanvas2 * kiscanvas = static_cast<KisCanvas2*>(canvas());
        QString message = i18n("The MyPaint Brush Engine is not available for this colorspace");
        kiscanvas->viewManager()->showFloatingMessage(message, koIcon("object-locked"));
        event->ignore();
        return;
    }
}

void KisToolPath::beginPrimaryAction(KoPointerEvent* event)
{
    if (!nodeEditable()) return;
    DelegatedPathTool::mousePressEvent(event);
}

void KisToolPath::continuePrimaryAction(KoPointerEvent *event)
{
    mouseMoveEvent(event);
}

void KisToolPath::endPrimaryAction(KoPointerEvent *event)
{
    mouseReleaseEvent(event);

    // Velyntora Line/Curve is line-first: a normal drag creates one
    // editable segment instead of leaving the old multi-click Bezier
    // construction active. Curve adjustment is handled as a follow-up
    // editing step.
    if (localTool()->pathStarted()) {
        localTool()->endPath();
    }
}

void KisToolPath::beginPrimaryDoubleClickAction(KoPointerEvent *event)
{
    // Double-click only finalizes an already active Line/Curve gesture.
    // Starting a fresh press/release pair here can create a zero-length
    // segment on touch devices where a double-click follows the normal tap.
    Q_UNUSED(event);
    if (localTool()->pathStarted()) {
        localTool()->endPath();
    }
}

QList<QPointer<QWidget> > KisToolPath::createOptionWidgets()
{
    QList<QPointer<QWidget> > widgets = DelegatedPathTool::createOptionWidgets();
    return widgets;
}


__KisToolPathLocalTool::__KisToolPathLocalTool(KoCanvasBase * canvas, KisToolPath* parentTool)
    : KoCreatePathTool(canvas)
    , m_parentTool(parentTool) {
    setIsOpacityPresetMode(true);
}

void __KisToolPathLocalTool::paintPath(KoPathShape &pathShape, QPainter &painter, const KoViewConverter &converter)
{
    Q_UNUSED(converter);

    QTransform matrix;
    matrix.scale(m_parentTool->image()->xRes(), m_parentTool->image()->yRes());
    matrix.translate(pathShape.position().x(), pathShape.position().y());
    m_parentTool->paintToolOutline(&painter, m_parentTool->pixelToView(matrix.map(pathShape.outline())));
}

void __KisToolPathLocalTool::addPathShape(KoPathShape* pathShape)
{
    // Velyntora Line/Curve keeps a simple two-anchor interaction.  Convert
    // that segment to a smooth cubic internally, so the user never has to
    // manipulate traditional Bezier handles.
    KoPathPoint *start = pathShape->pointByIndex(KoPathPointIndex(0, 0));
    KoPathPoint *end = pathShape->pointByIndex(KoPathPointIndex(0, 1));

    // Line/Curve owns exactly one segment. If an unexpected extra point is
    // present (for example from a synthesized touch/mouse event), do not
    // reinterpret a legacy multi-point path as our simple curve.
    KoPathPoint *extra = pathShape->pointByIndex(KoPathPointIndex(0, 2));
    if (start && end && !extra) {
        const QPointF delta = end->point() - start->point();

        // Keep the initial result visually identical to a straight line.
        // The automatically generated cubic handles are collinear and placed
        // at one third/two thirds of the segment. This gives Line/Curve a
        // curve-ready representation without changing what the user drew.
        if (!qFuzzyIsNull(delta.x()) || !qFuzzyIsNull(delta.y())) {
            // An open two-anchor curve must not carry the opposite endpoint
            // handles left over from any legacy path-tool state.
            start->removeControlPoint1();
            end->removeControlPoint2();
            start->setControlPoint2(start->point() + delta / 3.0);
            end->setControlPoint1(end->point() - delta / 3.0);

            // Endpoints only own one active handle on an open Line/Curve.
            // Do not mark them IsSmooth: that flag describes a join with
            // incoming and outgoing tangents and is misleading at endpoints.
            pathShape->normalize();
        }
    }

    if (!KoCreatePathTool::tryMergeInPathShape(pathShape)) {
        m_parentTool->addPathShape(pathShape, kundo2_i18n("Draw Line/Curve"));
    }
}
