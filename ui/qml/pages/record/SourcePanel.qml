pragma ComponentBehavior: Bound
import QtQuick

// Module U5 (ZCode). P1 area D (Card, inner width 248): three sections.
// - 视频来源: SegmentedControl [屏幕][摄像头] (currentValue = RecorderView.selectedVideoKind,
//   activated -> RecorderView.openSource(kind)); AppComboBox of the devices of that kind
//   (options() below, from RecorderView.captureDevices) -> selectVideoCaptureDevice.
// - 音频: per plan decision 3 the engine gives ONE mix level, so the audio group shows a
//   single shared LevelMeter at its top; below it the 系统声音 / 麦克风 rows (AppSwitch ->
//   RecorderView.setAudioMode(...) following the old OptionsArea rules A1–A6, mixing needs
//   audioMixingAvailable) and compact device combos; explanation line A2/A3.
// - AI 字幕: hidden until SubtitleView.serviceAttached (plan decision 9). AppSwitch bound
//   to SubtitleView.enabled; description + action per SubtitleView.uiState (U1–U10,
//   §3.2.4); "字幕设置 ›" -> Navigation.open("settings/subtitle"); "下载模型" ->
//   ModelCatalog.requestDownload(SubtitleView.modelId); "取消下载" -> ModelCatalog.cancelDownload;
//   "在文件夹中显示" -> SubtitleView.openLastSrtFolder().
// Everything is locked (not disabled) while RecorderView.capturing (§3.2.5).
// FROZEN API: none.
Card {
    id: root
    padding: Theme.space16

    readonly property bool locked: RecorderView.capturing
    readonly property string audioMode: RecorderView.audioMode
    // AppComboBox options ({text, value, enabled, secondaryText?}; AppComboBox presets
    // textRole "text" / valueRole "value"). The raw device maps of RecorderView have no
    // text/value fields, so used directly they gave empty rows, each showing the check mark
    // (undefined === undefined). Built from the captureDevices PROPERTY so the lists follow
    // captureDevicesChanged (a binding does not track Q_INVOKABLE devicesOfKind() calls).
    // Same shape as DeviceSettings.qml options(); the second line is only the unavailable
    // reason here, the kind is implied by the section.
    function options(devices, kind) {
        var result = [];
        devices.forEach(function (device) {
            if (device.kind !== kind)
                return;
            var option = { text: device.displayName, value: device.id, enabled: device.available };
            if (device.unavailableReason)
                option.secondaryText = device.unavailableReason;
            result.push(option);
        });
        return result;
    }
    function indexOfId(options, id) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].value === id)
                return i;
        }
        return -1;
    }
    readonly property var videoDevices: root.options(RecorderView.captureDevices,
        RecorderView.selectedVideoKind === "camera" ? "camera" : "screen")
    readonly property var systemDevices: root.options(RecorderView.captureDevices, "system-audio")
    readonly property var micDevices: root.options(RecorderView.captureDevices, "microphone")

    // §3.2.5 audio rules.
    readonly property bool systemOn: audioMode === "system" || audioMode === "both"
    readonly property bool micOn: audioMode === "microphone" || audioMode === "both"
    // A2: with no mixing, the other switch is disabled while one source is on.
    readonly property bool systemLockedOff: !RecorderView.audioMixingAvailable && micOn
    readonly property bool micLockedOff: !RecorderView.audioMixingAvailable && systemOn
    // A3: both off.
    readonly property bool silent: audioMode === "none"

    function setSystemAudio(on: bool) {
        if (locked)
            return
        if (on) {
            if (micOn && RecorderView.audioMixingAvailable)
                RecorderView.setAudioMode("both")
            else
                RecorderView.setAudioMode("system")
        } else {
            RecorderView.setAudioMode(micOn ? "microphone" : "none")
        }
    }
    function setMicrophone(on: bool) {
        if (locked)
            return
        if (on) {
            if (systemOn && RecorderView.audioMixingAvailable)
                RecorderView.setAudioMode("both")
            else
                RecorderView.setAudioMode("microphone")
        } else {
            RecorderView.setAudioMode(systemOn ? "system" : "none")
        }
    }

    Column {
        id: column
        anchors.fill: parent
        spacing: 0

        // ---- 视频来源 ----
        Text {
            text: qsTr("视频来源")
            font: Theme.bodyStrongFont
            color: Theme.textPrimary
        }
        SegmentedControl {
            id: sourceSwitch
            topPadding: Theme.space8
            width: parent.width
            model: [
                { text: qsTr("屏幕"), value: "screen", iconName: "monitor" },
                { text: qsTr("摄像头"), value: "camera", iconName: "camera" }
            ]
            currentValue: RecorderView.selectedVideoKind.length > 0 ? RecorderView.selectedVideoKind : "screen"
            locked: root.locked
            onActivated: (value) => {
                if (!root.locked)
                    RecorderView.openSource(value)
            }
        }
        AppComboBox {
            id: videoCombo
            objectName: "sourceVideoCombo"
            topPadding: Theme.space8
            width: parent.width
            model: root.videoDevices
            currentIndex: root.indexOfId(root.videoDevices, RecorderView.selectedVideoDeviceId)
            placeholderText: root.videoDevices.length === 0 ? qsTr("没有设备") : qsTr("未选择设备")
            locked: root.locked
            onActivated: (index) => {
                const option = model[index]
                if (option && option.enabled && !root.locked)
                    RecorderView.selectVideoCaptureDevice(option.value)
            }
        }

        Item { width: 1; height: Theme.space16 }
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.borderDefault
        }
        Item { width: 1; height: Theme.space16 }

        // ---- 音频 ----
        Text {
            text: qsTr("音频")
            font: Theme.bodyStrongFont
            color: Theme.textPrimary
        }
        // Decision 3: one shared meter for the recorded mix, at the top of the group.
        Item { width: 1; height: Theme.space12 }
        LevelMeter {
            width: parent.width
            levelDb: AudioLevelMonitor.mixLevelDb
            active: AudioLevelMonitor.active
            channelOn: !root.silent
        }

        // 系统声音 row.
        Item { width: 1; height: Theme.space12 }
        Row {
            spacing: Theme.space8
            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "volume-2"
                size: 16
                color: Theme.textSecondary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.parent.width - 16 - 8 - 36 - 8
                text: qsTr("系统声音")
                font: Theme.bodyFont
                color: Theme.textPrimary
                elide: Text.ElideRight
            }
            AppSwitch {
                anchors.verticalCenter: parent.verticalCenter
                backendChecked: root.systemOn
                locked: root.locked
                enabled: !root.systemLockedOff || root.systemOn
                disabledReason: qsTr("当前只能录一路声音")
                onToggled: root.setSystemAudio(!root.systemOn)
            }
        }
        AppComboBox {
            topPadding: Theme.space8
            width: parent.width
            compact: true
            objectName: "sourceSystemAudioCombo"
            model: root.systemDevices
            currentIndex: root.indexOfId(root.systemDevices, RecorderView.selectedAudioDeviceId)
            placeholderText: root.systemDevices.length === 0 ? qsTr("没有找到播放设备") : qsTr("未选择设备")
            locked: root.locked
            enabled: root.systemOn
            onActivated: (index) => {
                const option = model[index]
                if (option && option.enabled && !root.locked)
                    RecorderView.selectAudioCaptureDevice(option.value)
            }
        }

        // 麦克风 row.
        Item { width: 1; height: Theme.space12 }
        Row {
            spacing: Theme.space8
            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.micOn ? "mic" : "mic-off"
                size: 16
                color: Theme.textSecondary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.parent.width - 16 - 8 - 36 - 8
                text: qsTr("麦克风")
                font: Theme.bodyFont
                color: Theme.textPrimary
                elide: Text.ElideRight
            }
            AppSwitch {
                anchors.verticalCenter: parent.verticalCenter
                backendChecked: root.micOn
                locked: root.locked
                enabled: !root.micLockedOff || root.micOn
                disabledReason: qsTr("当前只能录一路声音")
                onToggled: root.setMicrophone(!root.micOn)
            }
        }
        AppComboBox {
            topPadding: Theme.space8
            width: parent.width
            compact: true
            objectName: "sourceMicrophoneCombo"
            model: root.micDevices
            currentIndex: root.indexOfId(root.micDevices, RecorderView.selectedMicrophoneDeviceId)
            placeholderText: root.micDevices.length === 0 ? qsTr("没有找到麦克风") : qsTr("未选择设备")
            locked: root.locked
            enabled: root.micOn
            onActivated: (index) => {
                const option = model[index]
                if (option && option.enabled && !root.locked)
                    RecorderView.selectMicrophoneCaptureDevice(option.value)
            }
        }

        // §3.2.5 explanation line (A2 single-source note / A3 silent warning).
        Item { width: 1; height: Theme.space8; visible: root.silent || root.systemLockedOff || root.micLockedOff }
        Text {
            width: parent.width
            visible: text.length > 0
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font: Theme.captionFont
            color: root.silent ? Theme.warningFg : Theme.textSecondary
            text: root.silent
                ? qsTr("两路都已关闭，将录制无声视频。")
                : (root.systemLockedOff || root.micLockedOff)
                ? qsTr("同时录两路需要 WASAPI 系统声音；当前只能录一路。")
                : ""
        }

        // ---- AI 字幕 (hidden until the service lands — decision 9) ----
        Column {
            visible: SubtitleView.serviceAttached
            width: parent.width

            Item { width: 1; height: Theme.space16 }
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.borderDefault
            }
            Item { width: 1; height: Theme.space16 }

            Item { width: 1; height: Theme.space12 }
            Row {
                spacing: Theme.space8
                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "captions"
                    size: 16
                    color: Theme.textSecondary
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.parent.width - 16 - 8 - 36 - 8
                    text: qsTr("AI 字幕")
                    font: Theme.bodyStrongFont
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
                AppSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    backendChecked: SubtitleView.enabled
                    locked: root.locked || SubtitleView.settingsLocked
                    onToggled: SubtitleView.enabled = !SubtitleView.enabled
                }
            }

            // §3.2.4 description text (two lines) + optional progress bar.
            Item { width: 1; height: Theme.space8 }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font: Theme.captionFont
                color: SubtitleView.uiState === "unavailable" || SubtitleView.uiState === "failed"
                    ? Theme.dangerFg
                    : SubtitleView.uiState === "needModel" || SubtitleView.uiState === "lagging"
                    ? Theme.warningFg : Theme.textSecondary
                text: {
                    switch (SubtitleView.uiState) {
                    case "needModel":
                        return qsTr("需要先下载识别模型。下载前不会生成字幕。")
                    case "downloading":
                        return qsTr("正在下载模型 %1%").arg(Math.round(SubtitleView.progress * 100))
                    case "unavailable":
                    case "failed":
                        return SubtitleView.lastError
                    case "ready":
                        return qsTr("已开启 · 录制时识别")
                    case "live":
                        return SubtitleView.mode === "after" ? qsTr("停止录制后开始识别") : qsTr("正在边录边识别")
                    case "lagging":
                        return qsTr("识别跟不上，已积压 %1 秒。不影响录制。").arg(Math.round(SubtitleView.lagSeconds))
                    case "finishing":
                        return qsTr("正在生成字幕 %1%").arg(Math.round(SubtitleView.progress * 100))
                    case "done":
                        return qsTr("已生成字幕")
                    default:
                        return qsTr("已关闭。打开后，停止录制时会在视频旁生成同名 .srt 字幕。")
                    }
                }
            }
            AppProgressBar {
                topPadding: Theme.space4  // ProgressBar is a Control: padding exists
                width: parent.width
                visible: SubtitleView.uiState === "downloading" || SubtitleView.uiState === "finishing"
                indeterminate: SubtitleView.uiState === "downloading" && SubtitleView.progress <= 0
                value: SubtitleView.progress
            }

            // §3.2.4 action row.
            AppButton {
                topPadding: Theme.space8 // Control padding
                small: true
                variant: SubtitleView.uiState === "needModel" ? "secondary" : "ghost"
                iconName: SubtitleView.uiState === "needModel" ? "download"
                         : SubtitleView.uiState === "done" ? "folder-open" : ""
                text: {
                    switch (SubtitleView.uiState) {
                    case "needModel":
                        return qsTr("下载模型")
                    case "downloading":
                        return qsTr("取消下载")
                    case "done":
                        return qsTr("在文件夹中显示")
                    case "live":
                    case "lagging":
                    case "finishing":
                        return ""
                    default:
                        return qsTr("字幕设置 ›")
                    }
                }
                visible: text.length > 0
                onClicked: {
                    switch (SubtitleView.uiState) {
                    case "needModel":
                        ModelCatalog.requestDownload(SubtitleView.modelId)
                        break
                    case "downloading":
                        ModelCatalog.cancelDownload(SubtitleView.modelId)
                        break
                    case "done":
                        SubtitleView.openLastSrtFolder()
                        break
                    default:
                        Navigation.open("settings/subtitle")
                    }
                }
            }
        }
    }
}
