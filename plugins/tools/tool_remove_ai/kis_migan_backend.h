#ifndef KIS_MIGAN_BACKEND_H
#define KIS_MIGAN_BACKEND_H

#include "kis_remove_ai_backend.h"

class KisMiganBackend final : public KisRemoveAIBackend
{
public:
    bool isAvailable() const override;
    Result run(const Request &request) override;

private:
    static QImage normalizeMask(const QImage &mask, const QSize &size);
};

#endif
