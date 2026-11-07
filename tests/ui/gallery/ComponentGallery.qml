import QtQuick
import SubRec.Ui

// Module U2 (ZCode). Component gallery: one page showing every component in its states
// for offscreen load-testing (ui_interface_smoke) and PC3 screenshot comparison against
// UI_REDESIGN §2.5. Pure presentation — no backend singletons are touched.
Rectangle {
    id: root
    width: 900
    height: content.implicitHeight + Theme.space32
    color: Theme.bgApp

    function sectionLabel(text: string): string { return text }

    Column {
        id: content
        x: Theme.space24
        y: Theme.space24
        width: root.width - Theme.space48
        spacing: Theme.space24

        Text {
            text: qsTr("组件画廊 Component Gallery")
            font: Theme.h1Font
            color: Theme.textPrimary
        }

        // ---- §2.5.1 buttons ----
        Text { text: qsTr("按钮 AppButton"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space8
            AppButton { text: "Primary"; variant: "primary" }
            AppButton { text: "Secondary" }
            AppButton { text: "Ghost"; variant: "ghost" }
            AppButton { text: "Danger"; variant: "danger" }
            AppButton { text: "小按钮"; small: true }
        }
        Row {
            spacing: Theme.space8
            AppButton { text: "加载中"; variant: "primary"; loading: true }
            AppButton { text: "禁用"; variant: "primary"; enabled: false; disabledReason: "没有可用的视频来源" }
            AppButton { text: "带图标"; iconName: "refresh-cw" }
            AppButton { text: "小·图标"; small: true; iconName: "download" }
        }

        // ---- §2.5.1 icon button ----
        Text { text: qsTr("图标按钮 IconButton"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space8
            IconButton { iconName: "folder-open"; toolTipText: "打开保存文件夹" }
            IconButton { iconName: "trash-2"; dangerHover: true; toolTipText: "删除" }
            IconButton { iconName: "pause"; round: true }
            IconButton { iconName: "x"; enabled: false; toolTipText: "禁用" }
            AppIcon { name: "monitor"; size: 32; color: Theme.textSecondary }
        }

        // ---- §2.5.2 text field ----
        Text { text: qsTr("输入框 AppTextField"); font: Theme.h2Font; color: Theme.textPrimary }
        Column {
            spacing: Theme.space12
            Row {
                spacing: Theme.space12
                AppTextField { placeholderText: "搜索文件名"; leadingIconName: "search" }
                AppTextField { text: "D:\\Videos\\SubRec"; mono: true }
            }
            Row {
                spacing: Theme.space12
                AppTextField { text: "Sub?"; errorText: "文件名不能包含 ? 等字符" }
                AppTextField { text: "D:\\Videos\\SubRec"; mono: true; locked: true }
                AppTextField { text: "SubRec_{日期}"; suffixText: ".mp4" }
            }
        }

        // ---- §2.5.3 combo box ----
        Text { text: qsTr("下拉框 AppComboBox"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space12
            AppComboBox {
                model: [
                    { text: "自动（推荐）", value: "auto", secondaryText: "优先用桌面复制，不可用时改用 DirectShow" },
                    { text: "桌面复制（ddagrab）", value: "ddagrab" },
                    { text: "DirectShow", value: "dshow", enabled: false }
                ]
            }
            AppComboBox {
                compact: true
                model: [{ text: "麦克风（默认）", value: "default" }, { text: "麦克风（USB）", value: "usb" }]
            }
            AppComboBox {
                model: [{ text: "被锁定的选择", value: "x" }]
                currentIndex: 0
                locked: true
            }
            AppComboBox { placeholderText: "没有找到麦克风"; model: [] }
        }

        // ---- §2.5.4 switch ----
        Text { text: qsTr("开关 AppSwitch"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space24
            AppSwitch { backendChecked: false }
            AppSwitch { backendChecked: true }
            AppSwitch { backendChecked: true; locked: true }
            AppSwitch { backendChecked: false; enabled: false; disabledReason: "需要先打开托盘图标" }
        }

        // ---- §2.5.5 segmented control ----
        Text { text: qsTr("分段控件 SegmentedControl"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space12
            SegmentedControl {
                model: [
                    { text: "屏幕", value: "screen", iconName: "monitor" },
                    { text: "摄像头", value: "camera", iconName: "camera" }
                ]
                currentValue: "screen"
            }
            SegmentedControl {
                model: [
                    { text: "跟随系统", value: "system" },
                    { text: "深色", value: "dark" },
                    { text: "浅色", value: "light" }
                ]
                currentValue: "dark"
            }
            SegmentedControl {
                model: [{ text: "已锁定", value: "a" }, { text: "B", value: "b" }]
                currentValue: "a"
                locked: true
            }
        }

        // ---- §2.5.6 check / radio / progress ----
        Text { text: qsTr("复选框 / 单选框 / 进度条"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space32
            Column {
                spacing: Theme.space12
                AppCheckBox { text: "未选中" }
                AppCheckBox { text: "已选中"; checked: true }
                AppCheckBox { text: "记住我的选择"; secondaryText: "（可在设置中修改）"; checked: true }
                AppCheckBox { text: "禁用"; enabled: false }
            }
            Column {
                spacing: Theme.space4
                AppRadioButton { text: "最小化到托盘，在后台继续运行（推荐）"; description: "录制不会中断"; checked: true }
                AppRadioButton { text: "退出字录"; description: "正在录制时会先停止并保存" }
            }
            Column {
                spacing: Theme.space12
                AppProgressBar { value: 0.3; width: 200 }
                AppProgressBar { indeterminate: true; width: 200 }
            }
        }

        // ---- §2.5.7 / §2.5.8 recording controls ----
        Text { text: qsTr("录制控制 / 状态胶囊"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space16
            Column { spacing: Theme.space8; AppButton { text: "→ 打开示例对话框"; onClicked: sampleDialog.open() } }
            RecordMainButton { uiState: "idle"; toolTipText: "开始录制（F9）" }
            RecordMainButton { uiState: "preparing" }
            RecordMainButton { uiState: "running" }
            RecordMainButton { uiState: "noDevice" }
            Column {
                spacing: Theme.space8
                RecordPauseButton { uiState: "running"; toolTipText: "暂停（F10）" }
                RecordPauseButton { uiState: "paused" }
                RecordPauseButton { uiState: "idle" }
            }
            Column {
                spacing: Theme.space8
                StatusPill { uiState: "idle" }
                StatusPill { uiState: "noDevice" }
                StatusPill { uiState: "preparing" }
                StatusPill { uiState: "running" }
                StatusPill { uiState: "paused" }
                StatusPill { uiState: "stopping" }
                StatusPill { uiState: "failed" }
                StatusPill { uiState: "running"; text: "录制中 00:12:34"; clickable: true }
            }
        }

        // ---- §3.2.4 subtitle badge ----
        Text { text: qsTr("字幕标签 SubtitleBadge"); font: Theme.h2Font; color: Theme.textPrimary }
        Column {
            spacing: Theme.space8
            Row {
                spacing: Theme.space8
                SubtitleBadge { uiState: "off" }
                SubtitleBadge { uiState: "needModel" }
                SubtitleBadge { uiState: "downloading"; progressPercent: 45 }
            }
            Row {
                spacing: Theme.space8
                SubtitleBadge { uiState: "unavailable" }
                SubtitleBadge { uiState: "ready" }
                SubtitleBadge { uiState: "live" }
            }
            Row {
                spacing: Theme.space8
                SubtitleBadge { uiState: "lagging"; lagSeconds: 132 }
                SubtitleBadge { uiState: "finishing"; progressPercent: 63 }
                SubtitleBadge { uiState: "done" }
                SubtitleBadge { uiState: "failed" }
            }
        }

        // ---- §2.5.9 level meter ----
        Text { text: qsTr("电平条 LevelMeter"); font: Theme.h2Font; color: Theme.textPrimary }
        Column {
            spacing: Theme.space8
            LevelMeter { width: 248; levelDb: -30; active: true }
            LevelMeter { width: 248; levelDb: -8; active: true }
            LevelMeter { width: 248; levelDb: -2; active: true }
            LevelMeter { width: 248; active: false }
            LevelMeter { width: 248; active: true; channelOn: false }
        }

        // ---- §2.5.10 alerts / toast ----
        Text { text: qsTr("提示条 InlineAlert / Toast"); font: Theme.h2Font; color: Theme.textPrimary }
        Column {
            spacing: Theme.space8
            InlineAlert { kind: "info"; title: "准备中"; body: "正在初始化采集设备。" }
            InlineAlert { kind: "success"; title: "录像已保存"; body: "SubRec_20261101_101500.mp4 · 00:01:23" }
            InlineAlert { kind: "warning"; title: "两路都已关闭"; body: "将录制无声视频。"; actionText: "开启系统声音"; closable: false }
            InlineAlert { kind: "danger"; title: "录制失败"; body: "请检查输出路径是否可写。"; closable: true }
            Row {
                spacing: Theme.space8
                AppButton {
                    text: "弹出 Toast"
                    onClicked: Toasts.show("success", "录像已保存", "SubRec_20261101_101500.mp4 · 00:01:23",
                                           [{ id: "gallery.open", text: "打开文件夹" }], 0)
                }
                AppButton {
                    text: "Toast（仅标题）"
                    onClicked: Toasts.show("info", "已复制", "", [], 3000)
                }
            }
        }

        // ---- §2.5.11 card / nav / dialog ----
        Text { text: qsTr("卡片 / 导航项 / 对话框"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space16
            Card {
                width: 300
                height: 140
                title: "卡片标题"
                Column {
                    spacing: Theme.space8
                    anchors.fill: parent
                    Text { text: qsTr("卡片正文 14 px"); font: Theme.bodyFont; color: Theme.textSecondary }
                    Text { text: qsTr("说明 12 px"); font: Theme.captionFont; color: Theme.textTertiary }
                }
            }
            Column {
                spacing: Theme.space4
                NavItem { text: "录制"; iconName: "video"; selected: true; dotColor: Theme.recordDefault }
                NavItem { text: "录像库"; iconName: "library" }
                NavItem { text: "设置"; iconName: "settings" }
                NavItem { text: "录制"; iconName: "video"; collapsed: true; dotColor: Theme.recordDefault }
            }
        }

        // ---- empty state ----
        Text { text: qsTr("空状态 EmptyState"); font: Theme.h2Font; color: Theme.textPrimary }
        Rectangle {
            width: parent.width
            height: 240
            color: Theme.bgPreview
            radius: Theme.radiusMd
            EmptyState {
                anchors.fill: parent
                iconColor: Theme.textTertiary
                iconName: "monitor-off"
                title: "没有找到可用的视频来源"
                body: "请连接显示器或摄像头后刷新。"
                secondaryText: "刷新设备"
                primaryText: "去录制"
                linkText: "复制错误信息"
            }
        }
        Rectangle {
            width: parent.width
            height: 240
            color: Theme.bgPreview
            radius: Theme.radiusMd
            EmptyState {
                anchors.fill: parent
                iconName: "circle-alert"
                iconColor: Theme.dangerFg
                title: "录制失败"
                body: "Set a writable output path and filename in Settings."
                detail: "请在设置中检查保存位置。"
                secondaryText: "刷新设备"
                primaryText: "重试"
                linkText: "复制错误信息"
            }
        }

        // ---- §3.4 settings rows ----
        Text { text: qsTr("设置表单 FormGroupTitle / FormRow"); font: Theme.h2Font; color: Theme.textPrimary }
        Column {
            width: 768
            spacing: 0
            FormGroupTitle { text: "外观" }
            FormRow {
                title: "主题"
                description: "跟随 Windows 的深色或浅色设置，也可以固定一种"
                AppSwitch { backendChecked: true }
            }
            FormRow {
                title: "点关闭按钮时"
                description: "选“最小化到托盘”时，关闭窗口不会停止录制"
                extraText: "需要先打开“在通知区域显示图标”"
                extraKind: "warning"
                AppComboBox {
                    width: 320
                    model: [{ text: "每次询问", value: "ask" }, { text: "最小化到托盘", value: "tray" }, { text: "退出字录", value: "exit" }]
                }
            }
            FormRow {
                title: "保存文件夹"
                description: "录像和字幕都保存在这里"
                wide: true
                showDivider: false
                AppTextField { width: parent.width; text: "D:\\Videos\\SubRec"; mono: true }
            }
        }

        // ---- spinner ----
        Text { text: qsTr("旋转圈 Spinner"); font: Theme.h2Font; color: Theme.textPrimary }
        Row {
            spacing: Theme.space16
            Spinner { size: 16 }
            Spinner { size: 24 }
            Spinner { size: 24; color: Theme.infoFg; running: true }
        }

        // Sample dialog + toast host so PC3 can open both from the gallery.
        AppDialog {
            id: sampleDialog
            title: "关闭字录窗口"
            primaryText: "确定"
            secondaryText: "取消"
            Item {
                width: parent.width
                height: 120
                Column {
                    spacing: Theme.space12
                    AppRadioButton { text: "最小化到托盘，在后台继续运行（推荐）"; description: "录制不会中断"; checked: true }
                    AppRadioButton { text: "退出字录"; description: "正在录制时会先停止并保存" }
                }
            }
            onAccepted: Toasts.show("info", "已选择", "", [], 0)
        }

        ToastHost {
            width: root.width
            height: 320
        }
    }
}
