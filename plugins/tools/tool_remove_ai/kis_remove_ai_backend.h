#ifndef KIS_REMOVE_AI_BACKEND_H
#define KIS_REMOVE_AI_BACKEND_H

#include <QImage>
#include <QString>
#include <QRect>

class KisRemoveAIBackend
{
public:
    struct Request {
        QImage source;
        QImage mask;
        int modelSize = 512;
        bool allowUpscale = true;
        bool modelHandlesPipeline = true;
    };
    struct Result {
        bool ok = false;
        QImage image;
        QRect sourceRect;
        QRect writeBackRect;
        qsizetype selectedPixelCount = 0;
        QSize inferenceSize;
        QString error;
    };

    virtual ~KisRemoveAIBackend() = default;
    virtual bool isAvailable() const = 0;
    virtual Result run(const Request &request) = 0;
};

#endif
