import QtQuick

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
Column {
    id: root
    spacing: 0

    // TODO(U8): rows.
    FormGroupTitle { text: "字幕" }
}
