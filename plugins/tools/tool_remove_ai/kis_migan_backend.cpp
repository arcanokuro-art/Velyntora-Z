#include "kis_migan_backend.h"

#include <QFileInfo>

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

    const QSize inferenceSize(request.modelSize, request.modelSize);
    const QImage source512 = request.source.convertToFormat(QImage::Format_RGBA8888)
                                           .scaled(inferenceSize, Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
    const QImage mask512 = normalizeMask(request.mask, inferenceSize);

    if (source512.isNull() || mask512.isNull()) {
        result.error = QStringLiteral("Could not prepare Remove inference input.");
        return result;
    }

    if (!isAvailable()) {
        result.error = QStringLiteral("MI-GAN runtime is not installed yet.");
        return result;
    }

    // Runtime hook: feed source512 + mask512 into the packaged MI-GAN model.
    // Do not fabricate a result: success is set only after real inference.
    result.error = QStringLiteral("MI-GAN inference hook is not connected yet.");
    return result;
}
