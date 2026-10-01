set(kritatoolremoveai_SOURCES
    kis_tool_remove_ai.cpp
)

kis_add_library(kritatoolremoveai MODULE ${kritatoolremoveai_SOURCES})
target_link_libraries(kritatoolremoveai kritaui)
install(TARGETS kritatoolremoveai DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
