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
                selectedPixelCount <= 0 || sourceRect.isEmpty() ||
                !sourceRect.contains(writeBackRect)) {
                return false;
            }

            // Backends may return either a full-source image or an ROI-sized
            // image. For an ROI result, translate the document-space
            // write-back rectangle into ROI-local coordinates and validate it
            // explicitly before Krita touches the active paint layer.
            const bool fullSourceResult = image.rect().contains(writeBackRect);
            const QRect localWriteBack = writeBackRect.translated(-sourceRect.topLeft());
            const bool roiResult = image.size() == sourceRect.size() &&
                                   image.rect().contains(localWriteBack);
            return fullSourceResult || roiResult;
        }

        QRect imageWriteBackRect() const {
            if (!hasValidWriteBack()) {
                return QRect();
            }
            if (image.size() == sourceRect.size()) {
                return writeBackRect.translated(-sourceRect.topLeft());
            }
            return writeBackRect;
        }

        QPoint documentWriteBackOrigin() const {
            return hasValidWriteBack() ? writeBackRect.topLeft() : QPoint();
        }

        QImage writeBackImage() const {
            const QRect rect = imageWriteBackRect();
            if (rect.isEmpty()) {
                return QImage();
            }
            const QImage patch = image.copy(rect);
            if (patch.isNull() || patch.size() != writeBackRect.size()) {
                return QImage();
            }
            return patch;
        }

        bool hasUsableWriteBackImage() const {
            const QImage patch = writeBackImage();
            return !patch.isNull() && patch.size() == writeBackRect.size();
        }

        bool isReadyForCommit() const {
            if (!hasValidWriteBack() || !hasUsableWriteBackImage()) {
                return false;
            }
            // A successful backend result must not carry an error. Keeping
            // this invariant prevents Krita from committing a partially
            // failed inference into the undo history.
            return error.isEmpty() && inferenceSize.isValid();
        }

        QString commitValidationError() const {
            if (!ok) return QStringLiteral("Remove AI inference did not succeed.");
            if (!error.isEmpty()) return error;
            if (!inferenceSize.isValid()) return QStringLiteral("Remove AI inference size is invalid.");
            if (!hasValidWriteBack()) return QStringLiteral("Remove AI write-back geometry is invalid.");
            if (!hasUsableWriteBackImage()) return QStringLiteral("Remove AI write-back image is invalid.");
            return QString();
        }
    };

    virtual ~KisRemoveAIBackend() = default;
    virtual bool isAvailable() const = 0;
    virtual Result run(const Request &request) = 0;
};

#endif
