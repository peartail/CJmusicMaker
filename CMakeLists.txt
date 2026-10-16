cmake_minimum_required(VERSION 3.22)

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MUSICBITMAKER_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(MusicBitMaker VERSION ${MUSICBITMAKER_VERSION} LANGUAGES C CXX)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:Embedded>")
    set(ENV{_CL_} "/Z7")
endif()

add_subdirectory(external/tracktion_engine/modules/juce EXCLUDE_FROM_ALL)
add_subdirectory(external/tracktion_engine/modules EXCLUDE_FROM_ALL)

juce_add_gui_app(MusicBitMaker
    COMPANY_NAME "MusicBitMaker"
    VERSION "${PROJECT_VERSION}"
    PRODUCT_NAME "MusicBitMaker")

if (WIN32)
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/MusicBitMaker_artefacts/JuceLibraryCode/MusicBitMaker_resources.rc")
endif()

if (MSVC)
    set_property(TARGET MusicBitMaker PROPERTY
        MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:Embedded>")
    target_compile_options(MusicBitMaker PRIVATE "$<$<CONFIG:Debug>:/Z7>")
endif()

target_sources(MusicBitMaker PRIVATE src/Main.cpp)
target_compile_features(MusicBitMaker PRIVATE cxx_std_20)

target_compile_definitions(MusicBitMaker PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MusicBitMaker,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="v${PROJECT_VERSION}"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

target_link_libraries(MusicBitMaker PRIVATE
    tracktion::tracktion_engine
    juce::juce_audio_utils
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_warning_flags)
