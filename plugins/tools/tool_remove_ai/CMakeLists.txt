set(kritatoolremoveai_SOURCES
    tool_remove_ai.cpp
    kis_tool_remove_ai.cpp
    kis_migan_backend.cpp
)

kis_add_library(kritatoolremoveai MODULE ${kritatoolremoveai_SOURCES})
target_link_libraries(kritatoolremoveai kritaui)

# Optional native ONNX Runtime integration. Android packaging can provide
# ONNXRUNTIME_ROOT pointing at an extracted ONNX Runtime distribution.
# Desktop builds without it keep compiling, but Remove AI stays unavailable
# rather than silently falling back to a fake implementation.
set(ONNXRUNTIME_ROOT "" CACHE PATH "Root of the ONNX Runtime distribution for Remove AI")
if(ONNXRUNTIME_ROOT)
    find_path(VELYN_ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        PATHS "${ONNXRUNTIME_ROOT}/include"
        NO_DEFAULT_PATH)
    find_library(VELYN_ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        PATHS "${ONNXRUNTIME_ROOT}/lib"
        NO_DEFAULT_PATH)

    if(VELYN_ONNXRUNTIME_INCLUDE_DIR AND VELYN_ONNXRUNTIME_LIBRARY)
        target_include_directories(kritatoolremoveai PRIVATE "${VELYN_ONNXRUNTIME_INCLUDE_DIR}")
        target_link_libraries(kritatoolremoveai "${VELYN_ONNXRUNTIME_LIBRARY}")
        target_compile_definitions(kritatoolremoveai PRIVATE VELYN_REMOVE_HAS_ONNXRUNTIME=1)
        message(STATUS "Velyntora Remove AI: ONNX Runtime enabled")
    else()
        message(WARNING "Velyntora Remove AI: ONNXRUNTIME_ROOT is set but headers/library were not found")
    endif()
else()
    message(STATUS "Velyntora Remove AI: ONNX Runtime disabled (set ONNXRUNTIME_ROOT to enable)")
endif()
install(TARGETS kritatoolremoveai DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})

# Ship the upstream MI-GAN license alongside the plugin. The model itself is
# intentionally not embedded until the Android ONNX Runtime packaging step.
install(FILES THIRD_PARTY_MI_GAN_LICENSE.txt DESTINATION ${DATA_INSTALL_DIR}/velyntora/licenses)
