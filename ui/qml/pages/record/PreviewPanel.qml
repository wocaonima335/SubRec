import QtQuick

// Module U5 (ZCode). P1 area B: preview container (radius 8, bg/preview, border by state:
// running 2 px record/default, paused 2 px warning/fg, failed danger/border), source chip
// ("屏幕 · 主显示器"), overlays: S0 no-device EmptyState, S2 spinner, S4 "已暂停" pill,
// S5 "正在保存…" pill, S6 error EmptyState (retry -> ShellView.requestStart(), refresh ->
// RecorderView.refreshCaptureDevices(), copy -> DesktopActions.copyText + Toast T7,
// extra line from RecorderView.recordingFailed partial path).
// FROZEN API: none.
Rectangle {
    id: root

    radius: Theme.radiusMd
    color: Theme.bgPreview

    readonly property string state: RecorderView.uiState
    // Partial file of the last failed session (S6 extra line), from recordingFailed.
    property string partialPath: ""

    border.width: state === "running" || state === "paused" ? 2 : 1
    border.color: state === "running" ? Theme.recordDefault
                : state === "paused" ? Theme.warningFg
                : state === "failed" ? Theme.dangerBorder
                : Theme.borderDefault

    PreviewSurface {
        anchors.fill: parent
        anchors.margins: 1
    }

    Connections {
        target: RecorderView
        function onRecordingFailed(error: string, partialFilePath: string) {
            root.partialPath = partialFilePath
        }
        function onCaptureStateChanged() {
            if (RecorderView.captureState === "Idle")
                root.partialPath = ""
        }
    }

    // Source chip ("来源类型 · 设备名"), shown while a source is selected and alive.
    Rectangle {
        visible: chipLabel.text.length > 0 && root.state !== "noDevice" && root.state !== "failed"
        x: Theme.space12
        y: Theme.space12
        width: chipLabel.implicitWidth + 20
        height: 24
        radius: 12
        color: Theme.previewChipBg

        Text {
            id: chipLabel
            anchors.centerIn: parent
            text: {
                if (!RecorderView.hasVideoDevice)
                    return ""
                const kind = RecorderView.selectedVideoKind === "camera" ? qsTr("摄像头") : qsTr("屏幕")
                const name = root.selectedDeviceName()
                return name.length > 0 ? kind + " · " + name : kind
            }
            font: Theme.captionFont
            color: "#FFFFFF"
            elide: Text.ElideRight
        }
    }

    function selectedDeviceName(): string {
        const devices = RecorderView.captureDevices
        const id = RecorderView.selectedVideoDeviceId
        for (let i = 0; i < devices.length; ++i) {
            if (devices[i].id === id)
                return devices[i].displayName !== undefined ? devices[i].displayName : ""
        }
        return ""
    }

    // S2 preparing: black + centred 24 px spinner (§3.2.3).
    Rectangle {
        visible: root.state === "preparing"
        anchors.fill: parent
        radius: root.radius
        color: Theme.bgPreview
        Spinner {
            anchors.centerIn: parent
            size: 24
            color: Theme.textSecondary
            running: visible
        }
    }

    // S4 paused / S5 stopping: centred pill over the frozen frame.
    Rectangle {
        visible: root.state === "paused" || root.state === "stopping"
        anchors.centerIn: parent
        width: pauseLabel.implicitWidth + 32
        height: 32
        radius: 16
        color: Theme.previewChipBg
        Text {
            id: pauseLabel
            anchors.centerIn: parent
            text: root.state === "paused" ? qsTr("已暂停") : qsTr("正在保存…")
            font: Theme.bodyFont
            color: "#FFFFFF"
        }
    }

    // S0 no device (§3.2.3).
    EmptyState {
        visible: root.state === "noDevice"
        anchors.fill: parent
        iconName: "monitor-off"
        iconColor: Theme.textTertiary
        title: qsTr("没有找到可用的视频来源")
        body: qsTr("请连接显示器或摄像头后刷新。屏幕录制不可用时，可在 设置 › 高级 里更换屏幕采集方式。")
        secondaryText: qsTr("刷新设备")
        onSecondaryClicked: RecorderView.refreshCaptureDevices()
    }

    // S6 error (§3.2.3): 85 % preview overlay; remediation/partial-file line as detail.
    Rectangle {
        visible: root.state === "failed"
        anchors.fill: parent
        radius: root.radius
        color: Theme.bgPreview
        opacity: 0.85

        EmptyState {
            anchors.fill: parent
            iconName: "circle-alert"
            iconColor: Theme.dangerFg
            title: qsTr("录制失败")
            body: RecorderView.captureError
            detail: root.partialPath.length > 0
                ? qsTr("已录制的部分可能已保存到 %1").arg(root.partialPath)
                : RecorderView.captureRemediation
            secondaryText: qsTr("刷新设备")
            primaryText: qsTr("重试")
            linkText: qsTr("复制错误信息")
            primaryEnabled: RecorderView.canStartCapture
            onSecondaryClicked: RecorderView.refreshCaptureDevices()
            onPrimaryClicked: ShellView.requestStart()
            onLinkClicked: {
                DesktopActions.copyText(RecorderView.captureError + "\n" + RecorderView.captureRemediation)
                Toasts.show("info", qsTr("已复制"), "", [], 3000) // T7
            }
        }
    }
}
