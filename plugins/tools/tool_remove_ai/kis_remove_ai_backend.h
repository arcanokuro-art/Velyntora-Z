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

        bool hasValidWriteBack() const {
            if (!ok || image.isNull() || writeBackRect.isEmpty() ||
                selectedPixelCount <= 0 || !sourceRect.contains(writeBackRect)) {
                return false;
            }

            // Backends may return either a full-source image or an ROI-sized
            // image. Reject every other geometry before Krita opens an undo
            // transaction and touches the active paint layer.
            const bool fullSourceResult = image.rect().contains(writeBackRect);
            const bool roiResult = image.size() == sourceRect.size();
            return fullSourceResult || roiResult;
        }
    };

    virtual ~KisRemoveAIBackend() = default;
    virtual bool isAvailable() const = 0;
    virtual Result run(const Request &request) = 0;
};

#endif
