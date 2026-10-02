set(kritatoolremoveai_SOURCES
    tool_remove_ai.cpp
    kis_tool_remove_ai.cpp
    kis_migan_backend.cpp
)

kis_add_library(kritatoolremoveai MODULE ${kritatoolremoveai_SOURCES})
target_link_libraries(kritatoolremoveai kritaui)
install(TARGETS kritatoolremoveai DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})

# Ship the upstream MI-GAN license alongside the plugin. The model itself is
# intentionally not embedded until the Android ONNX Runtime packaging step.
install(FILES THIRD_PARTY_MI_GAN_LICENSE.txt DESTINATION ${DATA_INSTALL_DIR}/velyntora/licenses)
