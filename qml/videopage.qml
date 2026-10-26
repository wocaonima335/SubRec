import QtQuick 2.15

Item {
    id: videopage

    // 控制 Main.qml 中持久化 GLView 的可见性
    property bool showPreview: true

    // videopage 完全透明，让底层 GLView 显示
    // 如需添加 UI 控件，请使用透明背景或调整 z-order
}
