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

QImage KisMiganBackend::compositeMaskedPatch(const QImage &sourcePatch,
                                                   const QImage &generatedPatch,
                                                   const QImage &removeMask)
{
    if (sourcePatch.isNull() || generatedPatch.isNull() || removeMask.isNull() ||
        sourcePatch.size() != generatedPatch.size() ||
        sourcePatch.size() != removeMask.size()) {
        return {};
    }

    QImage output = sourcePatch.convertToFormat(QImage::Format_RGBA8888);
    const QImage generated = generatedPatch.convertToFormat(QImage::Format_RGBA8888);
    const QImage mask = removeMask.convertToFormat(QImage::Format_Grayscale8);

    for (int y = 0; y < output.height(); ++y) {
        uchar *dst = output.scanLine(y);
        const uchar *src = generated.constScanLine(y);
        const uchar *m = mask.constScanLine(y);
        for (int x = 0; x < output.width(); ++x) {
            if (m[x] >= 128) {
                const int i = x * 4;
                dst[i] = src[i];
                dst[i + 1] = src[i + 1];
                dst[i + 2] = src[i + 2];
                dst[i + 3] = src[i + 3];
            }
        }
    }
    return output;
}

QImage KisMiganBackend::normalizeMask(const QImage &mask, const QSize &size)
{
    if (mask.isNull() || size.isEmpty()) {
        return {};
    }

    // Resize the binary remove mask with nearest-neighbour semantics.
    // Smooth interpolation would create gray pixels around the selection and
    // subtly change the area that MI-GAN is allowed to reconstruct.
    QImage normalized = mask.convertToFormat(QImage::Format_Grayscale8)
                                .scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    // Velyntora paints 255 where the user wants removal. The official
    // MI-GAN ONNX pipeline expects the inverse: 255 = known/preserve and
    // 0 = masked/inpaint. Keep the tool convention and invert only here.
    for (int y = 0; y < normalized.height(); ++y) {
        uchar *row = normalized.scanLine(y);
        for (int x = 0; x < normalized.width(); ++x) {
            row[x] = row[x] >= 128 ? 0 : 255;
        }
    }
    return normalized;
}

bool KisMiganBackend::validateRuntimeOutput(const QImage &image, const QSize &expectedSize, QString *error)
{
    if (image.isNull()) {
        if (error) *error = QStringLiteral("MI-GAN returned an empty image.");
        return false;
    }
    if (image.size() != expectedSize) {
        if (error) *error = QStringLiteral("MI-GAN returned an unexpected image size.");
        return false;
    }
    if (image.format() != QImage::Format_RGBA8888 &&
        image.format() != QImage::Format_ARGB32 &&
        image.format() != QImage::Format_ARGB32_Premultiplied) {
        if (error) *error = QStringLiteral("MI-GAN returned an unsupported pixel format.");
        return false;
    }
    return true;
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

    // The official MI-GAN ONNX pipeline accepts arbitrary-resolution uint8
    // RGB image + binary uint8 mask and performs crop/resize/normalization,
    // inference, resize-back and blending itself. Prefer that path to avoid
    // applying those transforms twice.
    if (request.modelHandlesPipeline) {
        result.inferenceSize = request.source.size();
        if (!isAvailable()) {
            result.error = QStringLiteral("MI-GAN ONNX pipeline runtime is not installed yet.");
            return result;
        }

        // Runtime hook receives the original-resolution source and the
        // normalized/inverted binary mask. No synthetic success is returned
        // until ONNX Runtime is actually connected.
        const QImage pipelineMask = normalizeMask(request.mask, request.mask.size());
        Q_UNUSED(pipelineMask);
        result.error = QStringLiteral("MI-GAN ONNX pipeline hook is not connected yet.");
        return result;
    }

    const int targetSide = request.allowUpscale
        ? request.modelSize
        : qMin(request.modelSize, qMax(roi.width(), roi.height()));
    if (targetSide <= 0) {
        result.error = QStringLiteral("Invalid Remove inference dimensions.");
        return result;
    }
    const QSize inferenceSize(targetSide, targetSide);
    result.inferenceSize = inferenceSize;

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
    Q_UNUSED(source512);
    Q_UNUSED(mask512);
    result.error = QStringLiteral("MI-GAN inference hook is not connected yet.");
    return result;
}
