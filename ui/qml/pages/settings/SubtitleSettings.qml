import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Module U8 (Codex). P3-4 AI 字幕 (UI_REDESIGN §3.5 P3-4), 1104-px tall.
//   intro text; page-level InlineAlert for U2/U4/U10 (SubtitleView.uiState);
//   启用 🔒 AppSwitch <-> SubtitleView.enabled; 识别时机 🔒 two AppRadioButtons <-> mode
//   live/after (disabled when off);
//   识别模型: Repeater of ModelRow over ModelCatalog.models; model note text;
//   字幕格式与性能 🔒: maxCharsPerLine (14/16/18/20/24), threads (1–4), srtBom, keepSpool;
//   模型保存位置: SubtitleView.modelDir + "打开" (SubtitleView.openModelDir()).
// No local-import entry and no cloud options (decided). Locked while
// SubtitleView.settingsLocked; model download/delete stay available (delete per inUse).
// FROZEN API: none.
ColumnLayout {
    id: root
    spacing: Theme.space16

    // TODO(U8): rows.
    Text {
        text: "在本机离线识别语音，停止录制后在视频旁生成同名 .srt 字幕。录音不会上传，只有下载模型时需要联网。"
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
    }
    InlineAlert {
        Layout.fillWidth: true
        visible: SubtitleView.uiState === "needModel" || SubtitleView.uiState === "unavailable" || SubtitleView.uiState === "failed"
        kind: SubtitleView.uiState === "needModel" ? "warning" : "danger"
        title: SubtitleView.uiState === "needModel" ? "还没有下载识别模型" : SubtitleView.uiState === "failed" ? "字幕生成失败" : "字幕服务不可用"
        body: SubtitleView.lastError
    }
    AppButton {
        visible: SubtitleView.uiState === "needModel"
        text: "下载模型"
        onClicked: ModelCatalog.requestDownload(SubtitleView.modelId)
    }
    FormRow {
        title: "启用 AI 字幕"
        description: SubtitleView.serviceAttached ? "录制页面会显示字幕状态" : "字幕服务尚未接入"
        control: AppSwitch {
            backendChecked: SubtitleView.enabled
            enabled: SubtitleView.serviceAttached && !SubtitleView.settingsLocked
            onToggled: function () {
                SubtitleView.enabled = checked;
            }
        }
    }
    FormRow {
        title: "识别时机"
        implicitHeight: 104
        control: ColumnLayout {
            enabled: SubtitleView.enabled && !SubtitleView.settingsLocked
            ButtonGroup {
                id: timingGroup
            }
            AppRadioButton {
                text: "录制时识别（推荐）"
                description: "边录边识别，停止后几秒内生成字幕"
                checked: SubtitleView.mode === "live"
                ButtonGroup.group: timingGroup
                onClicked: SubtitleView.mode = "live"
            }
            AppRadioButton {
                text: "录完后识别"
                description: "停止录制后再识别，适合配置较低的电脑"
                checked: SubtitleView.mode === "after"
                ButtonGroup.group: timingGroup
                onClicked: SubtitleView.mode = "after"
            }
        }
    }

    FormGroupTitle {
        text: "识别模型"
    }
    Repeater {
        model: ModelCatalog.models
        delegate: ModelRow {
            id: modelRow
            required property var model
            Layout.fillWidth: true
            modelId: modelRow.model.modelId
            displayName: modelRow.model.displayName
            tag: modelRow.model.tag
            summary: modelRow.model.summary
            licenseName: modelRow.model.licenseName
            status: modelRow.model.status
            progress: modelRow.model.progress
            bytesDone: modelRow.model.bytesDone
            bytesTotal: modelRow.model.bytesTotal
            errorText: modelRow.model.errorText
            selected: modelRow.model.selected
            inUse: modelRow.model.inUse
        }
    }
    Text {
        Layout.fillWidth: true
        text: "模型只能在线下载，来自模型作者的官方发布地址，下载后自动校验完整性。第一次下载前需要阅读并同意模型许可。"
        color: Theme.textTertiary
        font: Theme.captionFont
        wrapMode: Text.WordWrap
    }
    FormGroupTitle {
        text: "字幕格式与性能"
    }
    FormRow {
        title: "每行最大字数"
        control: AppComboBox {
            enabled: !SubtitleView.settingsLocked
            model: [
                {
                    text: "14",
                    value: 14
                },
                {
                    text: "16",
                    value: 16
                },
                {
                    text: "18",
                    value: 18
                },
                {
                    text: "20",
                    value: 20
                },
                {
                    text: "24",
                    value: 24
                }
            ]
            currentIndex: Math.max(0, [14, 16, 18, 20, 24].indexOf(SubtitleView.maxCharsPerLine))
            onActivated: function (index) {
                SubtitleView.maxCharsPerLine = model[index].value;
            }
        }
    }
    FormRow {
        title: "线程数"
        description: "数值越大识别越快，但会和录制抢 CPU，建议保持 1"
        control: AppComboBox {
            enabled: !SubtitleView.settingsLocked
            model: [
                {
                    text: "1",
                    value: 1
                },
                {
                    text: "2",
                    value: 2
                },
                {
                    text: "3",
                    value: 3
                },
                {
                    text: "4",
                    value: 4
                }
            ]
            currentIndex: Math.max(0, [1, 2, 3, 4].indexOf(SubtitleView.threads))
            onActivated: function (index) {
                SubtitleView.threads = model[index].value;
            }
        }
    }
    FormRow {
        title: "SRT 使用 BOM"
        control: AppSwitch {
            backendChecked: SubtitleView.srtBom
            enabled: !SubtitleView.settingsLocked
            onToggled: function () {
                SubtitleView.srtBom = checked;
            }
        }
    }
    FormRow {
        title: "保留临时音频"
        control: AppSwitch {
            backendChecked: SubtitleView.keepSpool
            enabled: !SubtitleView.settingsLocked
            onToggled: function () {
                SubtitleView.keepSpool = checked;
            }
        }
    }
    FormRow {
        wide: true
        title: "模型保存位置"
        control: RowLayout {
            Text {
                Layout.fillWidth: true
                text: SubtitleView.modelDir
                font: Theme.monoSmallFont
                color: Theme.textSecondary
                elide: Text.ElideMiddle
            }
            AppButton {
                text: "打开"
                onClicked: SubtitleView.openModelDir()
            }
        }
    }
}
