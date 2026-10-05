# Drawing module

The working Drawing implementation is preserved from the Android/Drawing development branch.

Current implementation touchpoints:
- `libs/ui/KisMainWindow.cpp` — Drawing workspace/window integration.
- `libs/ui/KisView.cpp` — Drawing view behavior.
- `libs/ui/kis_statusbar.cc/.h` — Velyntora bottom bar/status UI.
- `libs/ui/toolbox/KoToolBoxLayout_p.h` — Drawing toolbox layout.
- `plugins/tools/basictools/kis_tool_path.cc/.h` and `KisToolPath.action` — Línea/Curva tool.

These files stay in their engine/plugin locations for now so Krita CMake and plugin discovery are not broken. This directory is the ownership boundary for the gradual extraction into Velyntora modules.
