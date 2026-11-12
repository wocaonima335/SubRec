# Windows 安装器发布与验收清单

实施采用 D1–D16 推荐值，程序图标按最新决定采用 AI 生成的 cand1，本次完全不签名。
这是维护者之后操作的清单，本次实施没有触发 CI、访问 Windows 或操作任何 Release。

## 推送前维护者检查

- [x] 协调方于 2026-11-12 提供固定提交的 `ChineseSimplified.isl` 和 issrc 许可；
  原字节、作者头及 SHA256 已核对，来源与归档已补入仓库。
- [x] 协调方核实 Inno 许可、官网 6.7.4 / 7.1.1 与 Chocolatey 6.7.1；CI 固定 6.7.1。
  商业许可购买非强制；Qt 6.8 最低 Windows 10 1809 与安装器一致。
- [ ] 审阅中英 NOTICE、第三方清单和 AI 图标来源声明；AI 图像版权状态可能不明确。
  新界面 iconfont 图标的既有风险决定和仅预发布限制仍然有效，本次不要求重复授权。
- [ ] 在后续 Windows 构建中确认 6.7.1 安装、编译器随附 `License.txt` 及中文翻译兼容性。
- [ ] 审阅永久 AppId、历史清理目录表、版本资源和 Release 作业的写权限。
  确认后续推送会在 PR/main 构建安装器，v* tag 会创建预发布草稿；本次未启用/触发远端工作流。
- [ ] 协调方按模块提交；此工作区不提交。文件分组见实施回报。

## 首次 CI 验收（须由维护者后续操作）

- [ ] 编译 appSubRec 和 Inno 脚本；确认 exe 文件属性、关于页、安装向导、卸载列表
  显示同一版本（例如 `0.2.1-beta`；四段数值资源为 `0.2.1.0`）。确认 exe/快捷方式图标。
- [ ] 静默冒烟通过：中文/空格路径、文件 hash、快捷方式、升级清理、日志保留、卸载干净。
- [ ] 人工检查 `licenses/`、`sources/`、`SOURCES.txt`；对比安装器与 portable payload。
- [ ] 记录安装器真实体积与耗时，不引用方案估计作为实测结果。
- [ ] 测试仓库验证 Release 列表 API 能找到草稿和正式版；已有 Release、API 错误、
  不完整材料、hash 错误、额外附件都必须失败且不修改已有 Release。不要用正式 Release 做试验。

## Windows 10 / 11 人工验收

须在 Win10 22H2、Win11 24H2/25H2 上由维护者安排，不属于本次沙箱执行范围。

- [ ] 全新 per-user 安装无 UAC；开始菜单/桌面快捷方式启动；录制 MP4 到“视频”目录。
- [ ] 中英文系统 UI 语言自动选择；首次目录页可改、升级沿用旧目录；桌面选项可取消；
  完成页启动默认勾选、许可说明默认不勾选；静默安装不启动程序/打开说明。
- [ ] “关于”页日志存在，LICENSE.txt 与 THIRD_PARTY_LICENSES.md 链接可打开。
- [ ] 旧版升级：运行中、托盘驻留、录制中分别测试互斥检查和提示；先正常停止并退出，
  确认不强杀导致丢录制。卸载运行中程序同样提示退出。
- [ ] 升级/降级后不存在旧 Qt 插件、QML 文件；日志与 HKCU 设置保留。
- [ ] 卸载正确显示名称、版本和图标；删除安装文件、快捷方式及 SubRec.log；用户设置、
  录像文件及用户自建的非安装文件保留。用户文件存在时安装目录可以保留。
- [ ] `/VERYSILENT /CURRENTUSER /DIR="中文且带空格的路径"`；中文用户名下安装与录制。
- [ ] 绿色版和安装版分别启动，共享设置；同一会话不能同时运行。
- [ ] 记录 SmartScreen / Smart App Control / 杀软的实际行为；不要承诺只有一次警告或
  必定有“仍要运行”。遇到策略阻止时不要求用户关闭安全防护。

## 草稿评估与发布

1. 维护者批准后推下一个 beta tag；CI 构建并验收成功才创建预发布**草稿**。
   D10 未解决前正式 tag（无后缀）被拒绝。
2. 不要在 CI 完成前手工创建同名 Release。已有草稿/正式版时 CI 失败；重跑不得覆盖。
   由维护者判断是否保留草稿并手动上传，或在确认安全后删除自己的旧草稿再重跑；脚本不删除。
3. 从草稿下载：`SubRec-vX.Y.Z-beta-win64-setup.exe`、`...-win64-portable.zip`、
   `...-ffmpeg-corresponding-source.zip`、`SHA256SUMS.txt`；核对每个 SHA256 并完成上述验收。
4. 用评审后的中英文发布说明替换默认草稿说明，保留未签名、许可风险、源码与校验说明。
5. 维护者最后决定是否发布。证书申请与签名服务接入均不在本次范围内，以后再议。


程序图标由 AI 图像模型 gpt-image-2.5（经维护者自己的 CLIProxyAPI 调用）于 2026-11-12 生成。AI 生成图像的版权状态可能不明确。
提示词为：

> Flat minimal app icon for a Windows screen-recording and subtitle app called SubRec (字录). Rounded square tile filled solid red #DC2626, full bleed, no background outside the tile except transparent/white. Bold thick white simplified Chinese character "字" centered. No text other than the character, no gradients, no shadows, no fine details, high contrast, must stay legible at 16x16 pixels.
