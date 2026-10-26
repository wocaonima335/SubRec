# Native capture probe (G-01)

独立 Windows C++17 控制台程序；使用当前 Qt kit 的 MinGW g++，但不查找、链接或加载 Qt/FFmpeg。不录制 MP4，不写屏幕像素或音频样本到磁盘。程序只输出设备信息、统计和非密码学 FNV-1a 64-bit 哈希；请按设备信息日志管理输出。

## Git Bash 构建

以下命令只在现有 `build` 下创建独立构建目录和唯一运行日志目录，不清空已有目录，不部署/复制 DLL、不安装。

```bash
# 需要 MinGW g++、Ninja、CMake 已在 PATH 中；在仓库根目录执行
build=build/native-capture-probe
mkdir -p "$build"
run=$(mktemp -d "$build/run-$(date +%Y%m%d-%H%M%S)-XXXXXX") || exit 1
cmake -S tests/native_capture_probe -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  > "$run/configure.log" 2>&1
configure_rc=$?
printf 'configure_rc=%s\n' "$configure_rc"
# configure_rc 必须为 0，才继续构建。
cmake --build "$build" > "$run/build.log" 2>&1
build_rc=$?
printf 'build_rc=%s\n' "$build_rc"
# build_rc 必须为 0，才运行探针。
```

## 枚举、选择与采样

```bash
exe="$build/native_capture_probe.exe"
"$exe" wasapi --list > "$run/wasapi-list.log" 2>&1
printf 'wasapi_list_rc=%s\n' "$?"
"$exe" dxgi --list > "$run/dxgi-list.log" 2>&1
printf 'dxgi_list_rc=%s\n' "$?"
"$exe" wasapi --seconds 3 > "$run/wasapi-3s.log" 2>&1
printf 'wasapi_rc=%s\n' "$?"
"$exe" dxgi --seconds 3 > "$run/dxgi-default-3s.log" 2>&1
printf 'dxgi_default_rc=%s\n' "$?"
# 按本次枚举的 OUTPUT index 选择；这里的 0 仅是示例。
"$exe" dxgi --seconds 3 --output 0 > "$run/dxgi-output-0-3s.log" 2>&1
printf 'dxgi_selected_rc=%s\n' "$?"
```

- `wasapi`：枚举活动 render 端点，打印 ID、友好名称、状态；采样系统默认 `eRender/eConsole` 端点。记录原生 mix format，包括 extensible subtype、valid bits、channel mask、采样率和 block alignment。共享模式 loopback，以 5 ms 轮询读取包，不生成测试声音、不改变默认设备。
- `dxgi`：枚举全部 adapter/output，包括未连接桌面的 output；记录全局 output index、adapter/local index、LUID、桌面坐标/尺寸和旋转。默认选择第一个 AttachedToDesktop output；`--output N` 显式选择，不会在失败后偷偷切换到其他显示器。索引只对当前拓扑有效，运行时仍会重列全部输出。
- `--seconds` 默认 3，合法范围为整数 1..60；`--list` 仅枚举；`--help` 显示帮助。重复、缺失、未知参数及无效输出索引退出 2。

## 正确性及日志含义

采样在主线程完成，该线程使用 RAII 初始化 MTA COM，所有接口均在同线程使用和释放，最后 CoUninitialize。COM 接口、CoTaskMem、PROPVARIANT、音频流、音频 buffer、duplication frame 和 staging Map 均有独占 RAII 生命周期；正常路径显式检查 Stop/Release HRESULT，异常展开路径仍尝试配对释放。

WASAPI 成功 GetBuffer 才建立 buffer guard；`AUDCLNT_S_BUFFER_EMPTY` 不调用 ReleaseBuffer。SILENT packet 不解引用数据指针，按逻辑零字节计数/哈希；分别记录 silent packet/frame、discontinuity、timestamp error。`sample_frames` 是每通道时间帧，`interleaved_sample_values = frames * channels`。`buffer_gets` 应等于成功的 `buffer_releases`。

DXGI 成功 AcquireNextFrame 才建立 frame guard，timeout 不 ReleaseFrame。复制到 CPU_READ staging texture，以 DO_NOT_WAIT Map 在采样期限内轮询；逐行按 RowPitch 定位，只哈希 `width * 4` 个 BGRA8 可见字节，不哈希行填充。日志记录首帧 RowPitch 和内容哈希、末帧哈希以及各帧哈希序列的汇总哈希。`acquired_frames == released_frames`，`maps == unmaps == readable_frames` 应成立。截止时 GPU 尚未完成的最后一帧安全释放并记入 `deadline_dropped_frames`；之前已有可读帧则不判失败。画面不变时哈希可不变，静态桌面也可能仅首帧后超时；不要求每次哈希变化。旋转显示器仍哈希原始 duplication surface，不做转正。哈希不是可视质量、非黑帧或无保护遮罩的证明。

采样窗口以 steady_clock 限定，Acquire 最长等待 100 ms，不对 access-lost/device-removed 无限重试。单次 Windows 驱动 API、单帧哈希及清理并非硬实时，实际总运行时间可以略超窗口。

退出码：0 = 枚举完成或至少一个音频 sample frame / CPU-readable 视频帧；1 = Windows API/运行时错误（附 HRESULT）；2 = CLI/输出索引错误；3 = 采样期限内没有可用数据。`complete` 表示完整结束采样循环，并不等同于有数据。失败日志不能当作采样验收通过。没有活动播放端点时无法验证 mix format/音频包路径；默认端点空闲时也可能完全没有 loopback packet。DXGI 需要可访问的交互桌面和支持 duplication 的驱动；安全桌面、断开会话、显示拓扑改变等可能失败，需要在合适会话重新执行，而非本程序自动修改系统设置。
