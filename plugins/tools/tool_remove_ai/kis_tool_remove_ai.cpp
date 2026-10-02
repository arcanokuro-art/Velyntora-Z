#include "kis_tool_remove_ai.h"
#include "kis_migan_backend.h"

#include <KoColorSpaceRegistry.h>
#include <kis_canvas2.h>
#include <kis_painter.h>
#include <kis_paint_device.h>
#include <kis_algebra_2d.h>
#include <kis_paint_layer.h>
#include <KisViewManager.h>
#include <kis_transaction.h>
#include <kis_image.h>

struct KisToolRemoveAI::Private {
    KisPaintDeviceSP mask;
    KisPainter painter;
    qreal radius = 40.0;
    QPainterPath outline;
    KisMiganBackend backend;
};

KisToolRemoveAI::KisToolRemoveAI(KoCanvasBase *canvas)
    : KisToolPaint(canvas, Qt::CrossCursor), m_d(new Private)
{
    setObjectName("tool_VelyntoraRemoveAI");
    m_d->mask = new KisPaintDevice(KoColorSpaceRegistry::instance()->rgb8());
    m_d->painter.begin(m_d->mask);
    m_d->painter.setPaintColor(KoColor(Qt::white, m_d->mask->colorSpace()));
    m_d->painter.setFillStyle(KisPainter::FillStyleForegroundColor);
    setSupportOutline(true);
}

KisToolRemoveAI::~KisToolRemoveAI()
{
    m_d->painter.end();
}

void KisToolRemoveAI::deactivate()
{
    if (m_d->mask) {
        m_d->mask->clear();
    }
    m_d->outline = QPainterPath();
    setMode(KisTool::HOVER_MODE);
    KisToolPaint::deactivate();
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
        if (KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2 *>(canvas())) {
            kritaCanvas->viewManager()->showFloatingMessage(
                i18n("Select a paint layer to use Remove"), QIcon(), 2000);
        }
        event->ignore();
        return;
    }

    // Every new gesture starts a fresh inference mask. The source pixels are
    // not modified while the user is only marking an object for removal.
    m_d->mask->clear();
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

    // Do not modify the paint layer until the native runtime is actually
    // available. This turns the previous comment-only integration boundary
    // into a real backend gate and keeps failed Remove attempts non-destructive.
    if (!m_d->backend.isAvailable()) {
        if (KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2 *>(canvas())) {
            kritaCanvas->viewManager()->showFloatingMessage(
                i18n("Remove AI runtime is not available yet"), QIcon(), 2500);
        }
        return;
    }

    KisPaintLayer *paintLayer = qobject_cast<KisPaintLayer *>(currentNode().data());
    KisImageSP current = currentImage();
    if (!paintLayer || !current) return;

    KisPaintDeviceSP device = paintLayer->paintDevice();
    if (!device) return;

    const QRect imageRect = current->bounds();
    KisRemoveAIBackend::Request request;
    request.source = device->convertToQImage(nullptr, imageRect);
    request.mask = m_d->mask->convertToQImage(nullptr, imageRect);
    if (request.source.isNull() || request.mask.isNull() ||
        request.source.size() != request.mask.size()) {
        if (KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2 *>(canvas())) {
            kritaCanvas->viewManager()->showFloatingMessage(
                i18n("Remove could not snapshot the active layer"), QIcon(), 3000);
        }
        return;
    }
    request.modelSize = 512;
    request.allowUpscale = true;
    request.modelHandlesPipeline = true;

    const KisRemoveAIBackend::Result result = m_d->backend.run(request);
    if (!result.isReadyForCommit()) {
        if (KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2 *>(canvas())) {
            kritaCanvas->viewManager()->showFloatingMessage(
                result.commitValidationError(), QIcon(), 3000);
        }
        return;
    }

    const QImage patch = result.writeBackImage();
    if (patch.isNull()) return;

    KisTransaction transaction(kundo2_i18n("Remove"), device);
    device->convertFromQImage(patch, nullptr,
                              result.documentWriteBackOrigin().x(),
                              result.documentWriteBackOrigin().y());
    paintLayer->setDirty(result.writeBackRect());
    transaction.commit(current->undoAdapter());
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
