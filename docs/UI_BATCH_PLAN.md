# SubRec（字录）新界面 + 最小化第二批：实现批次计划

> **状态（2026-11-05）：第二批模块与 INT-A/INT-B 已合入 `ui-redesign`，审查中**（`SubRec_docs/UI_BATCH2_REVIEW.md`；必修 M1–M5 已修，S1 已定并已修，见修订记录）。
>
> 最初的框架提交（基于 `e60953a`）只有空壳：
> 全部 C++ 接口类（Q_OBJECT / Q_PROPERTY / 信号槽齐全，函数体返回默认值）、全部 QML 文件（冻结 API 注释 + 最小可加载内容 + `TODO(<模块>)`）、
> 构建登记和接口冒烟测试。没有任何具体模块逻辑。
>
> 依据：
> - `SubRec_docs/UI_REDESIGN.md`（下称"界面稿"），含 2026-11-01 合入的最小化修订。
> - `SubRec_docs/MINIMIZE_DESIGN.md`（下称"最小化稿"）§3.10 第二批：全局热键 + P3-5 快捷键页 + N6、迷你悬浮条、任务栏叠加图标；
>   §5 的 17 个选项（C1–C7、K1–K3、X1–X7）已由用户定为「按推荐」，本文直接按最终取值执行，不再列为待定。
> - 第一批外壳（`shell/**`，S0a–S11）已在 `e60953a` 中；本批在其上增量修改。
>
> **未对照 Penpot 原型**：本次会话 Penpot 连接器未连接，模块拆分和验收标准只依据上面两份文字稿。视觉验收基准见 §6 决定 11。
>
> 本文没有在 Windows 上构建或运行任何代码。本机验证范围见 §5.2。

---

## 0. 约定

- **模块编号**：`U*` 界面、`B*` 后端门面、`H*` 热键、`FB*` 悬浮条、`TB*` 任务栏、`INT-*` 集成。`U3` 未使用（图标并入 U1）。
  注意界面稿 §3.2.4 的字幕界面状态也叫 U1–U10；本文提到字幕状态时一律写"字幕状态 U*"，单写 U* 指模块。
- **负责人**：Codex、ZCode 两人。每个模块只归一人；文件集合互不重叠（§3）。
- **估时口径**：同 `RECORDING_ENGINE_REDESIGN.md` 与最小化稿 §8.6：AI 编码小时数，含单元测试和一轮修复；两人并行时墙钟取较长者。PC3 验收单独计时（§8）。
- **线程**：所有 `subrec::ui`、`subrec::shell` 对象都在 GUI 线程创建和使用。
- **注入方式**（冻结，`ui/backend/singleton_provider.h`）：main.cpp 创建后端对象 → `X::setInstance(&obj)` → `engine.loadFromModule("SubRec.Ui", "AppWindow")`；
  未注入时 QML 单例退回到引擎持有的默认实例，所以每个类在未 attach 时都必须返回文档写明的默认值。单例类不得有默认构造函数（冒烟测试 static_assert）。

---

## 1. 范围

| 包含 | 不包含 |
|---|---|
| 界面稿 P0 外壳、P1 录制页、P2 录像库、P3 设置（7 个子页）、D1–D7、Toast T1–T7、Lucide 图标、深/浅主题 | 录制引擎、编码、混音、采集（`recorder/** encoder/** capture/** thread/**` 不改） |
| 界面稿 §5.3 的代码配合 R1–R6、R8–R10（R6 只留接口） | R7 采集错误文案中文化（引擎侧，见 §6 决定 7） |
| 最小化稿第二批：全局热键（H1–H3）、N5/N6 与托盘第 6 项（H4）、悬浮条（FB1/FB2）、任务栏叠加图标（TB1） | AI 字幕服务本身（`SubtitleService`、模型管理器 E）；本批只做其界面和门面（U8） |
| 新界面接入 `main.cpp`、构建登记（INT-A / INT-B） | 云端识别 F1（界面稿 §3.8，第一阶段不出现） |

---

## 2. 模块清单

"输入"= 该模块读取/依赖的接口；"输出"= 它提供给别人的东西；文件集合见 §3。

### 2.1 后端门面（Codex）

| 模块 | 输入 | 输出 | 依赖 | 验收标准 | 估时 |
|---|---|---|---|---|---|
| **B1** RecorderView | `FFRecorder`（按 QObject 属性名转发，不包含引擎头）；`changeVideoSource` 回调 | QML 单例 `RecorderView`：转发属性、派生 `uiState`（S0–S6 + noDevice）/`capturing`/`selectedVideoKind`/`elapsedText`/`currentOutputFile`；`recordingSaved`/`recordingFailed`（R8） | 框架 | `tests/ui/recorder_view_tests.cpp` 用假 recorder（同 `tests/shell/fakes/observable_recorder.h` 写法）覆盖：全部转发、uiState 七个取值、保存/失败边沿各只发一次、未 attach 默认值；START INVARIANT（无任何 start/retry 方法，冒烟测试已检查） | 3 |
| **B2** ShellView | `WindowController`、`ShellSettings`（第一批 + H4/FB1 新增） | QML 单例 `ShellView`：C1–C7 读写、全部 `request*` 转发、`closeChoiceRequested`/`exitConfirmRequested`/`settingsPageRequested`/`startRejected`/`pageRequested` 转发 | 框架；H4 的新信号（已声明） | 单测：每个属性读写走到 ShellSettings、每个请求走到 WindowController、信号转发；未 attach 时为 §5.1 默认值 | 1.5 |
| **B3** OutputNaming | recorder `outputPath`/`outputFileName`；`openUiSettings()` | `OutputNaming` 单例（R1/R2）；纯函数 `naming::expandTemplate/validateTemplate/uniqueFileName`；C++ `prepareNextFile()` 供 INT-A 在开始回调里调用 | 框架 | 纯函数单测覆盖界面稿 P3-2 全部规则（3 个记号、非法字符原文报错、`_2/_3`、同名 .srt 按 §6 决定 2）；设置持久化；`prepareNextFile` 失败不动 recorder；AutoTest 的 `SUBREC_AUTOTEST_OUTPUT` 不受影响 | 2.5 |
| **B4** AudioLevelMonitor | `FFAudioSampler`（`volumeLevel` 0..100、`isActive`） | `AudioLevelMonitor` 单例：dBFS、≤30 Hz、`perSourceAvailable=false` | 框架 | 单测：0..100→-40..0 dB 换算与 -60 下限、节流、非活动 -60；电平条显示方式按 §6 决定 3 | 1 |

### 2.2 界面（U*）

| 模块 | 负责人 | 输入 | 输出 | 依赖 | 验收标准 | 估时 |
|---|---|---|---|---|---|---|
| **U1** 主题与图标 | ZCode | `ui/theme`；`QStyleHints::colorScheme`；Windows 动画设置 | `Theme.qml` 令牌值（名称已冻结）、`ThemeController`、`AppIcon` + `ui/icons/*.svg`（界面稿 §2.8 全部图标） | 框架 | 跟随系统 / 深 / 浅三种模式实时切换并持久化；§2.8 每个图标名可显示、未知名为空；`reduceMotion` 读到系统设置；单测覆盖 mode 读写与非法值 | 2.5 |
| **U2** 组件库 | ZCode | `Theme`、`AppIcon` | `ui/qml/components/` 下 26 个组件（冻结 API 见各文件头）；`tests/ui/gallery/ComponentGallery.qml` 组件画廊 | U1 | 每个组件按界面稿 §2.5 实现默认/悬停/按下/禁用/锁定/焦点各状态；画廊可离屏加载（加进冒烟测试）且供 PC3 截图对照；qmllint 0 警告 | 8 |
| **U4** 主窗口外壳 | ZCode | `ShellView`、`AppPreferences`、`Navigation`、`Toasts` | `AppWindow`（无边框、拉伸、最大化、最小 960×640、记住几何 R3）、标题栏、导航栏、窗口内快捷键 F9/F10/Ctrl+1/2/3（R9）、D1/D7、Toast 宿主、T1/T4/T7 与"录制结束后"动作 | U2、B2 | 界面稿 §3.1、§2.7；最小化稿 §3.4 决策表每行走查（D7 记住选择、D1a–D1d）；main.cpp/AutoTest 契约不变（冒烟测试：8 个根信号、`handleSettingItemClick`、`previewView`、`folderDialog`）；`AppPreferences` 单测 | 5 |
| **U5** 录制页 | ZCode | `RecorderView`、`ShellView`、`OutputNaming`、`AudioLevelMonitor`、`SubtitleView`、`RecordingLibrary.recent`、`HotkeyController.shortcutLabel` | P1 全部区域（预览、控制栏、来源面板、最近录制） | U2、B1、B3、B4（其余单例未实现时用默认值） | 界面稿 §3.2.3 七个状态逐一走查；§3.2.5 锁定态；唯一 `previewView`；ToolTip 含全局热键文字；字幕区按 §6 决定 9 | 6 |
| **U6** 录像库 | Codex | `RecordingLibrary`、`DesktopActions`、`RecorderView.currentOutputFile`、`OutputNaming.outputDir` | P2 页、右键菜单、D5、`DialogRequests` 对话框总线、`ContentDialogs` 宿主 | U2、B1、B3 | 界面稿 §3.3 L1–L5；`moveToTrash`（R10）；录制中的文件不可播放/删除；1000 个文件的文件夹扫描不阻塞 GUI（§6 决定 4）；模型排序/过滤/limit 单测 | 6 |
| **U7** 设置页 | Codex | `ShellView`、`AppPreferences`、`ThemeController`、`OutputNaming`、`RecorderView`（设备、后端）、`AppInfo` | P3 框架（7 项子导航）+ 常规、录制与输出、音频与设备、高级、关于 | U2、B2、B3、U1/U4 的单例（已有空壳即可开工） | 最小化稿 §6.4.2 P3-1 全部项双向绑定并持久化（C1–C7、主题、录制结束后）；C6/C5 在旧系统禁用；`folderDialog` objectName 保留；录制中锁定提示 | 5 |
| **U8** AI 字幕界面 | Codex | `SubtitleView`、`ModelCatalog`（服务未接入） | P3-4 子页、`ModelRow`、D2/D3/D4、P1 字幕区所需数据 | U2、U7 框架 | 未接服务时按 §6 决定 9 隐藏；用假服务对象的单测覆盖字幕状态 U1–U10 推导、`asr/*` 默认值（18 字、1 线程、BOM 开）、D2→D3→下载 的信号链 | 5 |

### 2.3 最小化第二批（H* / FB* / TB*）

| 模块 | 负责人 | 输入 | 输出 | 依赖 | 验收标准 | 估时 |
|---|---|---|---|---|---|---|
| **H1** KeyChord | Codex | — | `shell/key_chord.h` 纯函数：解析/存储/显示/紧凑文本/校验（最小化稿 §4.3 第 5–6 条） | 框架 | `tests/ui/key_chord_tests.cpp` 覆盖 §4.3 第 5 条全部规则（需 Ctrl/Alt、禁 Win/F12、单独功能键提示）和 K1 默认值往返 | 1.5 |
| **H2** GlobalHotkeys | ZCode | `KeyChord` | `RegisterHotKey`（隐藏消息窗口、`MOD_NOREPEAT`、WM_HOTKEY 排队到 Qt 事件循环、每动作 500 ms 去抖、suspend/resume 计数） | H1 头文件 | 非 Windows：全部 `Unsupported`、`Unset` 规则单测；Windows：PC3 V8–V10；`<windows.h>` 只出现在 `global_hotkeys.cpp`，用 `#ifdef Q_OS_WIN` 隔离 | 2.5 |
| **H3** 热键设置 | Codex | `GlobalHotkeys`、`KeyChord`、`openUiSettings()`、主 `QQuickWindow` | `HotkeyController` 单例、P3-5 页、`KeySequenceField`、`KeyCap` | H1、H2（接口）、U2、U7 框架 | 最小化稿 §6.5 九个状态；`hotkey/*` 持久化与 K1 默认值；录入时挂起全部热键、Esc 取消、Backspace 清除、失焦取消；重复/无效/冲突各自的 K-* 文案；`startupConflicts()` | 4 |
| **H4** 外壳第二批接线 | Codex | H3 的快捷键文字、FB1 的显示状态 | `WindowController`：N5（`startRejected`）、N6（`onHotkeyConflicts`）、`pageRequested`（N2→录像库、N3 失败→字幕设置、N6→快捷键页）；`TrayController`：第 6 项"显示悬浮条"、菜单快捷键文字；`shell_logic` 对应纯函数 | H1、FB1 | 扩展 `tests/shell`：拒绝开始时可见/隐藏两条路径、N6 只在 `notificationAllowed` 时发、托盘第 6 项只在录制态可见；第一批现有测试不回归 | 2.5 |
| **FB1** 悬浮条策略 | Codex | `ShellSnapshot`、`ShellSettings` | `floatingBarShouldShow()` 纯函数、`FloatingBarPolicy`；`ShellSettings` 的 C5 `ui/floatingBar`、`ui/barHintShown`、`ui/floatingBarPos/*` 持久化 | 框架 | `tests/ui/floating_bar_policy_tests.cpp`：状态 × C5 × 窗口可见性 × 托盘覆盖 × FB-5/FB-6 全表；旧系统默认 `never`；`tests/shell/shell_settings_tests.cpp` 补 C5 默认值/非法值 | 2 |
| **FB2** 悬浮条窗口 | ZCode | `FloatingBarPolicy`、`CaptureExclusion::addWindow`、`WindowController` 动作 | `FloatingBarView` 单例、`FloatingBar.qml`（FB-1…FB-6、FB-D、FB-H） | FB1、U2 | 首次显示前已交给 `CaptureExclusion`（最小化稿 §4.4 第 4 条）；按屏记位置、24 px 吸边；FB-H 只出现一次；PC3 V1–V3（悬浮条部分）与 V15 | 4 |
| **TB1** 任务栏叠加图标 | ZCode | `TrayIconKind`（`trayIconFor`）、`tray_icon_factory` | `TaskbarOverlay`（`ITaskbarList3::SetOverlayIcon`） | 框架 | 非 Windows 为空操作（单测）；PC3：五种状态图标、HWND 重建与 Explorer 重启后恢复；Win32/COM 代码只在 `taskbar_overlay.cpp` 且 `#ifdef Q_OS_WIN` | 1.5 |

### 2.4 集成

| 模块 | 负责人 | 输入 | 输出 | 依赖 | 验收标准 | 估时 |
|---|---|---|---|---|---|---|
| **INT-B** 构建登记 | Codex | — | 顶层 `CMakeLists.txt`：`SUBREC_BUILD_NEW_UI=ON` 时 `appSubRec` 链接 `ui_moduleplugin`、定义 `SUBREC_HAS_NEW_UI`；`tests/ui` 并入 `SUBREC_BUILD_LIFECYCLE_TESTS`；CI 登记 | 框架 | 两种构建预设（ON/OFF）都能编过；OFF 时不编、不加载 SubRec.Ui，不编第二批专属源文件，目标列表和编译参数与 `e60953a` 相同。但 `shell_module` 含 H4/FB1 的无门控增量，产物不是逐字节相同；唯一可观察的差异是窗口隐藏时开始被同步拒绝会多一条 N5（第一批时这种情况没有任何提示；用户 2026-11-05 接受，审查 M5 (a)）；ctest 含 `ui` 标签 | 1 |
| **INT-A** main.cpp 接入 | ZCode | 全部后端类的 `attach`/`setInstance`（空壳已有，可提前开工） | 创建并注入全部单例；`Q_IMPORT_QML_PLUGIN(SubRec_UiPlugin)`；`loadFromModule`；开始回调里先 `OutputNaming::prepareNextFile()`；接 `GlobalHotkeys`/`HotkeyController`/`FloatingBarPolicy`/`FloatingBarView`/`TaskbarOverlay`；AutoTest 调 `setUiSettingsIniPath()` 且不建热键；新旧界面切换按 §6 决定 1 | INT-B；合入前 B1–B3、U4 已合入 | 程序以新界面启动；AutoTest 现有脚本不回归；START INVARIANT 仍只有一条路径；旧界面仍可选（决定 1 推荐值） | 3 |

---

## 3. 分工与文件归属

路径相对仓库根。每个文件只属于一个模块；同一负责人的多个模块共用自己的登记文件（`ui/files_<人>.cmake`、`tests/ui/tests_<人>.cmake`）。

### 3.1 Codex（35 h）

| 模块 | 文件集合 |
|---|---|
| B1 | `ui/backend/recorder_view.h/.cpp`；`tests/ui/recorder_view_tests.cpp` |
| B2 | `ui/backend/shell_view.h/.cpp`；`tests/ui/shell_view_tests.cpp` |
| B3 | `ui/backend/output_naming.h/.cpp`、`ui/backend/output_naming_logic.h/.cpp`；`tests/ui/output_naming_tests.cpp` |
| B4 | `ui/backend/audio_level_monitor.h/.cpp`；`tests/ui/audio_level_monitor_tests.cpp` |
| U6 | `ui/qml/pages/library/{LibraryPage,RecordingTableHeader,RecordingRow,RecordingContextMenu}.qml`、`ui/qml/dialogs/{DeleteRecordingDialog,DialogRequests,ContentDialogs}.qml`、`ui/backend/recording_library.h/.cpp`、`ui/backend/desktop_actions.h/.cpp`；`tests/ui/recording_library_tests.cpp` |
| U7 | `ui/qml/pages/settings/{SettingsPage,GeneralSettings,OutputSettings,DeviceSettings,AdvancedSettings,AboutSettings}.qml`、`ui/backend/app_info.h/.cpp` |
| U8 | `ui/qml/pages/settings/{SubtitleSettings,ModelRow}.qml`、`ui/qml/dialogs/{ModelDownloadPromptDialog,ModelLicenseDialog,DeleteModelDialog}.qml`、`ui/backend/subtitle_view.h/.cpp`、`ui/backend/model_catalog.h/.cpp`；`tests/ui/subtitle_view_tests.cpp` |
| H1 | `shell/key_chord.h/.cpp`；`tests/ui/key_chord_tests.cpp` |
| H3 | `ui/qml/pages/settings/HotkeySettings.qml`、`ui/qml/components/{KeySequenceField,KeyCap}.qml`、`ui/backend/hotkey_controller.h/.cpp`；`tests/ui/hotkey_controller_tests.cpp` |
| H4 | `shell/window_controller.h/.cpp`、`shell/tray_controller.h/.cpp`、`shell/shell_logic.h/.cpp`、`shell/shell_types.h`、`tests/shell/fakes/tray_controller.cpp`、`tests/shell/{window_controller,shell_logic,tray_menu_model}_tests.cpp` |
| FB1 | `shell/floating_bar_policy.h/.cpp`、`shell/shell_settings.h/.cpp`、`tests/shell/shell_settings_tests.cpp`；`tests/ui/floating_bar_policy_tests.cpp` |
| INT-B | 顶层 `CMakeLists.txt`、CI 配置 |
| 登记文件 | `ui/files_codex.cmake`、`tests/ui/tests_codex.cmake` |

### 3.2 ZCode（32.5 h）

| 模块 | 文件集合 |
|---|---|
| U1 | `ui/qml/theme/Theme.qml`（只改值）、`ui/qml/components/AppIcon.qml`、`ui/icons/*.svg`（新）、`ui/backend/theme_controller.h/.cpp`；`tests/ui/theme_controller_tests.cpp` |
| U2 | `ui/qml/components/` 下除 `AppIcon`、`KeySequenceField`、`KeyCap` 外的 26 个文件（清单见 `ui/files_zcode.cmake` "U2"段）；`tests/ui/gallery/ComponentGallery.qml`（新） |
| U4 | `ui/qml/app/{AppWindow,Navigation,TitleBar,NavRail,WindowResizeFrame,BrandLogo}.qml`、`ui/qml/dialogs/{ShellDialogs,CloseWindowDialog,QuitConfirmDialog}.qml`、`ui/backend/app_preferences.h/.cpp`；`tests/ui/app_preferences_tests.cpp` |
| U5 | `ui/qml/pages/record/{RecordPage,PreviewPanel,PreviewSurface,ControlBar,SourcePanel,RecentRecordings,RecordingCard}.qml` |
| FB2 | `ui/qml/floating/FloatingBar.qml`、`ui/backend/floating_bar_view.h/.cpp` |
| H2 | `shell/global_hotkeys.h/.cpp`；`tests/ui/global_hotkeys_tests.cpp` |
| TB1 | `shell/taskbar_overlay.h/.cpp`；`tests/ui/taskbar_overlay_tests.cpp` |
| INT-A | `main.cpp` |
| 登记文件 | `ui/files_zcode.cmake`、`tests/ui/tests_zcode.cmake` |

### 3.3 归属理由

与第一批（最小化稿 §8.2）一致：Codex 拿平台无关、可在 Linux 单测的逻辑与门面（B*、H1、FB1、H3、H4，以及数据驱动的 U6–U8 页面）和它已归属的第一批 `shell/` 逻辑文件、顶层 CMake；ZCode 拿 Win32 相关（H2、TB1、FB2 的排除注册）、它已归属的 `main.cpp` 与 QML 视觉基础（U1/U2/U4/U5），以及真机验收。U8 归 Codex 是因为它与 U7 共用设置页框架、与 AI 字幕门面同属数据层；两边小时数大致平衡（35 / 32.5）。

---

## 4. 冻结 / 共享文件与修改规则

| 文件 | 状态 | 规则 |
|---|---|---|
| `ui/CMakeLists.txt` | 冻结 | 只能双方同意后改；需要加文件一律改各自的 `ui/files_<人>.cmake` |
| `ui/backend/singleton_provider.h`、`ui/backend/settings_store.h/.cpp` | 冻结 | 同上 |
| `tests/ui/CMakeLists.txt`、`tests/ui/ui_interface_smoke.cpp` | 共享 | 新测试登记在 `tests/ui/tests_<人>.cmake`。冒烟测试只在**改契约的同一个提交里**由改契约的人更新，并在提交说明写明对方已同意 |
| `shell/CMakeLists.txt`（第二批 `SUBREC_BUILD_NEW_UI` 段） | 冻结 | 第二批文件已全部登记，不再改 |
| 顶层 `CMakeLists.txt` | Codex | 只有 INT-B 改；ZCode 需要时向 Codex 提 |
| C++ 后端头文件里的 Q_PROPERTY / Q_INVOKABLE / 信号名 | 冻结名称 | 负责人可以**新增**成员和私有实现细节；改名、删除、改参数需双方同意并同步冒烟测试（`checkContracts()` 会失败）。START INVARIANT：`RecorderView` 不得出现 start/retry 入口 |
| 各 QML 文件头的 `FROZEN API` 注释块 | 冻结 | 文件体由负责人自由实现；API 块（属性、信号、函数名）同上规则。页面之间只经 `Navigation`、`Toasts`、`DialogRequests` 单例通信，不跨文件引用 id |
| `ui/qml/theme/Theme.qml` | 名称冻结 | 令牌名冻结；值只由 U1 调整；其他文件不得硬编码颜色/字号/圆角/时长 |
| `ui/qml/app/AppWindow.qml` 的 main.cpp 契约（8 个根信号、`handleSettingItemClick`、`initialVisible`） | 冻结 | INT-A 依赖；改动需双方同意 |
| `shell/shell_types.h` | Codex（H4） | ZCode 的 TB1/FB2 只读；新增枚举值需通知 ZCode |
| 旧界面 `qml/**`、`main.cpp` 中旧界面路径 | ZCode | 本批除 INT-A 外不改旧界面 |
| `recorder/** encoder/** capture/** thread/** opengl/**` | 不在本批 | 不改；需要引擎配合的写进 §6 |
| `docs/UI_BATCH_PLAN.md` | 共享 | 只由协调人（用户）或经用户同意后修改 |

QML 文件在模块里被拍平到同一目录（`ui/CMakeLists.txt` 用 `QT_RESOURCE_ALIAS`），**文件基名在两份清单之间必须唯一**，重名时 CMake 直接报错。

---

## 5. 构建、测试与本次验证

### 5.1 独立测试工程 `tests/ui`

```
cmake -S tests/ui -B build-ui -DCMAKE_PREFIX_PATH=<Qt>/6.8.0/<kit>
cmake --build build-ui && ctest --test-dir build-ui && cmake --build build-ui --target all_qmllint
```

- 不需要 FFmpeg 和录制引擎。编译整个 `SubRec.Ui` 模块（moc、qmltyperegistrar、qmlcachegen）、第二批 shell 单元（`ui_test_shell_batch2`，含它们用到的第一批纯逻辑单元 `shell_logic`、`shell_settings`、`tray_icon_factory`），
  以及一个**测试替身版 `shell_module`**（第一批 `window_controller`/`tray_controller`/`shell_state_source`/`capture_exclusion`/`power_request`，用 `tests/shell` 的假 recorder 和预览替身），
  这样 B2/H3/FB2 实现后调用 shell 类时 `tests/ui` 仍能链接，不用改共享的 CMake 文件。顶层构建里已有真 `shell_module` 时不建替身。
- `ui_interface_smoke`：契约（全部属性/方法/信号名）、默认值、注入实例即 QML 单例、`AppWindow` 离屏加载并保持 main.cpp/AutoTest 契约、第二批 shell 空壳可链接、第一批类上的第二批新增成员存在。
- 各模块的单测按 §3 文件名放进 `tests/ui/`，在 `tests_<人>.cmake` 登记（`add_executable` + `add_test(... LABELS "ui")`）。

### 5.2 本次验证记录（2026-11-04，本机 Linux）

工具：Qt 6.8.0 gcc_64（`/tmp/qtl/6.8.0/gcc_64`）、GCC 14.2、CMake 3.31、Ninja。本机没有 OpenGL 头文件，Qt6Gui 的 CMake 包找不到 `WrapOpenGL`，在 `$TMPDIR` 放了一个只含 GL 类型定义的桩头文件并指向系统 `libGL.so.1`（SubRec.Ui 不调用 OpenGL）。

| 项 | 结果 |
|---|---|
| 独立 CMake 工程（`$TMPDIR/uiv/proj`，`add_subdirectory` 引入 `tests/ui` 与 `tests/shell`），`-Wall -Wextra` 全量构建 | 通过，**0 警告 0 错误**。moc 处理 23 个 Q_OBJECT 头（`ui/backend` 13 个，其中 2 个同时含列表模型；第二批 shell 3 个：`global_hotkeys`、`floating_bar_policy`、`taskbar_overlay`；第一批 shell 6 个；假 recorder 1 个），qmltyperegistrar 注册 15 个类型 |
| `tests/ui` 按 §5.1 单独配置构建 | 通过；`ctest` 1/1 |
| `ui_interface_smoke`（offscreen + software） | 全部检查通过 |
| `tests/shell` | 23/24 通过。`single_instance` 失败：本机沙箱禁止 Unix 域套接字（`QLocalServer::listen: Unknown error 1`），与本批无关（`single_instance.*` 未改动） |
| qmllint 6.8.0：`all_qmllint` 目标；另外手工对 `ui/qml` 下全部 66 个文件以模块资源映射运行 `--max-warnings 0` | 66/66，**0 警告**，退出码 0。（直接在源码目录不带 `--resource` 运行会报跨子目录类型找不到，这是拍平目录布局所致，不是 QML 错误） |
| 顶层工程 `-DSUBREC_BUILD_NEW_UI=ON` 配置（FFMPEG_ROOT 指向本机 Linux FFmpeg 8 头文件 + 空 bin 占位） | 配置通过：`add_subdirectory(ui)` 与 `shell_module` 第二批 `target_sources` 生效 |
| 同上，构建 `shell_module` | 14 个目标文件中 13 个编译通过（含第二批 4 个 `.cpp`、修改过的 `shell_settings.cpp`/`tray_controller.cpp`、真 `window_controller.cpp` 与 `opengl/ffglitem.h`、moc 汇总）；`shell_state_source.cpp` 失败：引擎头 `queue/ffboundedqueue.h` 需要 Windows CRT 的 `corecrt.h`（`e60953a` 原有情况，Windows 专用）。因此顶层配置下没有编到 `ui_module`（Ninja 依赖顺序在 `shell_module` 之后），`ui_module` 的编译以独立工程结果为准 |

**未验证**：任何 Windows 构建（MinGW 13.1 / MSVC）、Windows 运行、`appSubRec` 链接、PC 真机行为。第二批空壳里目前没有 Win32 代码（H2/TB1 只有 TODO），所以暂无需要宏隔离的内容；规则是 Win32/COM 代码只能写在对应 `.cpp` 里并用 `#ifdef Q_OS_WIN` 包住，头文件保持平台无关（H2、TB1 验收标准已写入）。

> 注（2026-11-05）：本节是框架提交时的记录。之后 H2/TB1 已加入 Win32/COM 代码，INT-A/INT-B 已合入；OFF 构建与 `e60953a` 的差异按 §2.4 INT-B 验收（审查 M5）。第二批合入后的验证见 `SubRec_docs/UI_BATCH2_REVIEW.md`。

---

## 6. 用户决定项（2026-11-04 已定：全部 14 项按推荐值）

> 用户原话：「批准：UI_BATCH_PLAN 的 14 项全部按推荐值」。下表「状态」列均为已定、按推荐。

| # | 问题 | 选项 | 推荐 | 状态 |
|---|---|---|---|---|
| 1 | 新旧界面如何切换 | (a) INT-A 直接替换旧界面；(b) 并存一个版本周期 | **(b)**：`SUBREC_BUILD_NEW_UI=ON` 的构建默认加载新界面，命令行 `--old-ui` 回到旧 `qml/Main.qml`；PC3 验收通过后的下一个版本删除旧界面（单独提交） | 已定、按推荐 |
| 2 | 文件名查重时同名 `.srt` 是否算"已占用"（R1） | 算 / 不算 | **算**：调用方同时检查 `.mp4` 与 `.srt`，避免新录像继承旧字幕 | 已定、按推荐 |
| 3 | 引擎只给一路混合电平，P1 系统声音/麦克风两条电平条怎么显示 | 一条共享电平条 / 两条显示同一值 / 等引擎出分路电平 | **一条共享电平条**，放在音频组顶部；分路电平作为以后的引擎任务 | 已定、按推荐 |
| 4 | 录像库扫描放在哪个线程 | GUI 线程 / 工作线程 | **工作线程扫描、GUI 线程发布**（大文件夹不卡界面） | 已定、按推荐 |
| 5 | 图标来源 | Lucide SVG / 图标字体 / 继续用现有位图 | **Lucide SVG**（ISC 许可），打包为资源，在 `THIRD_PARTY_LICENSES.md` 登记 | 已定、按推荐 |
| 6 | 是否打包字体 | 打包 / 用系统字体栈 | **不打包**，按界面稿 §2.2 字体栈 | 已定、按推荐 |
| 7 | R7 采集错误文案中文化 | 本批做（改 `recorder/**`）/ 另开引擎任务 | **另开任务**（引擎归 Codex 引擎线）；本批界面显示中文标题 + 原文 + "复制错误信息" | 已定、按推荐 |
| 8 | 窄窗口（< 1100）录制页右侧面板 | 折叠抽屉 / 收窄 / 移到预览下方 | **收窄到 280 px，不做抽屉**；960 宽效果在 PC3 看过后再定（界面稿 §5.4 第 3 条） | 已定、按推荐 |
| 9 | AI 字幕服务未接入本分支时，字幕相关界面 | 隐藏 / 显示"不可用" | **隐藏**：P1 隐藏字幕区和控制栏字幕标签，P3 隐藏"AI 字幕"子页（`SubtitleView.serviceAttached=false`）；U8 代码照常合入，接入服务后自动出现 | 已定、按推荐 |
| 10 | 浅色主题是否本批验收 | 只验深色 / 深浅都验 | **深浅都验**（默认跟随系统，浅色系统用户首次就看到浅色） | 已定、按推荐 |
| 11 | 视觉验收基准 | 文字稿 / Penpot 原型 | **以两份文字稿为准**；Penpot 连接后加一轮原型对照（约 2 h，单独计时，不在 §8 内） | 已定、按推荐 |
| 12 | 热键冲突路径怎么测 | 给 `GlobalHotkeys` 抽接口做假实现 / 只靠真机 | **不抽接口**：H3 单测覆盖 Unsupported 与持久化路径，冲突路径靠 PC3 V9 | 已定、按推荐 |
| 13 | D3 是否记录"同意的许可版本"（界面稿 §5.4 第 2 条） | 记录 / 只记模型 | **记录**（模型清单有版本字段时）；U8 先按模型 ID 记录，版本字段待模型管理器 E | 已定、按推荐 |
| 14 | 分支与合入目标 | — | 两人各从框架提交开 `ui-codex` / `ui-zcode`，按 §7 顺序合入 `ui-redesign`；P2/P3 进 `main` 后 `ui-redesign` 变基一次再进 `main` | 已定、按推荐 |

---

## 7. 合并顺序

文件集合不相交，按下面顺序合入 `ui-redesign` 不会产生文本冲突；"→"表示前者合入后后者才能合入（开发可以并行，只是合入有先后）。

| 步 | Codex | ZCode | 说明 |
|---|---|---|---|
| 0 | 框架提交（本提交） | | 双方从这里开分支 |
| 1 | H1、FB1、B1、B2、B3、B4 | U1 → U2 | 门面先行：页面开发期间就能拿到真实数据；U2 是所有页面的前提 |
| 2 | H4（需 H1、FB1）、INT-B | H2、TB1、U4（需 U2、B2） | INT-B 只登记，不改默认值 |
| 3 | H3（需 H1、U2；H2 只需接口）、U6、U7（需 U2） | U5（需 U2、B1、B3、B4）、FB2（需 FB1、U2） | U5 的最近录制区在 U6 合入前显示空状态 |
| 4 | U8（需 U7） | INT-A（需 INT-B、B1–B3、U4） | INT-A 合入后程序才真正用上新界面 |
| 5 | 联调 + 修复（两人） | | 按界面稿 §3、最小化稿 §3.4/§3.3/§6.5 表格逐行走查 |
| 6 | | PC3 验收 | §8 |

---

## 8. 估时汇总

| 负责人 | 模块（h） | 合计 |
|---|---|---|
| Codex | B1 3、B2 1.5、B3 2.5、B4 1、U6 6、U7 5、U8 5、H1 1.5、H3 4、H4 2.5、FB1 2、INT-B 1 | **35 h** |
| ZCode | U1 2.5、U2 8、U4 5、U5 6、FB2 4、H2 2.5、TB1 1.5、INT-A 3 | **32.5 h** |
| 两人 | 联调 3 h、修复 3 h | 6 h（墙钟） |
| **编码墙钟** | max(35, 32.5) + 6 | **约 41 h** |

**PC3 验收（单独计时，ZCode 按现行约定执行）**

| 项 | 机时 |
|---|---|
| 构建部署 | 0.5 h |
| 视觉走查：P0–P3、D1–D7、Toast，深/浅 × 100%/150%（界面稿 §5.4 第 4 条） | 2.5 h |
| 录制页 S0–S6、录像库 L1–L5、字幕隐藏态（决定 9） | 1 h |
| 第二批真机：V8–V10 热键、V1–V3 中悬浮条排除部分、V15 悬浮条多屏/DPI、任务栏叠加图标与 Explorer 重启 | 1.5 h |
| AutoTest 回归（`tools/*.ps1`：`previewView`、`folderDialog`、`handleSettingItemClick`） | 0.5 h |
| 小计（机时） | **6 h** |
| 结果审核与结论写回文档 | 1.5 h |
| **PC3 合计** | **7.5 h** |

不含：Penpot 原型对照（决定 11，约 2 h）、AI 字幕服务接入后的字幕状态 U2–U10 验收。

---

## 9. 风险与未核实

1. **Penpot 原型未对照**（连接器未连接）：组件尺寸和页面布局只按文字稿；原型若与文字稿不一致，会在 U2/U4/U5 返工。
2. 本机没有任何 Windows 构建；MinGW 13.1 下 `ITaskbarList3`、`RegisterHotKey` 相关头文件/宏是否齐全**未核实**（H2/TB1 开工时先做一次 MinGW `-fsyntax-only`）。
3. `HotkeyController` 读 `QKeyEvent::nativeVirtualKey()` 在 Qt 6.8 Windows 上的取值**未核实**（H3 首日在 PC 上确认）。
4. 悬浮条的捕获排除对 DirectShow 后端**未核实**（最小化稿 V3），C5 说明文案可能要按结果改。
5. `tests/ui` 的替身 `shell_module` 用的是假 recorder 与预览替身，覆盖不到真 `FFRecorder` 信号；真集成只能在 INT-A 后的 Windows 构建里看到。

---

## 修订记录

| 日期 | 修订 |
|---|---|
| 2026-11-04 | 初稿：随框架提交。模块清单（B1–B4、U1/U2/U4–U8、H1–H4、FB1/FB2、TB1、INT-A/INT-B）、Codex/ZCode 分工与文件归属、冻结/共享文件规则、本机验证记录、14 项待决定事项、合并顺序、估时（编码墙钟约 41 h，PC3 7.5 h）。未对照 Penpot 原型 |
| 2026-11-05 | 第二批审查（UI_BATCH2_REVIEW.md）后：状态行更新；INT-B 验收改为承认 OFF 下 `shell_module` 的无门控增量（同步拒绝时的 N5，用户接受 M5 (a)）；§5.2 加注说明其为框架时的记录 |
| 2026-11-05 | 审查 S1 **已定**（用户决定）：没有可用的视频来源时 F9 / 全局热键 / 托盘 / 录制页一律由外壳层（`WindowController::requestStart`）拒绝，不调用引擎、不占文件名、不进入出错；可见 Toast（`startRejected`）、不可见 N5。H4 的"拒绝开始时可见/隐藏两条路径"测试按此覆盖 NoDevice。最小化稿 §3.9 已同步 |

