#ifndef KIS_MIGAN_BACKEND_H
#define KIS_MIGAN_BACKEND_H

#include "kis_remove_ai_backend.h"

class KisMiganBackend final : public KisRemoveAIBackend
{
public:
    struct TensorInput {
        QByteArray rgb;
        QByteArray mask;
        int width = 0;
        int height = 0;
        qsizetype pixelCount() const { return qsizetype(width) * height; }
        qsizetype rgbByteCount() const { return pixelCount() * 3; }
        qsizetype maskByteCount() const { return pixelCount(); }
        bool isValid() const {
            return width > 0 && height > 0 &&
                   rgb.size() == rgbByteCount() &&
                   mask.size() == maskByteCount();
        }
    };
    bool isAvailable() const override;
    Result run(const Request &request) override;

private:
    static TensorInput makeTensorInput(const QImage &rgb, const QImage &mask, QString *error);
    static QByteArray packRgbNchw(const TensorInput &input, QString *error);
    static bool hasRemovalPixels(const QImage &velyntoraMask);
    static QImage compositeMaskedPatch(const QImage &sourcePatch,
                                       const QImage &generatedPatch,
                                       const QImage &removeMask);
    static QImage normalizeMask(const QImage &mask, const QSize &size);
    static bool validateRuntimeOutput(const QImage &image, const QSize &expectedSize, QString *error);
};

#endif
