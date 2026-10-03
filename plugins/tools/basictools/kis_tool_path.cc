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
#include <KoPointerEvent.h>
#include <QPainterPath>


KisToolPath::KisToolPath(KoCanvasBase * canvas)
    : DelegatedPathTool(canvas, Qt::ArrowCursor,
                        new __KisToolPathLocalTool(canvas, this))
{
    setIsOpacityPresetMode(true);
    KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2*>(canvas);

    // The factory normally receives a KisCanvas2, but keep activation safe if
    // a non-Krita canvas is ever supplied (tests/embedded views). The previous
    // unconditional dereference could crash before the tool was usable.
    if (kritaCanvas && kritaCanvas->viewManager() && kritaCanvas->viewManager()->canvasResourceProvider()) {
        connect(kritaCanvas->viewManager()->canvasResourceProvider(),
                SIGNAL(sigEffectiveCompositeOpChanged()),
                SLOT(resetCursorStyle()));
    }

}

void KisToolPath::paint(QPainter &painter, const KoViewConverter &converter)
{
    Q_UNUSED(converter);

    // The pending Line/Curve segment is a decoration only. It is deliberately
    // kept out of the paint layer until the user finishes shaping it, so the
    // same line can be bent without erasing/repainting raster pixels.
    if (m_lineCurveState != LineCurveState::Idle && m_lineCurveStart != m_lineCurveEnd) {
        QPainterPath preview;
        const QPointF start = pixelToView(m_lineCurveStart);
        const QPointF end = pixelToView(m_lineCurveEnd);
        preview.moveTo(start);
        if (m_lineCurveState == LineCurveState::Curving) {
            const QPointF control = pixelToView(m_lineCurveControl);
            preview.quadTo(control, end);
        } else {
            preview.lineTo(end);
        }
        paintToolOutline(&painter, preview);
    }

    DelegatedPathTool::paint(painter, converter);
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

void KisToolPath::clearLineCurvePreview()
{
    clearLineCurvePreview();
}

void KisToolPath::requestStrokeEnd()
{
    // KoCreatePathTool keeps a provisional trailing point while a path is
    // live. Line/Curve owns only the two anchors already drawn, so a forced
    // stroke end (tool switch, canvas state change, etc.) must discard that
    // provisional point exactly like the normal primary-release path does.
    // Calling endPath() here can otherwise hand addPathShape() three points,
    // which it correctly rejects and makes the visible segment disappear.
    if (!localTool()->pathStarted()) {
        return;
    }

    if (!nodeEditable()) {
        localTool()->cancelPath();
        clearLineCurvePreview();
        return;
    }

    localTool()->endPathWithoutLastPoint();
    clearLineCurvePreview();
}

void KisToolPath::requestStrokeCancellation()
{
    // Cancellation must clear both KoCreatePathTool's delegated path and our
    // independent preview state. Otherwise Escape/right-click/layer changes
    // can leave a ghost Line/Curve decoration on the canvas.
    if (localTool()->pathStarted()) {
        localTool()->cancelPath();
    }

    clearLineCurvePreview();
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
    // Android may synthesize a mouse press after a touch press; consume this
    // raw entry point so one physical gesture still creates exactly one segment.
    Q_UNUSED(event);
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

    // If the target becomes locked/non-editable before a queued input event
    // is delivered, tear down the live segment immediately. This prevents
    // right-click/tablet events from operating on stale path state.
    if (!nodeEditable()) {
        localTool()->cancelPath();
        clearLineCurvePreview();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            // Line/Curve owns only one two-anchor segment. KoCreatePathTool's
            // removeLastPoint() intentionally does nothing at this size, so a
            // secondary click must cancel the whole live segment instead.
            localTool()->cancelPath();
            clearLineCurvePreview();
            return true;
        }
    } else if (event->type() == QEvent::TabletPress) {
        QTabletEvent *tabletEvent = static_cast<QTabletEvent*>(event);
        if (tabletEvent->button() == Qt::RightButton) {
            localTool()->cancelPath();
            clearLineCurvePreview();
            return true;
        }
    }
    return false;
}

void KisToolPath::beginAlternateAction(KoPointerEvent *event, AlternateAction action) {
    // Do not let a secondary mouse/stylus/touch action re-enter the inherited
    // multi-point path state while a Line/Curve segment is being created.
    if (localTool()->pathStarted()) {
        event->accept();
        return;
    }

    DelegatedPathTool::beginAlternateAction(event, action);
    if (!nodeEditable()) return;

    if (nodePaintAbility() == KisToolPath::MYPAINTBRUSH_UNPAINTABLE) {
        QString message = i18n("The MyPaint Brush Engine is not available for this colorspace");
        if (KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2*>(canvas())) {
            if (kritaCanvas->viewManager()) {
                kritaCanvas->viewManager()->showFloatingMessage(message, koIcon("object-locked"));
            }
        }
        event->ignore();
        return;
    }
}

void KisToolPath::continueAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (localTool()->pathStarted()) {
        event->accept();
        return;
    }

    DelegatedPathTool::continueAlternateAction(event, action);
}

void KisToolPath::endAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (localTool()->pathStarted()) {
        event->accept();
        return;
    }

    DelegatedPathTool::endAlternateAction(event, action);
}

void KisToolPath::beginPrimaryAction(KoPointerEvent* event)
{
    if (!nodeEditable()) {
        return;
    }

    // Once the straight segment exists, the next primary drag belongs to the
    // curvature phase. Do not feed that press back into KoCreatePathTool: its
    // legacy multi-click path logic would cancel/append anchors instead of
    // bending the pending two-anchor segment.
    if (m_lineCurveState == LineCurveState::AwaitingCurve) {
        m_lineCurveControl = convertToPixelCoordAndSnap(event);
        m_lineCurveState = LineCurveState::Curving;
        return;
    }

    // A fresh Line/Curve gesture is strictly single-segment. If a stale path
    // somehow survived outside the pending-curvature state, cancel it rather
    // than recreating the old multi-click Bezier workflow.
    if (localTool()->pathStarted()) {
        localTool()->cancelPath();
    }

    m_lineCurveStart = convertToPixelCoordAndSnap(event);
    m_lineCurveEnd = m_lineCurveStart;
    m_lineCurveControl = QPointF();
    m_lineCurveState = LineCurveState::DrawingStraight;

    DelegatedPathTool::mousePressEvent(event);
}

void KisToolPath::continuePrimaryAction(KoPointerEvent *event)
{
    if (m_lineCurveState == LineCurveState::Curving) {
        if (!nodeEditable()) {
            localTool()->cancelPath();
            clearLineCurvePreview();
            return;
        }
        m_lineCurveControl = convertToPixelCoordAndSnap(event);
        canvas()->updateCanvas(QRectF());
        return;
    }

    if (!localTool()->pathStarted()) {
        return;
    }

    // If editability changes mid-gesture, cancel immediately instead of
    // leaving a hidden delegated path alive until release.
    if (!nodeEditable()) {
        localTool()->cancelPath();
        clearLineCurvePreview();
        return;
    }

    // Only update the live segment while an actual Line/Curve gesture is
    // active. This prevents hover/synthesized touch moves from feeding the
    // legacy path state machine after the segment has already been committed.
    if (m_lineCurveState == LineCurveState::DrawingStraight) {
        m_lineCurveEnd = convertToPixelCoordAndSnap(event);
        canvas()->updateCanvas(QRectF());
    }
    mouseMoveEvent(event);
}

void KisToolPath::endPrimaryAction(KoPointerEvent *event)
{
    if (!localTool()->pathStarted()) {
        return;
    }

    // The curvature drag is our own lightweight interaction; it was never
    // forwarded to KoCreatePathTool, so its release must not be forwarded
    // either. Commit the still-live two-anchor delegated shape exactly once.
    if (m_lineCurveState == LineCurveState::Curving) {
        if (!nodeEditable()) {
            localTool()->cancelPath();
            clearLineCurvePreview();
            return;
        }
        m_lineCurveControl = convertToPixelCoordAndSnap(event);
        localTool()->endPathWithoutLastPoint();
        clearLineCurvePreview();
        return;
    }

    // If the target became non-editable while dragging (layer lock, node
    // switch, etc.), cancel rather than committing into an invalid target.
    if (!nodeEditable()) {
        localTool()->cancelPath();
        clearLineCurvePreview();
        return;
    }

    mouseReleaseEvent(event);

    // The release handler may already have committed/cancelled the delegated
    // path. Re-check both state and editability before the final commit so a
    // release-triggered node/layer change cannot commit into a stale target.
    if (!localTool()->pathStarted()) {
        return;
    }

    if (!nodeEditable()) {
        localTool()->cancelPath();
        clearLineCurvePreview();
        return;
    }

    // The first release finishes only the straight construction gesture.
    // Keep the delegated shape alive and retain our lightweight decoration so
    // the same segment can be bent before any pixels are rasterized.
    if (m_lineCurveState == LineCurveState::DrawingStraight) {
        if (m_lineCurveStart == m_lineCurveEnd) {
            localTool()->cancelPath();
            clearLineCurvePreview();
            return;
        }
        m_lineCurveState = LineCurveState::AwaitingCurve;
        canvas()->updateCanvas(QRectF());
        return;
    }

    // A later explicit finalization discards KoCreatePathTool's provisional
    // trailing point and commits exactly the two Line/Curve anchors.
    localTool()->endPathWithoutLastPoint();
    clearLineCurvePreview();
}

void KisToolPath::deactivate()
{
    if (localTool()->pathStarted()) {
        localTool()->cancelPath();
    }
    clearLineCurvePreview();

    DelegatedPathTool::deactivate();
}

void KisToolPath::beginPrimaryDoubleClickAction(KoPointerEvent *event)
{
    // Double-click only finalizes an already active Line/Curve gesture.
    // Starting a fresh press/release pair here can create a zero-length
    // segment on touch devices where a double-click follows the normal tap.
    Q_UNUSED(event);
    if (!localTool()->pathStarted()) {
        return;
    }

    if (!nodeEditable()) {
        localTool()->cancelPath();
        clearLineCurvePreview();
        return;
    }

    // Keep double-click finalization identical to normal/forced completion:
    // KoCreatePathTool owns a provisional trailing point while the segment is
    // live, and Line/Curve must not commit that legacy Bezier point.
    localTool()->endPathWithoutLastPoint();
    clearLineCurvePreview();
}

QList<QPointer<QWidget> > KisToolPath::createOptionWidgets()
{
    QList<QPointer<QWidget> > widgets = DelegatedPathTool::createOptionWidgets();

    // Line/Curve intentionally has no exposed Bezier-node editing stage.
    // Keep the inherited shape/brush options, but make the simplified
    // interaction explicit to assistive UI and future maintainers.
    for (const QPointer<QWidget> &widget : widgets) {
        if (!widget) {
            continue;
        }

        widget->setProperty("velyntoraLineCurve", true);
        widget->setAccessibleDescription(
            i18n("Options for the simplified Line/Curve tool. Curvature is handled without exposed Bezier handles."));

        // Autosmooth belongs to KoCreatePathTool's legacy multi-point Bezier
        // workflow. Line/Curve creates one two-anchor segment, so exposing
        // this checkbox is misleading and has no useful effect here. Keep
        // angle snapping and the remaining shape options available.
        if (QWidget *autoSmooth = widget->findChild<QWidget*>(QStringLiteral("smooth-curves-widget"))) {
            autoSmooth->hide();
        }
    }

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
    // Velyntora Line/Curve owns exactly one two-anchor segment.
    KoPathPoint *start = pathShape->pointByIndex(KoPathPointIndex(0, 0));
    KoPathPoint *end = pathShape->pointByIndex(KoPathPointIndex(0, 1));
    KoPathPoint *extra = pathShape->pointByIndex(KoPathPointIndex(0, 2));

    if (!start || !end || extra) {
        delete pathShape;
        return;
    }

    const QPointF delta = end->point() - start->point();
    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y())) {
        delete pathShape;
        return;
    }

    // Store the same curve shown by the simplified preview. A quadratic
    // segment P0-Q-P2 is represented exactly as a cubic with:
    // C1=P0+2/3(Q-P0), C2=P2+2/3(Q-P2). When no curvature gesture was used,
    // the midpoint produces the original straight cubic segment.
    start->removeControlPoint1();
    end->removeControlPoint2();
    start->unsetProperty(KoPathPoint::IsSmooth);
    start->unsetProperty(KoPathPoint::IsSymmetric);
    end->unsetProperty(KoPathPoint::IsSmooth);
    end->unsetProperty(KoPathPoint::IsSymmetric);

    QPointF quadraticControl = (start->point() + end->point()) / 2.0;
    if (m_parentTool->m_lineCurveState == KisToolPath::LineCurveState::Curving) {
        // The preview state is stored in image coordinates while the delegated
        // KoPathShape uses document coordinates. Reuse the canvas converter so
        // the committed cubic follows the on-canvas preview at any zoom/resolution.
        KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2*>(m_parentTool->canvas());
        if (kritaCanvas) {
            quadraticControl = kritaCanvas->coordinatesConverter()->imageToDocument(
                m_parentTool->m_lineCurveControl);
        }
    }
    start->setControlPoint2(start->point() + (quadraticControl - start->point()) * (2.0 / 3.0));
    end->setControlPoint1(end->point() + (quadraticControl - end->point()) * (2.0 / 3.0));
    pathShape->normalize();

    // Never merge into KoCreatePathTool's legacy multi-point path workflow.
    m_parentTool->addPathShape(pathShape, kundo2_i18n("Draw Line/Curve"));
}
