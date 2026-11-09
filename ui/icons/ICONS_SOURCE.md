# ui/icons 图标来源与许可状态

这 27 个 SVG 是新界面（`SubRec.Ui`，`SUBREC_BUILD_NEW_UI=ON`）里 `AppIcon` 可用的**全部**图标：25 个来自下述 iconfont 集合，2 个由项目自绘（见文末"项目自绘图标"），通过 `ui/files_zcode.cmake` 登记为模块资源（`:/qt/qml/SubRec/Ui/icons/`）。之前的 Lucide 图标已全部移除，运行时不存在 Lucide 回退。

## 来源
- 页面：iconfont.cn「Ant Design 官方图标库」，<https://www.iconfont.cn/collections/detail?cid=9402>，创建者显示为"竹尔"，页面简介为"Ant Design 3.0 全新线性图标体系"，共 599 个图标。
- 下载方式：在网站页面上把 25 个图标加入购物车，点"下载素材 → SVG"。没有付费；页面没有提供整库导出，所以只下载了这 25 个。
- 原包：`icon_04s7m1r40avc.zip`，SHA256 `d897e249571c75b6978216f37850c75796dbff1a08992a8fd9a2a9c4375837da`，内含 25 个 SVG，包内没有任何许可文本。
- 原包、页面截图和评论区截图都保存在仓库之外，作为工作证据；上面的原包 hash 可以用来核对。

## 规范化（可复现，不手改图形）
用一个离线脚本处理，不解析 DTD 或实体：
- 先按纯文本校验，然后去掉标准的 SVG 1.1 DOCTYPE。遇到 ENTITY、`<script`、`foreignObject`、`href`、外部 URL、`on*` 事件属性或 `&` 时直接拒绝。
- 校验通过后才用 XML 解析。

输出规则：
- `width`/`height` 统一为 24，`viewBox="0 0 1024 1024"` 不变。
- 根元素加 `fill="currentColor"`，`<path d>` 原样保留。
- 去掉 `class`、`version` 等导出属性，以及 `play`（源文件 caret-right）上硬编码的 `fill="#333333"`。

`AppIcon` 按 `size × 设备像素比` 光栅化后整体着色，所以 1024 的 viewBox 不需要改代码。

## 映射（语义 → 源图标）
| # | 运行文件 | 源名 | 原 SVG | 对应程度 | 原 SVG SHA256 | 运行 SVG SHA256 |
|---|---|---|---|---|---|---|
| 1 | `video.svg` | video | `video.svg` | 直接对应 | `ac9a0fd7e936ef7ffc22f742ebbadfedf3078437eb19d001aa404296aec8648f` | `ed30192be95dde6ad64dc92d7a646503c9b6255bba74e06d76c32fe1cdb04410` |
| 2 | `library.svg` | book | `book.svg` | 近似：book（书本）代替"库" | `2093d968766e53556a5ccc2daa186dc1b300f82994c87ef5e5379a8e1cae2326` | `1e3fc96bed23229bafe487e7964cf1f808fdece0b72c5a3b68202e29991d9df9` |
| 3 | `settings.svg` | setting | `setting.svg` | 直接对应（源名为单数 setting） | `8d95a3f90a08a25048131574cf8ea2672056a3e2725472810a05109e2ec43629` | `b18b502bbdaef4a7b44456bfd8eb6de17c6c7365253abbb5a7757eee0a08e521` |
| 4 | `minus.svg` | minus | `minus.svg` | 直接对应 | `1bb6fec2037df0216db39bd8a1d1854054cf7916c6a546f275acb7e50e7e3b8f` | `540285a81dbf3e43432ee21e7057eee796beb381ff4b4058122a859409af3175` |
| 5 | `maximize-2.svg` | fullscreen | `fullscreen.svg` | 近似：fullscreen（全屏/展开） | `2ceffe56f83f05dfa10fc2c772c385b8c66bba22ff18fa798bde3918478ce4e0` | `20a5bec20df3d7d1682e1f8ee3c93f0c6fc3af9c2e864dbf9a9111688f71b963` |
| 6 | `minimize-2.svg` | fullscreen-exit | `fullscreen-exit.svg` | 近似：fullscreen-exit（退出全屏/还原） | `52fedf9be9f71e29ae981f9e9131d14e68babf799704c1123dc7afbe6a58dc6c` | `ee7887d62cc9904a85e1448c74727c4b076c4c40f2bb99002de9fcc592dc2edb` |
| 7 | `x.svg` | close | `close.svg` | 对应：close | `2b05f7ad616d74dd5b400b610ee6cfd91e319d14eaed560213a85cf1bf83a3e2` | `29942b0a5d3e57bfb0d26ff7ab2f84bfc2dd9c28743bf62fc8a3f2f49b10c868` |
| 8 | `check.svg` | check | `check.svg` | 直接对应 | `73f84d75385fd06d64638bf260baa3ba6c57247933640e0fb6ae1b2280287d79` | `2a731f9ba812c407b5b91fdaeea48219b6b99044f6ce290d947444843b0443f3` |
| 9 | `chevron-down.svg` | down | `down.svg` | 近似：down（向下箭头） | `f0791fee5c65f5afc1d979e58fcb8db527dcfc4187771feccfed8e932c5d140e` | `c200f7357e67ba83588c9ce628f5f47bf4a528ceaf4271d2daa5f691a02b6a2f` |
| 10 | `circle-alert.svg` | warning-circle | `warning-circle.svg` | 对应：warning-circle（圆形感叹号） | `f971e8bf2b5b653c5a821b1c1ed84b1c0cc6950fe1ec989734def79ed5084803` | `207cb0caf9bc55b03255d73ca73d4d2c5a3f1bd68933206bab36d8249a541a0f` |
| 11 | `triangle-alert.svg` | error | `error.svg` | 对应：error（三角感叹号，源名为 error） | `cfda47083923ba4f86a878efba90509d1c218537bd080a222f0b0b47817f1b8f` | `1361125d1fc58a155848efd3badc5112baebd203dbec6fd883725207c9ea465e` |
| 12 | `circle-check.svg` | check-circle | `check-circle.svg` | 直接对应（check-circle） | `f6b958c7fba12cf7a3c3c7a4dd50d398fd0245f66a66f58f03abf276ba3ca54c` | `a9c3e22453ce39ff3c62ab9490323c8488e2b048a7588720bbeafd30f6e596f7` |
| 13 | `info.svg` | info-circle | `info-circle.svg` | 对应：info-circle | `53bdfb59aacd5d5456f69e2c12155fd7fbd6b6752975c707664d411103d367ff` | `b466496820552371aa923d689de1248c7cddbb420eb2234867bdbb8af0ab107e` |
| 14 | `captions.svg` | file-text | `file-text.svg` | **代理，无精确图形**：file-text（文本文档），该库子集中没有字幕/captions 图标 | `598d69b4c289c7d141aa50d73efbf375eaf960d8b2875e10b348d9bd156a5d89` | `009f412cf642559bc916981ecbf23e3cee62f31cd8aaacab43a33778a1463c4d` |
| 15 | `pause.svg` | pause | `pause.svg` | 直接对应 | `c6302f7969e5d214ab0a4ffb7af2239a31a94f9c804be485e1855e082233a6dd` | `e582589ecc6ff4275252b8d30252abf3282d82d273bd89ec2bb7dae92de87c6d` |
| 16 | `play.svg` | caret-right | `caret-right.svg` | 近似：caret-right（向右三角）；源 path 的 fill="#333333" 已去掉 | `586a1c2cf37fd79d061473c3db981b6300408bd4c1a88d7de77ed49493822038` | `2c4fbeb1582d0f77d84e8953d71dae31e97678f1428fbeafe155b9e79972b4d2` |
| 17 | `app-window.svg` | desktop | `desktop.svg` | **代理，无精确图形**：desktop（显示器） | `b8ea3b4ee1b8c28e966744ad7fa07c43aceafee8ec7c034cf184e2e05242e32d` | `11204e9af216e0822d276417b8cb3f916ef5c11ca485add87f71fcb250ff1d26` |
| 18 | `folder-open.svg` | folder-open | `folder-open.svg` | 直接对应 | `93800d38cfbe61d1d5cc35991079757ef6ca555f8dbdc1c75d0d5d2aa89409db` | `e49c6ba7d246ebe4f6b796734f010adc240827236b510888bf46ef7b13710ad4` |
| 19 | `monitor-off.svg` | disconnect | `disconnect.svg` | **代理，无精确图形**：disconnect（断开的链接） | `2cdad2b7b536d877442ff11dc4dc1b16d3765bd83a8e5308cde137541f345b8f` | `12ca6d6f089ccf0b0f5474829e2031d5f783760f754d4587326e375193d46efd` |
| 20 | `monitor.svg` | monitor | `monitor.svg` | 直接对应 | `958bfadd95326b36661ea3bf16d4f057995d1c1a2c99fe46b39a00c88701bc89` | `14f243d4c4de827ce5fe7bcb9fd462c66c3874c496e3e62d9523127add62a344` |
| 21 | `camera.svg` | camera | `camera.svg` | 直接对应 | `9bdf9a03c761920c81d41852a9a61c10a30af80d9f1ac5b3ab1e05bef3146875` | `0d97b66118777b8cfcc53cf7915f6e8d724206565be93a7765fa2e3b8c64569c` |
| 22 | `volume-2.svg` | sound | `sound.svg` | 近似：sound（扬声器） | `8631a2b587a010cd470346cc989238cf36388e0a70b60dbc1d8b4bebda9cad7e` | `e84ee930ec94bb7e37c6fd8873d364829f0b1b16e9f9edcfc518a4f124090b07` |
| 23 | `mic.svg` | audio | `audio.svg` | **近似**：audio（图形为麦克风） | `970b03f11de866f07a82aaecd09a0009ee9420e20b6487252e0017a0bbeecc03` | `4e728dda5f382e8e0cf39af0124a1ac4adf603a426e8ba3449e9cc7338b6e2b0` |
| 24 | `mic-off.svg` | audio static | `audiostatic.svg` | **近似**：audio static（图形为划掉的麦克风） | `db0df7ca23b20c07a6d83576e738ee8541064d0df2de02bc2e0ea6f13419d44b` | `bae81719e06bec1d79b476cb0d148358474d32d48ae0b574221ddd4cf6c5c030` |
| 25 | `download.svg` | download | `download.svg` | 直接对应 | `08e5b9149baaf94b2a2fe37a909602b263050444114637eb950d76fa0f3599e7` | `e3eef0b598f0005d00ce2177d6960141bf00090ee15ae2a6076ce64104f9c305` |

**没有精确图形、用代理的**：captions → file-text，app-window → desktop，monitor-off → disconnect。
**近似**：mic、mic-off 分别用 audio 和 audio static。另外，测试用组件画廊里原来的刷新、删除、搜索三个示例图标，在这 25 个里找不到同义图形，已改为不带图标的示例。

## 许可状态（如实记录，尚未解决）
- **没有拿到完整的许可文本**。下载包里没有许可文件，集合页面也没有显示许可条款。
- 唯一的依据是页面评论区的**非正式回复**：用户问"可以用作商业吗？"，创建者"竹尔"答"可以的"；另有其他用户（非创建者）说"可以免费商用的"。有人追问"商用需要申请授权书吗"，没有看到作者回答。这只是评论，**不是许可证**。
- 因此**不能**把这些图标标成 MIT、GPL、ISC 或其他任何开源许可。署名要求、修改许可（本项目改了尺寸属性和 fill）、再分发条件都未知。
- **与 GPL-3.0 程序一起分发是否兼容，风险尚未解决**。正式对外分发含这些图标的构建之前，需要通过合适渠道取得书面许可，或者换成许可明确的图标。

## 项目自绘图标（不是 iconfont 素材）
标题栏的最大化和还原按钮原先用的是 maximize-2（fullscreen，四角箭头）和 minimize-2（fullscreen-exit），这不是常见的窗口按钮形状。所以项目自己画了两枚标准窗口控件图标，属于本项目作品，按项目许可（GPL-3.0）发布，不涉及上面的 iconfont 许可问题。

| 运行文件 | 图形 | SHA256 |
|---|---|---|
| `window-maximize.svg` | 单个方框（最大化） | `4b3e49e185c811b489d8606c52c5fa9803f1fc16abbaf3ad8a6f88b9bb4ea1c6` |
| `window-restore.svg` | 两个错叠的方框（还原） | `199c3ca6cf28180de3b16d02b525b125fd2fa3ca4b21a0f76838a6aef38aba30` |

格式与上面的规范化输出相同：`viewBox 0 0 1024 1024`，宽高 24，`fill="currentColor"`，只有一个 `<path d>`，没有 DOCTYPE 和脚本；另外在 `<svg>` 上加了 `fill-rule="evenodd"`，方框内部按奇偶规则挖空，不依赖子路径的绕向。坐标都是 64 的整数倍、线宽 64，在 16 px 下正好是 1 px，对齐像素格。maximize-2 和 minimize-2 两个文件仍然保留。
