#include "kis_tool_remove_ai.h"

#include <KoColorSpaceRegistry.h>
#include <kis_canvas2.h>
#include <kis_painter.h>
#include <kis_paint_device.h>
#include <kis_algebra_2d.h>

struct KisToolRemoveAI::Private {
    KisPaintDeviceSP mask;
    KisPainter painter;
    qreal radius = 40.0;
    QPainterPath outline;
};

KisToolRemoveAI::KisToolRemoveAI(KoCanvasBase *canvas)
    : KisToolPaint(canvas, Qt::CrossCursor), m_d(new Private)
{
    setObjectName("tool_VelyntoraRemoveAI");
    m_d->mask = new KisPaintDevice(KoColorSpaceRegistry::instance()->rgb8());
    m_d->painter.begin(m_d->mask);
    m_d->painter.setPaintColor(KoColor(Qt::white, m_d->mask->colorSpace()));
    m_d->painter.setFillStyle(KisPainter::FillStyleForegroundColor);
}

KisToolRemoveAI::~KisToolRemoveAI()
{
    m_d->painter.end();
}

void KisToolRemoveAI::addMaskPoint(KoPointerEvent *event)
{
    const QPointF p = currentImage()->documentToPixel(event->point);
    QPainterPath stamp;
    stamp.addEllipse(p, m_d->radius, m_d->radius);
    m_d->painter.fillPainterPath(stamp);
    m_d->outline = stamp;
    canvas()->updateCanvas(currentImage()->pixelToDocument(stamp.boundingRect()));
}

void KisToolRemoveAI::beginPrimaryAction(KoPointerEvent *event)
{
    if (currentNode().isNull() || !currentNode()->inherits("KisPaintLayer") ||
        nodePaintAbility() != NodePaintAbility::PAINT) {
        event->ignore();
        return;
    }
    addMaskPoint(event);
    setMode(KisTool::PAINT_MODE);
}

void KisToolRemoveAI::continuePrimaryAction(KoPointerEvent *event)
{
    CHECK_MODE_SANITY_OR_RETURN(KisTool::PAINT_MODE);
    addMaskPoint(event);
}

void KisToolRemoveAI::endPrimaryAction(KoPointerEvent *event)
{
    CHECK_MODE_SANITY_OR_RETURN(KisTool::PAINT_MODE);
    addMaskPoint(event);
    setMode(KisTool::HOVER_MODE);

    // Integration boundary: the image + this binary mask will be passed to
    // the local inpainting backend (MI-GAN 512). Keep the mask alive until
    // inference succeeds so a failed/cancelled run cannot damage the layer.
}

void KisToolRemoveAI::paint(QPainter &painter, const KoViewConverter &converter)
{
    Q_UNUSED(converter);
    painter.save();
    painter.setBrush(QColor(255, 0, 255, 96));
    painter.setPen(Qt::NoPen);
    painter.drawPath(pixelToView(m_d->outline));
    painter.restore();
}
