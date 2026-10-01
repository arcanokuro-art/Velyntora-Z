#ifndef KIS_TOOL_REMOVE_AI_H
#define KIS_TOOL_REMOVE_AI_H

#include <QPainterPath>
#include <QScopedPointer>
#include "kis_tool_paint.h"
#include "KisToolPaintFactoryBase.h"
#include <kis_icon.h>

class KisToolRemoveAI : public KisToolPaint
{
    Q_OBJECT
public:
    explicit KisToolRemoveAI(KoCanvasBase *canvas);
    ~KisToolRemoveAI() override;

    void beginPrimaryAction(KoPointerEvent *event) override;
    void continuePrimaryAction(KoPointerEvent *event) override;
    void endPrimaryAction(KoPointerEvent *event) override;
    void paint(QPainter &painter, const KoViewConverter &converter) override;
    void deactivate() override;

private:
    struct Private;
    const QScopedPointer<Private> m_d;
    void addMaskPoint(KoPointerEvent *event);
};

class KisToolRemoveAIFactory : public KisToolPaintFactoryBase
{
public:
    KisToolRemoveAIFactory() : KisToolPaintFactoryBase("Velyntora/KisToolRemoveAI")
    {
        setToolTip(i18n("Remove"));
        setSection(ToolBoxSection::Fill);
        // Temporary native icon until the dedicated Velyntora Remove artwork lands.
        setIconName(koIconNameCStr("krita_tool_smart_patch"));
        setPriority(5);
        setActivationShapeId(KRITA_TOOL_ACTIVATION_ID);
    }

    KoToolBase *createTool(KoCanvasBase *canvas) override { return new KisToolRemoveAI(canvas); }
};

#endif
