#include "kis_migan_backend.h"

#include <QFileInfo>
#include <QRect>
#include <QPainter>
#include <QByteArray>
#include <cstring>
#include <limits>

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

bool KisMiganBackend::hasRemovalPixels(const QImage &velyntoraMask)
{
    if (velyntoraMask.isNull()) return false;
    const QImage gray = velyntoraMask.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            if (row[x] >= 128) return true;
        }
    }
    return false;
}

KisMiganBackend::TensorInput KisMiganBackend::makeTensorInput(const QImage &rgb,
                                                               const QImage &mask,
                                                               QString *error)
{
    TensorInput input;
    if (rgb.isNull() || mask.isNull() || rgb.size() != mask.size()) {
        if (error) *error = QStringLiteral("Invalid MI-GAN tensor input images.");
        return input;
    }
    if (rgb.format() != QImage::Format_RGB888 || mask.format() != QImage::Format_Grayscale8) {
        if (error) *error = QStringLiteral("MI-GAN tensor input must be RGB888 + Grayscale8.");
        return input;
    }

    input.width = rgb.width();
    input.height = rgb.height();
    const qsizetype pixels = input.pixelCount();
    if (pixels <= 0 || pixels > (std::numeric_limits<qsizetype>::max() / 3)) {
        if (error) *error = QStringLiteral("MI-GAN tensor dimensions overflow.");
        input.width = input.height = 0;
        return input;
    }
    input.rgb.resize(input.rgbByteCount());
    input.mask.resize(input.maskByteCount());
    if (input.rgb.size() != input.rgbByteCount() || input.mask.size() != input.maskByteCount()) {
        if (error) *error = QStringLiteral("Could not allocate MI-GAN tensor buffers.");
        input = TensorInput{};
        return input;
    }

    for (int y = 0; y < input.height; ++y) {
        if (rgb.bytesPerLine() < input.width * 3 || mask.bytesPerLine() < input.width) {
            if (error) *error = QStringLiteral("MI-GAN tensor input has an invalid row stride.");
            input = TensorInput{};
            return input;
        }
        memcpy(input.rgb.data() + qsizetype(y) * input.width * 3,
               rgb.constScanLine(y), size_t(input.width * 3));
        memcpy(input.mask.data() + qsizetype(y) * input.width,
               mask.constScanLine(y), size_t(input.width));
    }
    if (!input.isValid()) {
        if (error) *error = QStringLiteral("MI-GAN tensor packing produced an invalid payload.");
        return TensorInput{};
    }
    return input;
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

    if (!hasRemovalPixels(request.mask)) {
        result.error = QStringLiteral("Remove mask contains no selected pixels.");
        return result;
    }

    const QRect maskBounds = maskedBounds(request.mask);
    if (maskBounds.isEmpty()) {
        result.error = QStringLiteral("Remove mask is empty.");
        return result;
    }

    // Clamp the selection to the source before expanding it. This prevents
    // malformed/out-of-bounds masks from creating an invalid inference crop.
    const QRect clippedMaskBounds = maskBounds.intersected(request.source.rect());
    if (clippedMaskBounds.isEmpty()) {
        result.error = QStringLiteral("Remove mask does not intersect the source image.");
        return result;
    }

    // Process only the masked neighborhood. This is substantially cheaper on
    // Android than resizing the entire canvas and also preserves detail away
    // from the requested removal.
    const QRect roi = expandedSquare(clippedMaskBounds, request.source.size());
    if (roi.isEmpty() || !request.source.rect().contains(roi)) {
        result.error = QStringLiteral("Could not calculate a valid Remove region.");
        return result;
    }

    result.sourceRect = roi;

    // Keep a canonical binary Velyntora mask for final compositing. The
    // runtime-facing mask is inverted separately because MI-GAN uses the
    // opposite convention. Never composite with the inverted model mask.
    QImage compositeMask = request.mask.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < compositeMask.height(); ++y) {
        uchar *row = compositeMask.scanLine(y);
        for (int x = 0; x < compositeMask.width(); ++x) {
            row[x] = row[x] >= 128 ? 255 : 0;
        }
    }
    if (!hasRemovalPixels(compositeMask)) {
        result.error = QStringLiteral("Remove mask became empty during normalization.");
        return result;
    }

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
        if (pipelineMask.isNull()) {
            result.error = QStringLiteral("Could not prepare MI-GAN pipeline mask.");
            return result;
        }

        // The full ONNX pipeline expects an opaque RGB image. Flatten alpha
        // onto an opaque copy for inference; the original alpha remains owned
        // by Krita and will be restored when the generated patch is applied.
        QImage pipelineSource(request.source.size(), QImage::Format_RGB888);
        pipelineSource.fill(Qt::white);
        {
            QPainter painter(&pipelineSource);
            painter.drawImage(0, 0, request.source);
        }
        if (pipelineSource.isNull()) {
            result.error = QStringLiteral("Could not prepare MI-GAN RGB input.");
            return result;
        }

        // Runtime ownership boundary:
        // - pipelineSource and pipelineMask stay local to this call.
        // - a runtime implementation must copy/consume their bytes before return.
        // - generated pixels are accepted only after validateRuntimeOutput().
        // This prevents dangling QImage storage when inference is moved to a
        // worker thread on Android.
        // Make the buffers contiguous before handing them to a native
        // inference runtime. QImage scanlines may contain padding, while ONNX
        // tensor upload code must know the exact byte layout.
        const qsizetype rgbBytes = qsizetype(pipelineSource.bytesPerLine()) * pipelineSource.height();
        const qsizetype maskBytes = qsizetype(pipelineMask.bytesPerLine()) * pipelineMask.height();
        if (rgbBytes <= 0 || maskBytes <= 0) {
            result.error = QStringLiteral("Invalid MI-GAN input buffer size.");
            return result;
        }

        // Reject impossible layouts before a native tensor upload. RGB888
        // needs at least width*3 bytes per row and Grayscale8 at least width.
        // Extra bytes are legal Qt scanline padding and must be handled by
        // the runtime adapter rather than interpreted as image pixels.
        if (pipelineSource.bytesPerLine() < pipelineSource.width() * 3 ||
            pipelineMask.bytesPerLine() < pipelineMask.width()) {
            result.error = QStringLiteral("Invalid MI-GAN input row stride.");
            return result;
        }

        // Pack rows into tightly contiguous buffers. This is the exact
        // memory representation the native ONNX adapter can upload as HWC
        // uint8 tensors without accidentally including Qt row padding.
        QString tensorError;
        const TensorInput tensorInput = makeTensorInput(pipelineSource, pipelineMask, &tensorError);
        if (!tensorInput.isValid()) {
            result.error = tensorError.isEmpty()
                ? QStringLiteral("Could not create MI-GAN tensor input.")
                : tensorError;
            return result;
        }

        // Final adapter boundary: these counts must agree with both the
        // Qt images and the tightly packed tensor payload before ONNX sees
        // any pointer.
        if (rgbBytes < tensorInput.rgbByteCount() ||
            maskBytes < tensorInput.maskByteCount()) {
            result.error = QStringLiteral("MI-GAN source buffers are smaller than the packed tensor payload.");
            return result;
        }

        Q_UNUSED(rgbBytes);
        Q_UNUSED(maskBytes);
        Q_UNUSED(tensorInput);
        Q_UNUSED(pipelineSource);
        Q_UNUSED(pipelineMask);
        Q_UNUSED(compositeMask);
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
