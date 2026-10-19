cmake_minimum_required(VERSION 3.22)
project(FBTECHMusic VERSION 0.5.2)

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/JUCE)
endif()
add_subdirectory(${JUCE_DIR} JUCE_build)

# Ricrea l'icona dai byte base64 (funziona anche con il vecchio build.yml)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.b64" AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.ico")
    execute_process(COMMAND powershell -NoProfile -Command "$b64 = Get-Content -Raw '${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.b64'; [IO.File]::WriteAllBytes('${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.ico', [Convert]::FromBase64String($b64))")
endif()

juce_add_gui_app(FBTECHMusic
    PRODUCT_NAME "FBTECH Music"
    COMPANY_NAME "FBTech"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.ico"
)

target_sources(FBTECHMusic PRIVATE
    Source/Main.cpp
    Source/Engine.cpp
    Source/MainComponent.cpp
)

target_compile_definitions(FBTECHMusic PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(FBTECHMusic PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
)
