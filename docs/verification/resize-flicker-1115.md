# PC3 Win10 上下边缘 resize 闪烁诊断（修改前）

基线：`resize-flicker-1115` / `1ec866e`。症状来自用户转述的视频观察，
本环境没有 PC3、原始视频、Windows 运行环境；禁止联网。

## 仓库证据（以下行号为修改前）

- `git show 0ac0a5a`：改动只有 RecordPage 的 clip / StopAtBounds / ScrollBar、
  标题栏 z=1、outline z=2、resize frame z=3 以及结构测试。没有 DWM、原生消息或
  render-loop 修复。移动窗口通过是用户实测结果，不能据此认定该提交修过原生移动循环。
- `ui/qml/app/AppWindow.qml:23-30`：最小尺寸来自 Theme，背景 `color: Theme.bgApp`，
  flags 为 `Qt.Window | Qt.FramelessWindowHint`。
  `ui/qml/theme/Theme.qml:11` 的两个颜色都完全不透明；不存在透明主窗口的设置。
- `ui/qml/app/WindowResizeFrame.qml:12-75`：四边和四角的 MouseArea 均在按下时调用
  `window.startSystemResize(edges)`。没有手动 geometry 更新或 resize 会话管理。
- `ui/qml/app/TitleBar.qml:19-27`：标题栏是客户区 Rectangle，移动调用
  `startSystemMove()`；不是 Windows 绘制的非客户区 caption。
- `ui/qml/app/AppWindow.qml:191-222`：标题栏层级高于页面；页面顶部锚定标题栏底部、
  底部锚定窗口。没有 resize 时隐藏标题栏或禁用布局的代码。
- `shell/window_controller.cpp:218-243`：attach 只观察可见性/几何、管理捕获排除，
  不介入 resize；对应头文件也没有原生消息处理入口。
- `main.cpp:984-991` 设置 PassThrough DPI 并创建 QApplication；`:1206` 明确选择
  `QSGRendererInterface::OpenGLRhi`。仓库没有设置 QSG_RENDER_LOOP / QSG_RHI_BACKEND，
  没有主窗口 WM_NCCALCSIZE、WM_NCHITTEST、WM_ERASEBKGND、WM_SIZING、
  WM_ENTERSIZEMOVE 的拦截，也没有 DwmExtendFrameIntoClientArea / DwmFlush。
  外部环境是否覆盖 Qt 选择未采集。
- `opengl/ffglitem.h:56`：真实预览继承 QQuickFramebufferObject，依赖 OpenGL。
  直接换 D3D11 不属于本次可安全采用的局部修复。

## Qt / Windows 机制及证据边界

1. Qt 公共 API 文档 `QWindow::startSystemResize(Qt::Edges)` 将交互式 resize 委托
   给窗口系统。Qt Windows 实现应查 Qt 6.8.0 的
   `qtbase/src/plugins/platforms/windows/qwindowswindow.cpp`
   （`QWindowsWindow::startSystemResize` / `startSystemMoveResize`）；消息分发应查
   `qtbase/src/plugins/platforms/windows/qwindowscontext.cpp`。
   本机只找到 Qt 6.8.0 头文件/二进制，没有这些 .cpp，以上是复核路径，不是本次阅读过的源码。
2. Windows 的 SC_SIZE / SC_MOVE 交互进入系统模态循环；WM_ENTERSIZEMOVE /
   WM_SIZING / WM_EXITSIZEMOVE 描述这个生命周期。Windows 的 WM_NCCALCSIZE
   决定客户区和有效区域保留，WM_NCHITTEST 决定边缘/标题命中，WM_ERASEBKGND
   处理背景擦除。这些消息由 Qt 平台插件及默认窗口过程负责，仓库没有自行处理。
   依据是 Microsoft 对上述消息及 WM_SYSCOMMAND 的 API 契约；本次没有联网复核。
3. Qt Quick 的 QML 布局/polish、场景图同步和绘制不是一次 Windows geometry 变更
   中的同一原子操作。threaded loop 的 GUI 线程仍须参与 polish/sync；换成 basic
   也不能从原理上保证原生模态循环内每次 geometry 都有完成的新帧。
   参考 Qt 文档 “Qt Quick Scene Graph / Scene Graph and Rendering”，源码复核路径
   `qtdeclarative/src/quick/scenegraph/qsgthreadedrenderloop.cpp`、
   `qsgrenderloop.cpp`。实际 loop 应通过 QSG_INFO 日志确认，不能仅由 OS 版本断定。
4. 当前程序选用 OpenGL RHI，不能将现象断言为 D3D11 `ResizeBuffers` 故障。
   OpenGL 同样涉及窗口 drawable 尺寸、场景图 render target 和交换呈现；对应实现
   复核路径是 `qtbase/src/gui/rhi/qrhigles2.cpp` 以及 Windows OpenGL 平台实现。
   resize 时旧帧被保留/裁切、扩展区域未及时呈现，能解释内容错位和壁纸/黑色空洞，
   但这条具体因果链仍是**推测**，没有 GPU/消息轨迹证明。
5. DWM 在 Win10/Win11 上都参与桌面合成，系统版本、显卡驱动、OpenGL 呈现路径及
   时序可能使同样的空窗期有不同表现。这是**推测**，不能据“Win11 正常”推出
   “Win11 已修某个 Qt bug”或“Win10 没有合成”。没有确认的 QTBUG 编号。
   标题栏属于同一个 QML surface，故 surface 未及时呈现时它也可能一起消失。

## 修改前结论及修复选择

已证实的风险入口是：无边框主窗口的所有边缘操作直接委托原生交互式 resize，
而内容/标题栏必须由 Qt Quick 在变化的客户区中重绘。首要工作假设是此路径在
PC3 的 Win10/OpenGL/DWM 组合上暴露几何与新帧不同步，不是页面把标题栏盖住。
底层根因尚未实机证实。

局部修复采用 Windows 边缘鼠标抓取期间的普通 Qt geometry 更新，避开原生
SC_SIZE 模态交互路径，让普通 Qt 事件循环继续处理布局和场景图更新。每次用
按下时的全局坐标和 geometry 计算，不累计局部坐标误差；上/左边最小尺寸钳制
必须固定对边。释放、取消抓取、失活、隐藏、窗口状态变化和销毁都结束会话。
非 Windows 保留 startSystemResize；标题栏 startSystemMove、窗口样式、颜色、
录制面板以及 OpenGL 后端保持现有行为。

不叠加未经验证的 DWM 扩边、DwmFlush、WM_ERASEBKGND 吞消息或全局 basic loop：
前两者不能保证 Qt 已生成新帧，背景色已经不透明，切后端还影响真实 GL 预览。
此修复是有针对性的路径规避，最终是否消除 PC3 闪烁必须实机验收。

## 实现与自动验证结果

- `ui/backend/window_resize_handler.h/.cpp`：QML 可用的独立会话控制器。
  `usesManualResize()` 的 Windows 选择用 `#ifdef Q_OS_WIN` 隔离；几何计算和会话
  事件过滤实现不依赖 Win32 API，可直接在 Linux offscreen 验证。
  `start()` 的非 Windows 分支保留 Qt 系统 resize。
- `ui/qml/app/WindowResizeFrame.qml`：八个 MouseArea 接统一入口，保留位置、尺寸和
  光标；只接受左键，防止其他 item 抢占，释放/取消即结束。最大化/全屏不接受 resize。
  MouseArea/Qt 负责原有的鼠标抓取，不设置原生全局 hook。
- `ui/files_codex.cmake`：注册新增 backend 源文件。
- `tests/ui/window_resize_handler_tests.cpp`、`tests/ui/tests_codex.cmake`：新增套件。
  覆盖八方向、最小/最大约束、上/左固定对边、负/小数全局起点、反向拖回和重复事件
  不漂移、释放最后位置、取消、销毁、非法边、隐藏/最大化/全屏、平台选择和 QML 状态。
  Windows 条件部分还覆盖八个真实 MouseArea 的 press/move/release 路由；本次仅编译。
- `tests/ui/ui_interface_smoke.cpp`：真实 AppWindow 中模拟顶部/底部 resize，连续取
  900/640/720/800 高度，检查页面锚点重排、标题栏可见、opaque color、flags 和会话状态。
- `AppWindow.qml`、`TitleBar.qml`、`shell/window_controller.*`、`main.cpp`、录制面板
  均未修改；没有添加 DWM、渲染循环或图形后端的全局设置。

执行用户指定的命令：

```sh
cmake -S tests/ui -B build-ui -G Ninja -DCMAKE_PREFIX_PATH=/workspace/qt/6.8.0/gcc_64 -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" -DOPENGL_INCLUDE_DIR=/workspace/gldev/usr/include -DOPENGL_opengl_LIBRARY=/usr/lib/x86_64-linux-gnu/libOpenGL.so.0 -DOPENGL_glx_LIBRARY=/usr/lib/x86_64-linux-gnu/libGLX.so.0 -DOPENGL_gl_LIBRARY=/usr/lib/x86_64-linux-gnu/libGL.so.1 -DCMAKE_INCLUDE_PATH=/workspace/gldev/usr/include
cmake --build build-ui --parallel 8
QT_QPA_PLATFORM=offscreen ctest --test-dir build-ui --output-on-failure --no-tests=error

cmake -S tests/ui -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=/workspace/codex-run/mingw-toolchain.cmake -DCMAKE_PREFIX_PATH=/workspace/r12/tc/.toolchain/6.8.0/mingw_64 -DQT_HOST_PATH=/workspace/qt/6.8.0/gcc_64
cmake --build build-win --parallel 8
```

结果：修改前 Linux **39/39**；修改后 Linux **40/40**（8.10 秒），新增 QtTest 套件
**22 passed / 0 failed / 0 skipped**（含 init/cleanup）。两边构建退出码均为 0；
Windows 的 Q_OS_WIN 路径和条件测试均编译/链接成功，没有执行 Windows 测试。
Linux 日志在 `build-ui/Testing/Temporary/LastTest.log` 和
`build-ui/test-logs/window_resize_handler_tests.txt`。

所有修改的 C++/QML 生产代码都由 tests/ui 构建覆盖；完整录制应用、main.cpp 和实际
FFGLItem 预览未由该 harness 编译/运行（也没有改这些文件）。配置提示未找到可选
XKB/Vulkan headers，不影响本次成功结果。`git diff --check` 通过；删除的 `//` 注释
行数为 0。未提交、未 push、未联网。

## PC3 Win10 验收步骤

1. 构建包含本次改动的新 UI 应用，在普通窗口模式启动，先保持原来的 Qt/驱动/显示
   设置，不人为改 QSG_RENDER_LOOP 或图形后端。可用 QSG_INFO=1 留下实际后端/循环
   信息，记录 Windows build、GPU/驱动、屏幕缩放比例。
2. 在**上边中央最外侧 6 px**（竖向 resize 光标，避开标题栏移动区）按住左键，缓慢
   增大/减小高度，再快速来回十次；保持按住停顿一秒再松开。对**下边中央 6 px**
   重复。观察按下瞬间、拖动中、停顿中、松开后，标题和三按钮是否持续存在；新增
   窗口区域不应出现壁纸、黑块或仅在松开后才恢复的旧内容。
3. 缩到 640 的最小高度后继续向内拖，再反向拉大：上边拖动时底边应固定，不能因
   尺寸钳制变成移动窗口；页面视口应实时改变。短窗口的现有滚动条可以出现，滚动
   后应能到达底部控制栏，不能是整块画面僵住或控制栏永久消失。
4. 复测四角和左右边；拖动时指针移出窗口再释放、拖动中 Alt+Tab，随后移动鼠标
   不应继续 resize。验证标题栏移动、双击最大化/还原和三个窗口按钮仍正常；
   最大化/全屏时边缘不可拖。
5. 100% / 125% / 150% 缩放、跨不同 DPI 显示器分别检查跳位和光标跟随；浅色/深色、
   空闲/实时预览/录制中都试。Bandicam 录屏和肉眼分别看一次，并在 Win11 做回归。

## 未验证风险 / 行为取舍

- 没有 Win10/Win11 实机 GPU 呈现测试，不能用 offscreen 几何断言证明闪烁已消除。
  Qt 的普通 setGeometry 仍会触发平台窗口尺寸及底层 drawable/render target 更新；
  本次只绕过 SC_SIZE 交互循环，并未消除所有 native resize / WM_NCCALCSIZE 消息。
- 未验证真实 GL 预览/FBO 在连续 resize、录制负载下的时延和资源重建。
- Windows 边缘拖拽不再进入系统交互循环，依赖该循环的 Aero 边缘 resize 吸附、
  系统 Escape 取消/还原语义不再由 Windows 提供。标题栏的系统移动/吸附路径未改。
- 混合 DPI 跨屏时，Qt 全局逻辑坐标/窗口 DPR 可变化；本次覆盖小数坐标及固定尺寸
  计算，但没有覆盖真实屏幕切换、驱动和鼠标抓取丢失的全部组合。
- 若 PC3 仍闪烁，应将普通 geometry 更新、原生消息、Qt 帧同步/呈现时序一起采集，
  再判断是否需要 Qt 平台插件或驱动层修复；当前无证据指向某个确定的 QTBUG。
