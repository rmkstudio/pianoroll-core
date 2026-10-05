# PianoRollCore 0.5.0

这是 MIDI Lines 和 MIDI Recover 共用的钢琴窗体验模块。0.3.0 在原有导航、播放与编辑规则上增加命令搜索、快捷键设置、原子宏、音符处理、控制曲线变换，以及音高显示和步进输入。插件继续各自安装，公共代码编译在各自 VST3 内，无需另装组件。

用户操作见 `EDITING-GUIDE.zh-CN.md`。命令接入与事务约定见 `COMMANDS.md`；可选 JUCE 面板见 `juce/README.md`。

0.4.0 新增可选的 JUCE 钢琴窗组件 `PianoRollCore::View`：音符、标尺、琴键、力度栏和 CC 栏的绘制，各工具的鼠标手势、光标、视图窗口和通用工具栏都由共享模块提供；插件只通过 `PianoRollHost` 接口提供数据并接收修改，文档、撤销和提交仍由插件负责。纯 C++ 核心的接口和行为没有改变。具体变化和接入说明见 `RELEASE.md`，组件说明见 `juce/README.md`。

0.5.0 把钢琴窗工具栏合为一行（播放、时钟、撤销、画笔／选择、吸附、“编辑”菜单），标尺小节号按宽度稀疏显示，栏高分隔条跟手；`ViewLayout` 和 `PianoRollHost` 的菜单接口有不兼容变化，见 `RELEASE.md`。

## 现在共同维护哪些功能

| 用户操作 | 公共实现 | 插件负责接入的内容 |
|---|---|---|
| 按住中键拖动；滚轮缩放与平移 | 键盘区中键纵向缩放、音符／控制区中键平移；鼠标位置保持为缩放锚点 | 提供区域、可见时间与音高范围 |
| Ctrl＋滚轮；Ctrl＋Shift＋滚轮；Shift＋滚轮 | 分别缩放时间、缩放音高、横向平移；普通滚轮移动音高 | 将 Ctrl／Command 和窗口坐标转成公共输入 |
| 看音高、看小节／拍、看吸附网格 | 音高名称与显示阈值；实际拍号分段、小节编号、拍线与细分线 | 传原 MIDI 拍号、PPQ；设置音名八度习惯 |
| 点击标尺跳转；拖动标尺循环 | 判断点击与拖动、循环两端吸附与排序、右键清循环意图 | 更新播放位置与绘制光标／循环区域 |
| 播放、暂停、停止、回到开头 | 共用 transport、寻址、循环、分轨／声部 solo、控制器追奏、参考试听音色 | 原拍／tick 经本插件最终 tempo map 转秒，连接宿主音频缓冲 |
| 点击音符／琴键试听；拖动音符试听 | 共用短音试听与发声生命周期 | 决定哪次用户动作请求试听 |
| All、单组、多组、solo | 共用范围与切换规则；编辑范围和试听 solo 分开 | Lines 用 Voice，Recover 用 Track；保留各自 MIDI 源归属 |
| 工具、撤销、复制、方向键、播放快捷键 | 完整命令映射，1 画笔／2 选择，Space／Enter 播放暂停，Home 起点；文本输入保留焦点 | 执行业务命令、管理窗口与宿主焦点 |
| 框选、吸附、移动、拉长、复制 | 坐标／命中、组边界、原抓取音符对应副本、Ctrl／Shift 轴锁、一次手势一次提交 | 原音符 ID、文档 revision、撤销与最终保存 |
| 力度绘画、Ctrl 相对调整、Alt＋滚轮 | 笔画跨音符插值、相对偏移、0／127 边界、每次滚轮一步 | 哪些音符可编辑、绘制与保存修改 |
| 只列有数据的 CC；选择写入目标 | 从真实事件／有效笔画构目录、排序去重、明确来源；新增空通道不会生成事件 | Recover 保留 CC 可编辑；Lines 保留原文件 CC 的来源策略 |
| Q 量化、镜像、时值、条件选择、切分、重复、扫弦 | `NoteActions` 不可变输入到批量结果，保护未选音符、只读占位与显式 Key Switch | 原始时间账本、音符 ID、最后一次 tick 舍入和提交 |
| 力度与 CC 变换框、CC 随乐句 | `AutomationActions` 及可选 `TransformOverlay`，来源明确、区间外状态保护 | 提供当前真实事件、明确轨道／通道／CC 选择，提交一次撤销 |
| Ctrl+K、快捷键、宏 | `Commands` 注册表、参数、按需查找、键位冲突和宏事务；可选 JUCE 共用面板与设置保存 | 实际可用性、隔离工作副本、文档与撤销发布 |
| 音高折叠、音阶、演奏法、步进 | `MusicTools` 行映射、音阶成员、明确演奏法映射、和弦松键推进 | 绘制／命中使用同一行表，按来源保存标记，MIDI 输入队列 |
| 钢琴窗画面、各工具鼠标手势、光标、通用工具栏（0.4.0） | 可选 JUCE 组件 `PianoRollView`（`PianoRollCore::View`），手势按工具拆分，一次手势只经宿主提交一次 | 实现 `PianoRollHost`：按序号提供音符、范围、CC 栏和播放位置，接收草稿修改，保管文档、撤销与提交闸门；放置产品自己的控件 |

**仍由各插件负责：**皮肤与主窗口布局（钢琴窗内部布局可沿用 `defaultViewLayout` 或由插件提供）、分线和 FLP 恢复算法、文档／原始 tick、tempo 校正、MIDI 写出和拖出文件、工程存档、异步任务。公共模块没有合并两款产品，也没有把 Recover 的 CC 改成只读。

## 第三个插件怎样复用

核心 target 是纯 C++17，依赖标准库，无 JUCE 或 Python 运行时依赖。可选的 `PianoRollCore::CommandUI` 依赖宿主工程提供的 JUCE 7，不随本源码包附带 JUCE。第三个插件保留自己的文档，在窗口中传入统一的输入值，使用公共动作结果更新窗口；播放前把自己的最终事件转换成秒，交给公共播放引擎。

```cmake
set(PIANO_ROLL_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/PianoRollCore"
    CACHE PATH "Shared piano-roll source")
add_subdirectory("${PIANO_ROLL_CORE_DIR}" "${CMAKE_BINARY_DIR}/piano-roll-core")
target_link_libraries(MyPlugin PRIVATE PianoRollCore::PianoRollCore)
```

```cpp
#include <PianoRollCore/PianoRollCore.h>
using namespace pianoroll;
ViewState view{0, 16, 36, 84};
Rect notes{60, 40, 800, 490};
NavigationLimits limits{.25, 128, 128, 12};
auto gesture = beginMiddleDrag(PointerRegion::keys, {40, 180}, view, notes);
view = dragNavigation(gesture, {40, 130}, limits).view;
```

- `Core.h`：基础几何、吸附、命中、复制映射、编辑提交。
- `Navigation.h`：`ViewState`、中键／滚轮导航、音名、播放跟随。
- `Interaction.h`：快捷键、标尺手势、分组／solo、拖动轴锁、力度笔画。
- `Meter.h`：`MeterMap` 分段拍号、音乐位置与有数量上限的网格线。
- `Controllers.h`：`ControllerDirectory` 实际 CC 目录和明确写入目标。
- `Playback.h`：`makePlaybackSchedule` 与 `PlaybackEngine`；可编译用例在 `examples/PlaybackExample.cpp`。
- `Commands.h`：稳定命令 ID、参数验证、快捷键、自定义宏及事务回调。
- `NoteActions.h`：批量音符处理与条件选择，统一由适配器转换拍／tick。
- `AutomationActions.h`：力度／CC 变换、明确来源的 CC 跟随与边界状态保护。
- `PianoRollView.h`（JUCE，`PianoRollCore::View`）：`PianoRollView` 组件、`PianoRollHost` 接口、`ViewPalette` 配色、`defaultViewLayout` 布局；手势状态在 `ViewGestures.h`。
- `MusicTools.h`：音高行映射、音阶成员、演奏法标签与步进和弦。

完整小型编辑适配示例见 `examples/MinimalAdapter.cpp`。`PianoRollCore.h` 是总入口；也可以只包含需要的头。

## 接入时必须保持的约定

**时间与坐标。**编辑与导航可使用四分音符拍，也可使用原始 tick，每次调用的全部数值必须同单位。`ViewState` 的 `pitchLow`／`pitchHigh` 是可含小数的音高边界，跨度为 `high-low+1`。导航始终从按下时快照计算，重复 mouseDrag 不会累积误差。两款现有插件的 MIDI 60 标签均为 C5，因此调用 `pitchName(pitch, 5)`；函数默认 C4，便于别的工程明确选择习惯。

**音乐标尺。**`MeterMap.assign(changes, unitsPerQuarter)` 传 1 或原 PPQ；同一时间多个拍号取输入顺序最后一个。缺拍号按 4/4 显示，`implicitMeter` 明确记录该回退。拍号改变处开始新小节，之前不完整的小节计一个；重复相同拍号不新开小节。`grid(from,to,minimumSpacing,maximumMarks,subdivisionStep)` 按可见范围生成小节／拍线与可选吸附细分线；返回的 `barStart`、`beatStart` 用于控制线强度。标尺不会更改 MIDI 事件、自动推断 FL 速度或修改 tempo map。

**播放与线程。**adapter 在消息线程从当前编辑结果和最终 tempo map 建立秒单位的不可变 schedule。公共引擎用同一份 schedule 播放、暂停、跳转、循环和 solo；CC64 延音、CC7 音量、CC11 表情、CC10 声像可影响参考试听，其他 CC 仍保留在文档／输出中。参考音色用于检查音高、节奏和轨道组合，不替代原工程的 SWAM／Kontakt 音色。`render()` 在音频线程运行，无锁且不分配／释放内存；控件／schedule 方法由一个消息线程持有。`prepare()` 和析构前宿主必须停止 render。宿主异线程恢复可以调用 `requestSilence()`，由消息线程新 schedule 解除静音；设备停止在 `releaseResources` 调用 `suspendAudio()`，`prepare()` 仅解除设备停止状态，不会解除待处理的宿主工程恢复静音。

**焦点与范围。**文本输入获得焦点时，`CommandDispatcher::resolveKey` 返回未消费；文本框保留自己的编辑操作。旧 `editorCommand` 仅保留兼容接口，新适配器必须只走 dispatcher，不可保留另一条硬编码快捷键路径。插件已识别但当前不可执行的命令应被消费，避免 Delete／Undo／Space 落到宿主。`GroupScope` 的 All 只改变可编辑组范围，不会全选音符；`SoloState` 单独决定监听。手动导航调用 `PlayheadFollow.userNavigated()`，明确播放／跳转调用 `transportLocated()`，自动跟随不会与正在进行的编辑抢窗口。

**CC 存在与可写性。**目录只收集真实事件或实际写入的非空笔画。擦除笔画即使有鼠标点也不能传给 `observeStroke`；应按擦除后的最终合并事件重建目录，擦光则消失，撤销则恢复。选“新增 CC”只保存界面目标，不制造起始默认值。`writeTarget` 必须得到当前可写来源列表；多个来源且没有明确首选时返回 `chooseSource`，避免一次笔画误写所有轨道。是否可编辑由各插件能力决定，目录本身从不强制只读。

**原有编辑保障。**`SnapPolicy.outputQuantum=0` 保留精度；Recover 仍由 adapter 最后执行 JUCE 原 tick 舍入，不能把 std::round 与其半数行为混为一谈。组边界仅统计真正可编辑音符；复制 anchor 必须指向抓取音符的副本。`EditSession` 只对当前 revision 的有改动手势调用一次提交；插件保管 baseline/draft 与撤销，取消时自行恢复 baseline。

## 构建、版本与发行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

独立构建包含 6 个测试程序和 2 个示例；作为子目录时默认关闭测试／示例，可通过 `PIANO_ROLL_CORE_BUILD_TESTS`、`PIANO_ROLL_CORE_BUILD_EXAMPLE` 开启。结果与限制见 `VALIDATION.md`，来源与许可见 `NOTICE.md`。

复用共用面板或钢琴窗组件时，先 `add_subdirectory` JUCE，再加入本模块并链接 `PianoRollCore::CommandUI` 和／或 `PianoRollCore::View`。已有 `juce::juce_gui_extra` target 时面板默认开启；可显式设置 `PIANO_ROLL_CORE_BUILD_JUCE_UI=OFF` 保持纯核心构建。

公共目录是唯一修改入口，产品包的 `vendor/PianoRollCore` 为固定发行快照，让下载者能够独立重建。共同功能修一次，两插件各自更新依赖、验证和编译；现有已安装的插件不会自动改变。

```sh
python tools/snapshot.py --refresh-manifest
python tools/snapshot.py --snapshot /path/to/lines/vendor/PianoRollCore
python tools/snapshot.py --snapshot /path/to/recover/vendor/PianoRollCore
python tools/snapshot.py --zip /path/to/PianoRollCore-0.5.0-Source.zip
```

快照工具需要 Python 3.10+ 标准库。目标已有不同内容时拒绝覆盖；升级前确认旧快照无本地改动后将其移出。发布脚本必须执行 `--verify <vendor路径>`；两产品记录相同 VERSION 和 `source_tree_sha256`。manifest 是内容一致性检查，不是来源签名。工具不打包构建目录、Git、缓存或其它 ZIP。

版本号只修改 `include/PianoRollCore/Core.h` 中的 `pianoroll::version`。CMake 从此读取项目版本；`--refresh-manifest` 生成对应的 `VERSION` 和清单，`--verify` 检查它们一致。直接包含源码头文件的调用方式仍然可用，不依赖构建时生成的头文件。
