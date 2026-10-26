# QML模块
qt_add_library(qml_module STATIC)

# 设置C++17标准
set_target_properties(qml_module PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

qt_add_qml_module(qml_module
    URI SubRec
    VERSION 1.0
    QML_FILES
    Main.qml
     QML_FILES videopage.qml
     QML_FILES settingpage.qml
     QML_FILES cuttingpage.qml
     QML_FILES settinglist.qml
     QML_FILES ScreenTitle.qml
     QML_FILES AudioVisualizer.qml
     QML_FILES AudioIndicator.qml
     QML_FILES RecordButton.qml
     QML_FILES TimeDisplay.qml
     QML_FILES PanelBackground.qml
     QML_FILES OptionsArea.qml
     QML_FILES SourceSelectButton.qml
     QML_FILES VerticalSeparator.qml
    RESOURCES
        ../icons/Pause.png
        ../icons/Setting.png
        ../icons/main.png
        ../icons/main-gray.png
        ../icons/record.png
        ../icons/setting-gray.png
        ../icons/video-gray.png
        ../icons/video.png
        ../icons/microphone.png
        ../icons/audio.png
        ../icons/camera.png
        ../icons/screen.png
)

target_link_libraries(qml_module
    PRIVATE Qt6::Quick
)
