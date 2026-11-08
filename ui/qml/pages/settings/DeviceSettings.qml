import QtQuick
import QtQuick.Layouts

// Module U7 (Codex). P3-3 音频与设备 (UI_REDESIGN §3.5 P3-3).
//   视频设备 🔒: AppComboBox of screens + cameras, secondaryText "屏幕"/"摄像头"
//     -> RecorderView.selectVideoCaptureDevice
//   系统声音设备 🔒 / 麦克风设备 🔒: RecorderView.captureDevices filtered by kind
//     ("system-audio"/"microphone") -> selectAudioCaptureDevice / selectMicrophoneCaptureDevice
//   The option lists read the captureDevices PROPERTY so they rebuild on
//   captureDevicesChanged (a binding does not track Q_INVOKABLE devicesOfKind() calls).
//   能否同时录两路: read-only from RecorderView.audioMixingAvailable
//   设备列表: "共 {n} 个设备" + "刷新设备" (RecorderView.refreshCaptureDevices); empty list ->
//     warning InlineAlert.
// FROZEN API: none.
ColumnLayout {
    id: root
    spacing: Theme.space16
    // Second line: unavailable reason, else the kind in Chinese (kinds from
    // capture/capture_types.cpp captureDeviceKindName; anything else is shown as-is).
    readonly property var kindNames: ({
            "screen": "屏幕",
            "camera": "摄像头",
            "system-audio": "系统声音",
            "microphone": "麦克风"
        })
    function kindText(kind) {
        return root.kindNames[kind] !== undefined ? root.kindNames[kind] : kind;
    }
    // devices: RecorderView.captureDevices; kinds: list of kinds, in this order.
    function options(devices, kinds) {
        var result = [];
        kinds.forEach(function (kind) {
            devices.forEach(function (device) {
                if (device.kind !== kind)
                    return;
                result.push({
                    text: device.displayName,
                    value: device.id,
                    enabled: device.available,
                    secondaryText: device.unavailableReason ? device.unavailableReason : root.kindText(device.kind)
                });
            });
        });
        return result;
    }

    // TODO(U7): rows.
    FormGroupTitle {
        text: "视频来源"
    }
    FormRow {
        title: "视频设备"
        control: AppComboBox {
            objectName: "videoDeviceCombo"
            model: root.options(RecorderView.captureDevices, ["screen", "camera"])
            locked: RecorderView.capturing
            enabled: !RecorderView.capturing
            currentIndex: indexOfValue(RecorderView.selectedVideoDeviceId)
            placeholderText: "没有找到可用设备"
            onActivated: function (index) {
                if (model[index].enabled)
                    RecorderView.selectVideoCaptureDevice(model[index].value);
            }
        }
    }
    FormGroupTitle {
        text: "音频与设备"
    }
    FormRow {
        title: "系统声音"
        control: AppComboBox {
            objectName: "systemAudioCombo"
            model: root.options(RecorderView.captureDevices, ["system-audio"])
            locked: RecorderView.capturing
            enabled: !RecorderView.capturing
            currentIndex: indexOfValue(RecorderView.selectedAudioDeviceId)
            placeholderText: "没有找到可用设备"
            onActivated: function (index) {
                if (model[index].enabled)
                    RecorderView.selectAudioCaptureDevice(model[index].value);
            }
        }
    }
    FormRow {
        title: "麦克风"
        control: AppComboBox {
            objectName: "microphoneCombo"
            model: root.options(RecorderView.captureDevices, ["microphone"])
            locked: RecorderView.capturing
            enabled: !RecorderView.capturing
            currentIndex: indexOfValue(RecorderView.selectedMicrophoneDeviceId)
            placeholderText: "没有找到可用设备"
            onActivated: function (index) {
                if (model[index].enabled)
                    RecorderView.selectMicrophoneCaptureDevice(model[index].value);
            }
        }
    }
    FormRow {
        title: "同时录制两路音频"
        control: Text {
            font: Theme.captionFont
            text: RecorderView.audioMixingAvailable ? "支持" : "仅一路混音"
            color: Theme.textSecondary
        }
    }
    InlineAlert {
        Layout.fillWidth: true
        visible: RecorderView.captureDevices.length === 0
        kind: "warning"
        title: "没有找到可用设备"
        body: "请检查系统权限和设备连接。"
    }
    Text {
        font: Theme.captionFont
        text: "共 " + RecorderView.captureDevices.length + " 个设备"
        color: Theme.textSecondary
    }
    AppButton {
        enabled: !RecorderView.capturing
        text: "刷新设备"
        onClicked: RecorderView.refreshCaptureDevices()
    }
}
