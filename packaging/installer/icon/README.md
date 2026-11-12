# 程序图标来源

程序图标由 AI 图像模型 gpt-image-2.5（经维护者自己的 CLIProxyAPI 调用）于 2026-11-12 生成。AI 生成图像的版权状态可能不明确。
协调方生成 3 个候选后，暂按推荐接入 cand1（维护者审阅后可能更换）；本次只使用提供的文件，没有再次调用模型。
源图 `subrec-icon-source.png` 为暂时接入的候选图 cand1（裁边居中到 1024×1024，未做其它修改），1024×1024 RGBA，透明角。
红色圆角方块与白色粗体“字”呼应 `ui/qml/app/BrandLogo.qml`；提示词指定红色 `#DC2626`。
程序图标不再使用 iconfont；新界面 `ui/icons/*.svg` 的既有来源与许可说明仍然适用。

提示词为：

> Flat minimal app icon for a Windows screen-recording and subtitle app called SubRec (字录). Rounded square tile filled solid red #DC2626, full bleed, no background outside the tile except transparent/white. Bold thick white simplified Chinese character "字" centered. No text other than the character, no gradients, no shadows, no fine details, high contrast, must stay legible at 16x16 pixels.

原提示词文件原样保存在 `prompt_base.txt`。
源图 SHA256：`379598a9fa591801d582cb9e85338ed20e902ccd31713feb97ff086ee2a3b550`。

`../generate-icon.py` 使用 Pillow，从原图分别以 Lanczos 缩放到 256/48/32/16；
16/32 像素的 RGB 使用轻微 UnsharpMask，保持缩放后的 alpha，避免破坏透明圆角。
ICO SHA256：`c8c2101f81550505b279b2fcb2689e3ed57bd134fc7a8ab157efbb78b1f30c5b`。
同一 ICO 用于 exe 的 `IDI_ICON1`、安装器及快捷方式；卸载入口从 exe 读取此图标。
没有修改托盘状态图标或任何窗口代码。
