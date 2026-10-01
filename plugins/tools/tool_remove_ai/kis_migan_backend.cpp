#include "kis_migan_backend.h"

#include <QFileInfo>
#include <QRect>

namespace {
QRect maskedBounds(const QImage &mask)
{
    if (mask.isNull()) return {};
    const QImage gray = mask.convertToFormat(QImage::Format_Grayscale8);
    int left = gray.width(), top = gray.height(), right = -1, bottom = -1;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            if (row[x] > 0) {
                left = qMin(left, x); top = qMin(top, y);
                right = qMax(right, x); bottom = qMax(bottom, y);
            }
        }
    }
    return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

QRect expandedSquare(const QRect &bounds, const QSize &limit)
{
    if (bounds.isEmpty() || limit.isEmpty()) return {};
    const int padding = qMax(32, qMax(bounds.width(), bounds.height()) / 4);
    QRect r = bounds.adjusted(-padding, -padding, padding, padding);
    const int side = qMin(qMax(r.width(), r.height()), qMin(limit.width(), limit.height()));
    const QPoint center = r.center();
    r = QRect(center.x() - side / 2, center.y() - side / 2, side, side);
    if (r.left() < 0) r.moveLeft(0);
    if (r.top() < 0) r.moveTop(0);
    if (r.right() >= limit.width()) r.moveRight(limit.width() - 1);
    if (r.bottom() >= limit.height()) r.moveBottom(limit.height() - 1);
    return r.intersected(QRect(QPoint(0, 0), limit));
}
}

QImage KisMiganBackend::normalizeMask(const QImage &mask, const QSize &size)
{
    if (mask.isNull() || size.isEmpty()) {
        return {};
    }

    QImage normalized = mask.convertToFormat(QImage::Format_Grayscale8)
                                .scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // MI-GAN contract used by Velyntora:
    // 0 = preserve source pixel, 255 = region requested for removal.
    return normalized;
}

bool KisMiganBackend::isAvailable() const
{
    // The Android model asset/runtime is connected in the next integration
    // stage. Returning false prevents accidental destructive fallback.
    return false;
}

KisRemoveAIBackend::Result KisMiganBackend::run(const Request &request)
{
    Result result;

    if (request.source.isNull() || request.mask.isNull()) {
        result.error = QStringLiteral("Remove requires an image and a mask.");
        return result;
    }

    if (request.modelSize <= 0) {
        result.error = QStringLiteral("Invalid Remove model size.");
        return result;
    }

    if (request.source.size() != request.mask.size()) {
        result.error = QStringLiteral("Remove image and mask sizes do not match.");
        return result;
    }

    const QRect maskBounds = maskedBounds(request.mask);
    if (maskBounds.isEmpty()) {
        result.error = QStringLiteral("Remove mask is empty.");
        return result;
    }

    // Process only the masked neighborhood. This is substantially cheaper on
    // Android than resizing the entire canvas and also preserves detail away
    // from the requested removal.
    const QRect roi = expandedSquare(maskBounds, request.source.size());
    if (roi.isEmpty()) {
        result.error = QStringLiteral("Could not calculate Remove region.");
        return result;
    }

    result.sourceRect = roi;

    const QSize inferenceSize(request.modelSize, request.modelSize);
    const QImage source512 = request.source.copy(roi)
                                           .convertToFormat(QImage::Format_RGBA8888)
                                           .scaled(inferenceSize, Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
    const QImage mask512 = normalizeMask(request.mask.copy(roi), inferenceSize);

    if (source512.isNull() || mask512.isNull()) {
        result.error = QStringLiteral("Could not prepare Remove inference input.");
        return result;
    }

    if (!isAvailable()) {
        result.error = QStringLiteral("MI-GAN runtime is not installed yet.");
        return result;
    }

    // Runtime hook: feed source512 + mask512 into the packaged MI-GAN model.
    // When inference succeeds it must return RGBA at inferenceSize.
    // Never fabricate output while the runtime is unavailable.
    result.error = QStringLiteral("MI-GAN inference hook is not connected yet.");
    return result;
}
