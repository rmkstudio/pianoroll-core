# 0.5.2 candidate — sustained audition tails

Based on the local 0.5.1 candidate commit `b27c915`, on `codex/preview-sustain-fix`. Not merged or tagged; remote core publication remains pending.

- The reference synth now distinguishes key release from CC64 extension. Only pedal-held, released keys decay (-60 dB in six seconds, recycled below -80 dB). A long physically held key retains the existing envelope. Seek/resume/loop chase uses elapsed tail age, so exhausted tails cannot restart at full volume.
- `PlaybackNote::keyEnd` is derived by `makePlaybackSchedule`; adapters still supply unextended `end`. This addition requires recompilation. Musical schedule endpoints, source-isolated pedal/retrigger handling and serialized MIDI data are unchanged.
- A prebuilt interpolated waveform table replaces three sine calls per voice/sample; cached pitch/controller gains and compact active voices reduce render cost. The existing dry-waveform regression tolerance remains unchanged.
- At the 256-voice limit, new keys replace released/quiet/old voices with a bounded 5 ms fade instead of being silently dropped. The audio callback uses fixed storage, no heap allocations or reclamation.

This is a lightweight reference sound, not acoustic piano modeling. Windows/DAW listening and MIDI Lines adoption remain unverified. See VALIDATION.md for measured coverage.

---

# 0.5.1 candidate — view cost and fractional-time contract

Based on `codex/pianoroll-view-0.5`; work is on `codex/view-optimization`, not merged or tagged.

- Paint reads notes once, resolves group membership once per group, reuses visible notes for velocity and hover, and skips hidden lane work.
- Optional `controllerLaneRange(start, end)` lets indexed hosts provide only visible steps (including preceding/following boundary points); the existing full-lane fallback remains available.
- `minimumNoteLength()` makes host time resolution explicit (default one tick; beat-based hosts override it). Snap fractions, free movement and resizing respect that resolution.
- Playback, document mutation and serialized music contracts are unchanged.

Validation: JUCE command and view CTest both pass under Linux/Xvfb, including fractional beat resizing, all snap divisions, hidden CC access and per-paint read bounds. Windows/DAW and MIDI Lines integration have not run.

---

# PianoRollCore 0.5.0 — 单行工具栏

钢琴窗工具栏按 MIDI Recover 第 4 步的界面方案重排。产品需要更新 vendor 快照、按下面的接口变化调整后重新构建才会采用。

## 改动

- **工具栏合为一行**：播放／停止、时钟、撤销／重做、画笔／选择、吸附、“编辑”菜单、跟随，末尾留出“适应窗口”和“展开”的产品位置。底部的播放行去掉，各栏随之加高。
  - 画笔和选择从下拉框改为两个并排按钮（`Draw tool` / `Select tool`），1／2 键和右键临时绘制不变。
  - “选区”“操作”“塑形”“视图”合并为一个“编辑”菜单：适应选区、命令搜索（Ctrl+K）、数值塑形，后面接产品自己的条目。
  - 去掉“回起点”按钮：连按两次停止或 Home 键（命令 `transport.home`）仍回到开头。
  - 按钮带 `icon` 属性（play/pause、stop、undo、redo、pencil、pointer、follow），成组按钮带 `segment`（first/last）；产品的 LookAndFeel 可以据此画图标，否则照常显示文字。吸附框带 `prefix` 属性“吸附”，选项缩短为“1/16”等。
- **标尺**：小节号按可用宽度每 1、2、4… 小节标一次，放不下的标签跳过，缩小后不再挤成一团。
- **CC 栏数值**：0 和 127 的标签保持在栏内，不被栏头的控件遮住。
- **栏高分隔条**：按实际布局计算，拖动时把手留在鼠标下。0.4.0 用固定常数，抓住时会先跳约 10 像素、之后比鼠标慢约一成。

## 接口变化（不兼容 0.4.0）

- `ViewLayout`：去掉 `tool`、`fitSelection`、`actions`、`transform`、`view`、`home`；新增 `pencil`、`pointer`、`edit`、`expand`。`controller` 默认放在 CC 栏头左侧。
- `PianoRollHost::showViewMenu(Component&)` 改为 `addEditMenuItems(PopupMenu&)`：产品把自己的条目（带 action）加进“编辑”菜单。
- `PianoRollView` 不再有 `toolSelect`、`fitSelectionButton`、`actionsButton`、`transformButton`、`viewButton`、`homeButton`；`keyboardControls()` 只返回吸附框。

验证结果见 `VALIDATION.md` 和 `juce/tests/VALIDATION.md`。

## 上一版：0.4.0 — 共享钢琴窗组件

本次把钢琴窗的“身体”从 MIDI Recover 搬进共享模块：画面、各工具的鼠标手势、光标和通用工具栏。MIDI Lines、MIDI Recover 需要各自更新 vendor 快照、接入后重新构建才会采用；已安装的插件不会自动改变。

## 改动

- **新增可选 JUCE 目标 `PianoRollCore::View`**，入口 `juce/include/PianoRollCore/PianoRollView.h`。它和 `CommandUI` 一样受 `PIANO_ROLL_CORE_BUILD_JUCE_UI` 控制；纯 C++ 目标 `PianoRollCore::PianoRollCore` 不依赖它。
  - `PianoRollView` 负责绘制标尺、琴键、音符、力度栏、CC 栏、播放头、循环区和悬停提示。
  - 它也负责鼠标手势：画笔、选择/框选、移动、改长度、Shift 复制、擦除、力度笔画、CC 笔画、中键和滚轮导航、标尺定位/循环、栏高分隔条。
  - 光标、视图窗口和通用工具栏（撤销/重做、工具、吸附、选区、操作、塑形、视图菜单、播放控制、时钟、滚动条）也在这里。
  - 手势按工具拆开，状态在 `ViewGestures.h`，同一时刻只有一个手势。
- **新增 `PianoRollHost` 接口**，由插件实现：
  - 插件按序号提供音符、选区、范围、CC 栏和播放位置。
  - 插件接收每一处草稿修改。
  - `beginEdit` / `editChanged` / `commitEdit` 对应插件已有的手势事务；每次手势最多提交一次，撤销与提交闸门仍由插件负责。
- **`ViewPalette` 和 `defaultViewLayout`**：默认配色和布局与 MIDI Recover 0.7.10 相同，插件可以替换。
- 这些代码由 MIDI Recover 0.7.10 的钢琴窗逐行搬入，没有改变手感。搬移过程见 Recover 的路线图第 2 步。
- 纯 C++ 核心（导航、吸附、选择、批量动作、命令、播放）的接口和行为不变。
- 快照工具不再把仓库的 `.github` CI 配置计入发行清单，产品 vendor 快照与此一致。

## 接入

1. 先加入 JUCE，再 `add_subdirectory` 本模块，链接 `PianoRollCore::View`。
2. 实现 `PianoRollHost`。时间用产品自己的单位，`roundTime` 做最终取整（Recover：JUCE `roundToInt`）。演奏法标签与查找使用“每次绘制建一次索引”的函数。
3. 产品自己的控件放在 `placeProductControls`，例如分组选择、通道、All、CC 选择、适应窗口、拖出。可以让编辑器继承 `PianoRollView`，也可以组合持有。
4. 用产品已有的事务入口实现 `beginEdit`、`commitEdit`：保留 mouse-down 前像，过期的 hash/epoch 拒绝提交，宿主接受后才记入撤销历史。
5. “不吸附”时步长是 1 个时间单位。以四分音符为单位的产品（MIDI Lines）接入前需要补一个宿主设置。

验证结果见 `VALIDATION.md` 和 `juce/tests/VALIDATION.md`。

## 上一版：0.3.1 — 公共接口维护

本次更新独立的 PianoRollCore 模块。MIDI Lines、MIDI Recover 的源码快照和插件二进制未在本次更新；产品需要更新依赖后重新构建才会采用修正。

### 改动

- **宏草稿关闭流程**：搜索页执行命令成功后也经过 `dismiss()`。有未保存宏时，命令正常完成一次，界面回到保留完整草稿的宏页；保存或放弃后可以关闭，不会再次执行命令。
- **数值接口**：公开 `parseCommandNumber` / `parseCommandInteger`，与注册表共用解析规则，失败返回空 optional。整数参数经校验后转成普通十进制字符串，`1e2`、`100.0` 都交付为 `100`，避免旧接入代码只读取前缀。JSON schema、命令 ID、回调签名保持兼容。
- **键位转换**：共享 JUCE 转换器支持 Insert、F1–F24 及其修饰键，与核心接受的键位范围一致。
- **版本来源**：只维护 `Core.h` 的公共版本常量。CMake 读取它；快照工具据此生成 VERSION，并验证它与清单一致。修正旧版发行号 0.3.0 与头文件常量 0.2.0 不一致的问题。
- **验证入口**：修复独立 JUCE 测试 CMake 未提供应用版本的问题，直接使用公共模块版本。

### 后续维护入口

| 修改内容 | 入口 |
|---|---|
| 数值语法、默认值、参数范围、整数规范形式 | `Commands.h`、`src/Commands.cpp` |
| 命令面板关闭、宏草稿、快捷键录制 | `juce/CommandPanel.cpp`；关闭统一走 `dismiss()` |
| 变换框绘制、命中、拖动、取消 | 现有 `juce/TransformOverlay.cpp` |
| 坐标、命中、框选遍历、提交门 | 现有 `Core.h`、`src/PianoRollCore.cpp` |
| 范围切换、标尺手势、力度笔画 | 现有 `Interaction.h`、`src/Interaction.cpp` |
| 发布版本与源码快照 | `Core.h` 版本常量、`tools/snapshot.py` |

不要因为公共模块已有几何、范围和编辑计算，就假定产品的每条事件路径都已接入。普通音符手势与选区联动是否值得进一步共享，应先对照两款产品真实调用路径。本次不新增统一编辑器、文档存储或画布框架。

### 接入

1. 确认产品旧 vendor 快照没有本地修改，再以本版完整快照更新依赖与版本/hash 检查。
2. 字符串参数接口继续可用。需要读数值的新接入代码优先调用公共解析函数；已有整数读取也会收到规范的整数文本。自定义超出 C++ int 范围的参数应选择相应表示，不能使用 int 读取接口。
3. 各产品按自己的检查入口验证命令、宏、键盘焦点和文档提交；本模块测试不能替代 DAW 内的验证。

验证结果见 `VALIDATION.md` 和 `juce/tests/VALIDATION.md`。
