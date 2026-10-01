#ifndef TOOL_REMOVE_AI_H
#define TOOL_REMOVE_AI_H
#include <QObject>
#include <QVariant>
class ToolRemoveAI : public QObject {
    Q_OBJECT
public:
    ToolRemoveAI(QObject *parent, const QVariantList &);
    ~ToolRemoveAI() override;
};
#endif
