#include "tool_remove_ai.h"
#include <kpluginfactory.h>
#include <KoToolRegistry.h>
#include "kis_tool_remove_ai.h"

K_PLUGIN_FACTORY_WITH_JSON(RemoveAIToolsFactory, "kritatoolremoveai.json", registerPlugin<ToolRemoveAI>();)

ToolRemoveAI::ToolRemoveAI(QObject *parent, const QVariantList &) : QObject(parent)
{
    KoToolRegistry::instance()->add(new KisToolRemoveAIFactory());
}
ToolRemoveAI::~ToolRemoveAI() = default;
#include "tool_remove_ai.moc"
