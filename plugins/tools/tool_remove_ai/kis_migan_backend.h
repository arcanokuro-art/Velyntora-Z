#ifndef KIS_MIGAN_BACKEND_H
#define KIS_MIGAN_BACKEND_H

#include "kis_remove_ai_backend.h"

class KisMiganBackend final : public KisRemoveAIBackend
{
public:
    bool isAvailable() const override;
    Result run(const Request &request) override;

private:
    static QImage compositeMaskedPatch(const QImage &sourcePatch,
                                       const QImage &generatedPatch,
                                       const QImage &removeMask);
    static QImage normalizeMask(const QImage &mask, const QSize &size);
    static bool validateRuntimeOutput(const QImage &image, const QSize &expectedSize, QString *error);
};

#endif
