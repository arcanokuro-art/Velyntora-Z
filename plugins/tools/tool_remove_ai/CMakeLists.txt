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

        # Android needs the native runtime inside the final package, not only
        # linked while compiling the plugin. Install the resolved shared
        # library so the deployment step can collect it into the APK.
        if(ANDROID)
            if(NOT VELYN_ONNXRUNTIME_LIBRARY MATCHES "\\.(so|so\\.[0-9.]+)$")
                message(FATAL_ERROR "Velyntora Remove AI: Android requires a shared libonnxruntime.so")
            endif()
            install(FILES "${VELYN_ONNXRUNTIME_LIBRARY}" DESTINATION lib)

            # Catch the most common Android packaging mistake early: linking
            # an ONNX Runtime binary built for a different ABI.
            get_filename_component(VELYN_ONNXRUNTIME_LIB_DIR "${VELYN_ONNXRUNTIME_LIBRARY}" DIRECTORY)
            string(TOLOWER "${VELYN_ONNXRUNTIME_LIB_DIR}" VELYN_ONNXRUNTIME_LIB_DIR_LOWER)
            if(DEFINED ANDROID_ABI AND NOT "${ANDROID_ABI}" STREQUAL "")
                string(TOLOWER "${ANDROID_ABI}" VELYN_ANDROID_ABI_LOWER)
                if(VELYN_ONNXRUNTIME_LIB_DIR_LOWER MATCHES "arm64-v8a|armeabi-v7a|x86_64|x86")
                    if(NOT VELYN_ONNXRUNTIME_LIB_DIR_LOWER MATCHES "${VELYN_ANDROID_ABI_LOWER}")
                        message(FATAL_ERROR
                            "Velyntora Remove AI: ONNX Runtime ABI does not match ANDROID_ABI=${ANDROID_ABI}: ${VELYN_ONNXRUNTIME_LIBRARY}")
                    endif()
                endif()
            endif()
        endif()
        message(STATUS "Velyntora Remove AI: ONNX Runtime enabled")
    else()
        # A configured runtime path means the caller expects Remove AI to be
        # functional. Do not silently produce a release where the tool is
        # present but can never run.
        message(FATAL_ERROR "Velyntora Remove AI: ONNXRUNTIME_ROOT is set but headers/library were not found")
    endif()
else()
    message(STATUS "Velyntora Remove AI: ONNX Runtime disabled (set ONNXRUNTIME_ROOT to enable)")
endif()
install(TARGETS kritatoolremoveai DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})

# Ship the upstream MI-GAN license alongside the plugin.
install(FILES THIRD_PARTY_MI_GAN_LICENSE.txt DESTINATION ${DATA_INSTALL_DIR}/velyntora/licenses)

# The trained model is intentionally kept out of source control. Release and
# Android packaging jobs may provide it explicitly; fail configuration if a
# supplied path is wrong instead of producing an APK whose Remove tool can
# never become available.
set(VELYN_MIGAN_MODEL "" CACHE FILEPATH "Path to the official MI-GAN ONNX model")
if(VELYN_MIGAN_MODEL)
    if(NOT EXISTS "${VELYN_MIGAN_MODEL}")
        message(FATAL_ERROR "Velyntora Remove AI: VELYN_MIGAN_MODEL does not exist: ${VELYN_MIGAN_MODEL}")
    endif()
    get_filename_component(VELYN_MIGAN_MODEL_EXT "${VELYN_MIGAN_MODEL}" EXT)
    if(NOT VELYN_MIGAN_MODEL_EXT STREQUAL ".onnx")
        message(FATAL_ERROR "Velyntora Remove AI: VELYN_MIGAN_MODEL must be an .onnx file")
    endif()
    install(FILES "${VELYN_MIGAN_MODEL}"
            DESTINATION ${DATA_INSTALL_DIR}/velyntora/models
            RENAME migan.onnx)
    message(STATUS "Velyntora Remove AI: MI-GAN model will be packaged as models/migan.onnx")
else()
    message(STATUS "Velyntora Remove AI: MI-GAN model not packaged (set VELYN_MIGAN_MODEL for release builds)")
endif()


# Release/Android builds can opt into a strict completeness gate. This makes
# missing native inference assets a configuration error instead of a runtime
# surprise.
option(VELYN_REMOVE_AI_REQUIRED "Require a complete Remove AI runtime/model package" OFF)
if(VELYN_REMOVE_AI_REQUIRED)
    if(NOT ONNXRUNTIME_ROOT OR NOT VELYN_ONNXRUNTIME_INCLUDE_DIR OR NOT VELYN_ONNXRUNTIME_LIBRARY)
        message(FATAL_ERROR "Velyntora Remove AI: complete ONNX Runtime is required for this build")
    endif()
    if(NOT VELYN_MIGAN_MODEL OR NOT EXISTS "${VELYN_MIGAN_MODEL}")
        message(FATAL_ERROR "Velyntora Remove AI: MI-GAN model is required for this build")
    endif()
endif()
