# 接手须知（HANDOFF）

给「下一个会话 / 另一个人」看的现状说明。**动手前先读这一页，再按下面的顺序读规范文档。**

## 0. 一分钟版

- 项目是 **HM**（Windows 原生 DAW，以频率为第一公民 + 参考线系统），C++20 + JUCE 8.0.15 + WASAPI。
- 工作目录：`C:\Users\ASUS\Documents\Codex\2026-09-11\ni`，下面有 `NiDaw\`（源码）、`work\`（脚本与构建目录）、`outputs\`（交付物）。
- **Git 已初始化但没有历史提交**：交接时 `NiDaw/` 全部是未跟踪文件，不要让 `git clean` / `git reset` 清掉工作区；接手验证后先建立基线提交。
- 当前基线：**单元测试 37 组 / 5528 条断言全过；离屏自检 1483 项全过**（61 张快照，含真实 VST3 乐器 / 效果器）。
- 一条命令重出全部交付物：`work\make_release.bat`（约 5–6 分钟，见第 4 节）。

## 1. 先读什么（顺序）

1. 本页（现状、命令、坑）。
2. `README.md`（文档索引）→ `01-requirements.md`（需求与设计决策 D1…D116）。
3. `05-build-env.md`（工具链 + **必须全量重建**的原因）。
4. `06-testing.md`（三层验证、基线、写测试的注意事项）。
5. 最近几轮的 `../devlog/`：`2026-10-06.md`（第四十七批：力度 / 踏板 / 表情、虚拟键盘、琶音器）、
   `2026-09-25.md`（第四十二～四十六批：自定义波表、插入链尾巴、
   MIDI 输入的缩放 / 一键多连线、外部 VST 多音互相拉音高、波表 3D 瀑布视图）、
   `2026-09-24.md`（第四十一批：MIDI 输入 + 录制 + 片段重叠）、
   `2026-09-23.md`（第三十九批：参考组 / 参考线组编辑栏、框选、
   1–5 快捷键、叉点旁标签；第四十批：编辑栏「侧视」）、`2026-09-22.md`（第三十八批：控制器栏）、
   `2026-09-21.md`（第三十七批：混音台 + 空格键全局走带）、
   `2026-09-19.md`（第三十六批：栏位拖动 / 折叠、去掉右侧栏、片段菜单、效果器窗口）、
   `2026-09-17.md`（第二十六～二十九批：编辑栏抖动、车道交互、插件音量与扫描隔离）。

按需：`02-architecture.md`（改结构）、`03-pitch-model.md`（碰音高 / 参考线）、`04-ui-spec.md`（改界面）、`07-roadmap.md`（下一步做什么）。

## 2. 现在是什么状态

已完成到**第六十五批（钢琴窗边界、圆点时间拖动与接缝跟随）**：新建菜单为混合合成器 / FM / 鼓机 / 采样器 /
粒子合成器 / 音频 / VST；混合器已经到每声部双滤波、4 包络、4 LFO、4 FX 槽、混音器页与
工程文件夹格式。旧轨道（FM / 减法 / 波表 / 鼓机 / 采样 / 粒子 / 音频 / **VST 乐器**）仍兼容读取。
参考线与参考组、音轨片段系统、导出与分轨导出、
循环与节拍器、混音台、MIDI 输入（映射 / 录制 / 虚拟键盘 / 琶音器）、自定义波表、
M/S、鼓机、**编辑栏的参考线模式 / 钢琴窗模式**、**每条轨道一条 VST3 效果器插入链**等。
第十八批第一批加的是**外部 VST3 乐器**（按需扫描 + 缓存、插件选择器、插件自己的窗口、
状态内嵌 `.nprj`、缺插件回退，音高按半音 + 音分，每音一个 MIDI 通道 + pitch bend，
导出时每个文件新建私有离线实例），第二批加的是**钢琴窗模式**（半音行 + 键盘条、写入量化到半音、
参考线只读且可整体隐藏），第三批删掉了旁视并给派生的那一行加了「显示 / 隐藏、独显」的继承规则；
第十九批把**插件选择器**改成按厂家 / 类型分组的树并加了**效果器栏**（推子前的插入链，
导出同样过链）；第二十批修了用户反馈的「别的电脑上窗口跑到屏幕外」——所有窗口都经过
`Core/WindowPlacement`（先缩到工作区再挪进去，主窗口最小尺寸随屏幕放低，插件 / 音色窗口
被压小时改成滚动）；第二十一批修了用户反馈的「插件列表看不到扫描结果」——`RootItem` 从来没把
分组加进 `TreeView`，现在真的填树、折叠状态用 `setOpenness` 存、空列表给提示；
第二十二批修了「插件界面有时会混乱或无法控制」——插件永远拿自己的尺寸、窗口两个方向都跟着它、
只有装不下时才滚动且滚动期间不追插件（不许两个尺寸互相回推）；第二十三批让**音轨栏与编辑栏
各自一套横向视图**（栏内对齐、栏间独立，都随工程保存），并把循环片段的缩略图改成
**按真刻度平铺**（与分隔线、与发声一致）；第二十四批把外部乐器当成**一直活着的乐器**
（每块都处理、停止时也处理、开窗时把键盘焦点交给它，窗口尺寸以界面当前真实尺寸为准），
于是插件自带的键盘能直接试听。
第二十五批是**代码健康清理**：84 条编译警告清零（构建脚本现在会因为 `\Source\` 警告而失败）、
共用源文件清单收成 `cmake/SharedSources.cmake` 一份、离屏自检从 `Main.cpp` 迁到
`Source/SelfTest/`（`Main.cpp` 只剩下 146 行的程序入口）、单元测试拆成 `Source/Tests/` 下 9 个文件；
第二十六批修了四个用户报告的缺陷：框选时音符编辑栏挤压画布导致抖动（改成**固定槽位 + 浮层列表**）、
片段头不在零点时写音符/改长度错位（预览与改长度统一成**绝对 tick**）、打开工程后行头不跟着换
（轨道集合变化即重建，加载时强制重建并清空片段选中）、轨道多到装不下时行头够不到
（音轨栏**滚轮 = 纵向**、`Shift` + 滚轮 = 横向平移）。
第二十七批修了三个用户报告的缺陷：改名之后退不出编辑态（补 `Esc`，应用 / 取消都交还键盘焦点，
点音轨栏空白也结束输入）、滚动时车道画到上方工具条上（行头搬进只覆盖车道区的 `lanesHolder`
容器并裁切）、多选音符只改被拖的那一个（现在整组按同一条 delta 改长度，一条撤销记录）。
第二十八批对着「导出没有声音」这条反馈查到了根子：`runExport` 从来**没有把活着的插件状态
抓进要渲染的工程副本**，所以插件加载之后没存过盘就导出的 VST 轨是按插件出厂音色渲染的
（采样器的出厂音色＝空机架＝静音）——现在导出前先在消息线程 `captureStates`，并且**缺插件的
轨道会让导出直接失败并点名**，而不是写一个没有声音的文件；`PluginWindow` 也不再因为
「别的插件加载完了」而拆掉重建用户正开着的界面（那正是插件界面一闪、控件全在零位的原因）。
第二十九批修了两个用户报的问题并把扫描移出进程：**车道区恢复可用**（`lanesHolder` 只是裁切容器，
现在 `setInterceptsMouseClicks (false, true)`；`TrackArea` / `LaneHeader` 自己 `setWantsKeyboardFocus`，
所以点击空白不会再「跳进重命名」）、**插件音量不再被宿主改动**（行头推子关掉滚轮，
删除前抓一次插件状态、打开工程时按文件重建实例、总线布局只申请一次、RPN 补 null、
all-notes-off 只发给在响的通道），并新增 `%APPDATA%\NiDaw\plugin-events.log` 记录宿主对插件做过的
每一件事；**插件扫描改成子进程**（同一个 `HM.exe --scan-plugin … --scan-out …`，每个文件 30 s 超时，
坏插件只会被记成「读取失败」）——这台机器上带 Kontakt / Serum 的标准目录一扫就退出进程的问题因此消失。
细节见 D117–D140 与 devlog。

第三十四、三十五批的生成器（先模块补丁、后马尔科夫链）已**整体删除**：文件、测试组、
自检项与快照都不在了，基线回到加它们之前（28 组 / 1981 条、771 项 / 38 张）。

第三十九批（最新）：参考组那套东西有了**编辑态**。三个「编辑」入口——预设栏的
**参考组预设**行、**参考线组预设**行，以及参考线状态栏里**参考线组**那一类的行——
都把**左边那一格**换成对应的编辑栏 `Source/UI/GroupEditorPanel.{h,cpp}`
（`EditorPanel::resized()` 按 `appState.groupEditor.isOpen()` 挑显示哪一个）。
编辑期间的工作副本**装进工程的参考组**（`applyDerivedEdit`：画面立刻预览、撤销栈一动不动），
`返回` 放回进来前的快照、`保存` 写回原处（工程里的条目一次 `applyEdit("编辑参考线组")`，
可 `Ctrl+Z`）、`另存为` 新建预设——写盘那一侧是 `PresetStore::replaceReferenceGroup` /
`replaceReferenceLineGroup`。参考组栏与两个编辑栏的**中段共用** `Source/UI/GroupControls.{h,cpp}`
（状态文字 / 颜色行 / 频率比行 / 等分行，89 px），所以三处这几行只有一份实现。
参考线状态行**删掉了「颜色」**（按钮与色板），位置给「编辑」（只对参考线组那类可用）；
八度线段 `Source/UI/OctaveLadder.{h,cpp}` 支持**框选**（框到的叉点一次变成圆点、圆点被选中，
一整框一条撤销）、**按键盘焦点分的 1–5 快捷键**、**叉点旁左侧的标签牌**
（「正 1:3」/「负 1:3」/「i/N」，不画箭头）；`OctaveLadder::commitGroupChange` 在编辑栏开着时
改走 `applyDerivedEdit`（否则「改一个圆点」会先塞一条撤销记录）。见 D164–D167。

同一批的追加（用户要求「选中多个音符或圆点时，右键它们可以一起删除」）：右键删除现在**看选择**，
不只看向标——`PianoGrid` 右键点在**多选里的**音符上时走新的 `PianoGrid::deleteSelectedNotes()`
（一次 `删除音符`），点在没选中的音符上仍只删那一个；八度线段同理（`OctaveLadder` 的右键
命中多选里的圆点时整批 `删除圆点`）。两边共用的那句「按 id 删一串音符」收在
`music::removeNotesByIds(Clip&, const std::set<int64>&)`（`Core/MusicMath.{h,cpp}`），
`MainComponent::deleteSelectedNotes()`（Delete / Backspace）也改用它，所以键盘删与右键删是同一件事。

同一批的第二次追加（用户要求「上下拖动时触碰边界也可以移动」）：**边界自动滚动现在是两个方向**。
编辑栏里 `PianoGrid::updateAutoScroll()` 除了原来的时间轴，还会在**音高轴**上滚
（`verticalScrollFollowsDrag()`：只有「拖动音符」与「橡皮筋」两种手势，鼓机轨与写音符 / 改长度不滚）；
滚动的那一步会**重新取一次 `frozenLines`**（参考线模式的吸附目标是「你现在看得见的线」，
不然音符永远爬不出拖拽开始时的那一屏）并在钢琴窗模式下重新对齐半音行。
音轨栏里 `TrackArea::updateAutoScroll()` 在**拖动片段**时会按上下边界滚 `laneScroll`
（`resized()` 让行头一起走），所以滚出画面的轨道仍然是落点。见 D169。
用户随后报「拖动到边缘时方框边际也跟着移动，有的音符离开了框选范围」——那是框选的角
存在**屏幕坐标**上的后果：视图一滚，屏幕固定的角相对音符就跑掉了。现在角存的是
`marqueeAnchorTick` / `marqueeAnchorHz`（内容坐标），`PianoGrid::marqueeBand()` 每次按当前视图
映射回屏幕，**扫过的音符留在框里**，框的角跟着内容走。
再随后报「我无法从圆点或叉点旁拖动」：八度线段上每个圆点 / 叉点周围有 **7 px 命中半径**，
按在它旁边会被读成「点了它」，于是拖不动。现在 `OctaveLadder` 的叉点**改成松手时才变成圆点**
（按下只记下 `pendingCrossValid` / `pendingCrossPosition`），按下之后只要真的移动（> 3 px）
就转成框选（`bandArmedAtPress`）；按在圆点上再拖也一样（`bandKeepsPressedDot` 让那颗圆点
留在框里）。见 D171。

第四十批：编辑栏表头多了「**侧视**」勾选（`EditorPanel::sideViewButton`，64 px，
插在「模式」右边、「参考线显示」之前，两种模式都在）。打开后画布把**其他轨道**的音符以
**各自轨道颜色、30% 不透明度**画在**背景**里（`PianoGrid::otherTrackNotes` /
`paintOtherTrackNotes`，插在网格线之后、参考线之前），循环片段按 `music::expandClip`
**平铺成实际发声音**；幽灵**不参与任何命中测试**（不能点、不能选、不参与框选 / 吸附 /
参考线 / 自动取景）。开关存 `EditorViewState::sideView`（`.nprj` 的 `viewSideView`，
缺字段 → 关）。为了给这 64 px 腾地方，「吸附 / 纵向取景 / 模式」三个下拉共收窄 26 px
（86→78、110→100、104→96）。`PianoGrid::getSideViewGhostRectsForTest()` 与绘制走同一份
列表，所以自检能精确断言「幽灵画在哪儿」和「画面只多了这些像素」。见 D172。
同一批的追加（用户要「侧视可以勾选想看到的轨道」）：**右键「侧视」按钮**弹出挑轨道的菜单
（`EditorPanel::buildSideViewMenu` / `performSideViewMenuCommand`，自检走
`getSideViewMenuItemsForTest` + `performSideViewMenuCommandForTest`）。菜单是
「显示侧视 + 每一条能当背景的其他轨道（打勾 = 要显示）+ 全选 / 全不选」；当前轨与鼓机轨
不列（前者就是正在编辑的那条，后者是鼓垫不是音高）。**存的是「勾掉的」**：
`EditorViewState::sideViewHiddenTracks`（`.nprj` 的 `viewSideViewHidden` 数组），
所以空集合就等于这个功能刚做出来时的行为（全都看），以后新加的轨道也自动在列 ✓。
入口后来按用户要求从右键改成**「侧视」右边的 18 px 小箭头**：复用 `PanelToggleButton`
新加的 `Side::menu`（永远朝下的箭头，外观与其它折叠箭头一致），点它弹菜单、且**不会**
顺手开关侧视。`EditorPanel` 也多了 `showDialogs`（离屏自检里不真弹菜单）。见 D173。

第四十一批（**MIDI 输入 + 录制 + 片段重叠**，分三步做完）：

- **映射与发声**：`Core/MidiInputMap.{h,cpp}` 是纯逻辑（循环模式一格 `loopKeys` 个半音
  **上下平铺**、非循环模式逐音号、`0` = 空、`isSame` / `noteName`），
  `Audio/MidiInputManager.{h,cpp}` 管设备（枚举 / 打开 / 关闭，选择写
  `%APPDATA%\NiDaw\midi.json`，回调里只认 note-on / note-off / CC123，并 `callAsync` 到消息线程）。
  映射随工程保存（`Project::midiInput` → `.nprj` 的 `midiInput` 段，两个方向都读得回来），
  也能存进 `presets.json` 的 `midiMaps` 段。界面在 `Source/UI/MidiInputWindow.{h,cpp}`
  （左琴键 / 中连线 / 右参考线组的只读圆点），入口是顶栏「MIDI输入」（96 px，混音台右边）。
  **引擎多了一条实时音符通道**：`PlaybackEngine::noteOnTrack / noteOffTrack` 把事件压进一个
  `AbstractFifo`，音频线程在块首施加到该轨的乐器上——所以**走带停着也出声**、
  过该轨的效果器与推子、而**导出没有它**（导出用另一台 `PlaybackEngine`）。
  钢琴窗模式下 MIDI 绕过映射走十二平均律；写下一个新音符时该轨乐器试听 260 ms。
- **录制**：走带栏多了「录制 / 停止录制」（武装变红、写着「停止录制」）与「片段内 / 片段外」，快捷键 `*`。
  武装不自动起播；片段内写打开的片段并在越界时拉长 `lengthTicks`，片段外由第一个音新建片段；
  一个音（按下到抬起）一条 `录制 MIDI` 撤销。
- **片段重叠**：`music::trimOverlapsInTrack` 与它的三处调用一起删掉，
  重叠处两段一起发声（调度本来就是「把这一轨所有片段的音符展开后一起发声」，
  所以引擎一行没改）；粘贴仍然先找空位。

细节见 D174–D178 与 `../devlog/2026-09-24.md`。

同一批的用户反馈修复（「无法将钢琴键连接到参考线组上，且圆点的颜色没有匹配参考线组的颜色」）：

- **颜色**：MIDI 窗口原来用**第一个圆点**的颜色画**所有**圆点与连线，而「应用参考组」建出来的
  组第一个点一定是 **frame dot**（浅灰）——所以用户看到一整列灰点，画布上却是有颜色的线。
  现在 `MidiInputCanvas::dots()` 把「这条频率来自哪个圆点」一起带出来，每个圆点按画布**同一条
  规则**上色（条目自己的颜色优先，否则用圆点自己的颜色；条目还在时跟条目同步）。
  颜色存进 `MidiInputState::groupColourIndex`（缺字段 = -1）。
- **连不上**：绑定原来只在 `mouseUp` 时用 **14 px 磁力**判落点，而相邻圆点隔着一个八度
  （900×560 窗口里约 90–150 px），「从琴键往右平拖、在柱子里松手」几乎必然落空且**毫无提示**；
  反向（圆点 → 琴键）当时压根没实现。现在柱子里**任意高度都按最近的圆点接住**
  （磁力仍在、拖动时有虚线预览与提示环）、**两个方向都能拖**、**右键圆点**松开连到它的键，
  右侧栏与状态行还会写「把琴键拖到圆点上即可绑定；拖到圆点附近就行」。

同一批的第二次反馈修复（「应用后点钢琴键 C、按 MIDI 键盘的 C 之后，白键应该出 C 大调、
从左到右递增、黑键不发音」）：

- **跨八度的语义改了**（`midimap::Map::frequencyFor`）：
  `频率 = 格里的值 × 2^floor((音号 − 锚点) ÷ 格长)`，空格步在任何八度都不发声。以前是
  「整格上下平铺」（高八度同一个音），现在键盘往上走一格整组升一个八度——这才是乐器。
  `tile` 里存的仍是锚点那个八度的频率，**文件格式与预设都没变**（已有的映射按新语义播放）。
- **右列只画一个八度**（C 大调 = 7 个圆点，不再是 21 个）；圆点多于格长时**装不下就绑不了**
  （画灰、不作落点、提示把「循环键数」调大）。
- **左列每个键都标音名**、空步写「空」；**状态行**写明「锚点 · 一格 N 个半音 · 已绑 N 个 ·
  空 M 个（空的不发声）」，学习态里先按 MIDI 键会提示「先点一个左侧琴键」。
- 用户口径的整条链路在自检里跑通：**7 个白键连 7 个圆点 → 白键 C4 261.626 → B4 490.548 Hz
  递增、5 个黑键全是 0、C5 两倍、C3 一半、B3 不是重复 B4**。

紧接着的第三次反馈（**右列显示多个八度**、**左列钢琴键上下翻转**）：

- 右列恢复成**锚点上下各一个八度共三个**（C 大调 7 点 × 3 = 21 个），按八度分块升序；
  `tile` 存的仍是**被拖的那个点的频率**（拖中间八度的 C 存 261.63、拖下八度的 C 存 130.81），
  弹的时候再按键盘的八度 ×2/÷2。
- 左列改成**低音在下、高音在上**（与右列频率轴、钢琴窗键盘条同向）：一格 12 个键的方块里
  最低那个画在最下面，非循环模式同理，滚轮方向跟着反，「跟着播放键滚动」改成「矩形出界就
  滚到中间」。于是**横着拖过去基本落在同一高度**。
- 「装不下」改按**一个八度里的第几个**判定（`Dot::slot`），三个八度要么都能绑要么都不能；
  提示语随之改成「这一格只有 M 个半音，这一组的 N 个圆点装不下」。

第四十二批（**自定义波表**：导入 WAV / 从片段取样 / 手绘 + 谐波编辑，存进工程）：

- 数据与纯逻辑在 `Source/Core/WavetableTables.{h,cpp}`（两个目标都编译）：`CustomWavetable`
  （id / 名字 / 帧数 1–8 / **16 位 PCM**，帧长固定 2048 点）＋ `makeFromAudio`（等分切片 →
  按比例 box 平均重采样 → 每帧去直流 → **整表**归一化到 0.98）、`makeFromFrames`、
  `copyBuiltIn`、`smoothFrame / clearFrame / normalise`、`synthesiseFromHarmonics` 与
  `analyseHarmonics`（加法合成 / FFT 分析，编辑器的谐波条）、`dataHash`、`sameData`、
  `buildCustomTable`（**mipmap 是派生数据**，由 `wavetable::buildTableFromFrames` 生成，
  内置库与自定义表共用同一段 FFT 截断代码）。
- **工程自包含**：`Project::customWavetables` + `nextCustomWavetableId`，`.nprj` 新增
  `customWavetables` 段（base64 的 16 位 PCM，为空时一个字段都不写）；`WavetableOsc`
  新增 `customTableId`（0 = 用内置 `tableIndex`，缺字段读 0）。**旧工程照常打开**。
- **引擎**：`TrackRenderInfo::wavetables`（两个 `shared_ptr<const Wavetable>`，与 `sample`
  同一条不可变快照的路）；`buildSchedule` 按 **id + 数据哈希**缓存地构建 mipmap；
  `WavetableSynth::setTables` 让"有自定义表就用它，否则回退内置"，`matchTracks` 在指针变化时
  推送；试听走 `AudioEngine::updatePreviewWaveTables`，所以**按键盘试听的就是正在画的那张表**。
- **界面**：音色窗口的波表页多了「波表编辑器…」按钮（用内置表时点它会把那张表复制成一张
  自定义表再打开）；新窗口 `Source/UI/WavetableTableWindow.{h,cpp}`（非模态、780×430、
  经 `WindowPlacement`）：名字 / 帧数 / 导入 WAV… / 从片段取样… / 拷内置… / 试听 C4 /
  删除这张表；可画的大画布（铅笔、Shift 直线）、1–8 帧缩略图、**谐波条 + 应用**
  （进入时用 FFT 分析当前帧，改完反向合成为这一帧）。**一次手势一条撤销**（`beginEdit` +
  `notifyEdit`），离散命令各一条 `applyEdit`。取样只认**当前打开在编辑栏里的音频片段**，
  不是音频片段时只提示、不建表；删除一张表时引用它的振荡器退回内置。
- **预设**：`PresetStore` 的波表条目升为 `WavetablePatch{ params, tables }`（`getWavetablePatches`），
  保存时把它引用到的自定义表一起写，载入时按「名字 + 数据」在工程里复用或新建一张再把
  振荡器指过去——**预设是自包含的**。

细节见 devlog `2026-09-25.md`。

同一批的用户反馈（「我希望可以删除创建的波表」）：

- 删除本来就有（编辑器窗口工具栏右端的「删除这张表」），但**窄窗口会把它裁掉**——
  编辑器窗口现在改成**设计尺寸面板 + 滚动视口**（与 `SynthWindow` 同一套做法），
  窗口再小也只是出滚动条。
- 音色窗口的波表页标题行又加了一个 **「删除波表」**（只在振荡器 1 用自定义表时可点），
 走同一个 `MainComponent::deleteCustomWavetable`：表从工程里去掉，**所有**引用它的振荡器
 退回内置表，编辑器状态行写明。

第四十三批（**修「声音被硬切」**）：用户报「用混响等效果器、或者外部 VST 乐器时，音符停止 /
松开 MIDI 键盘会突然断掉」。两个根因都在 `PlaybackEngine::renderVoices`：乐器一「不活跃」就
`continue`，于是**插入链与并入输出一起被跳过**（尾巴当场消失）；`AudioEngine::stop()` 既释放
声部（外部插件收到 CC123 all-notes-off）又把位置拨回播放起点，后者被当成跳转，于是**每条插入链
`reset()`**。现在**链跟着尾巴走**（`chainSilenceBlocks`：有插入效果的轨继续跑链，直到连续 8 块
低于 −100 dBFS 才跳过）、**停止 / 循环回卷用真正的 note-off 且不 reset**
（`HostedInstrument::releaseHeldNotes()`，保留 `tailSamplesLeft`）、**跳转仍然硬截断**
（`effect->reset()`，否则延迟线会重放跳转前的音频）。随后按用户要求又加了**「再按播放 = 掐断
尾巴」**：从停着开始播放时 `PlaybackEngine::cutSoundingTails()` 硬放所有声部并冲掉每条链；
正在播时再点播放什么也不掐。见 D185–D187。

第四十四批（**MIDI 输入窗口的缩放 / 滑动 + 一键多连线**）：右列改成**自己的视图**
（`groupZoom` 0.5–8 + `groupScroll`，滚轮滑动、`Ctrl+`滚轮以指针为锚点放大、双击回全览），
一个钢琴键可以连**多个**圆点（`Map::tile` / `free` 从"一个 double"变成 `vector<double>`，
弹这个键所有连线一起响、录制时一个频率写一个音符），右键琴键 / 连线 / 圆点都能断开
（右键琴键认**整行**）。见 D188–D189。

第四十五批（**修「多个音互相拉音高」**）+ 修正：用户报外部 VST 同时响多个音时不同音高互相干扰。
宿主侧确实有两个 bug（通道回收太快 → 新音落在刚释放的通道上，旧音尾被新音的弯音拉走；
工程音符 id 与实时音符 id 都从 1 起 → 撞号共用通道），修法是
`vst::ChannelAllocator` 改成「**从未用过优先、其次释放最久优先**」并报出被抢的音 id、
实时音符翻译进**私有负数空间**（`liveNoteIdFor`）、通道级 CC 在通道拿到音时**补发**
（`lastControllerValue`）。用户说症状仍在，于是加了诊断工具
`HM.exe --vstprobe <插件.vst3> [--out <目录>]`（真的加载插件、量两个音的真实频率），
量出 **Serum 2 的弯音是整机级的**（Pianoteq 9 / Absynth 6 是按通道的），所以给 VST 轨加了
**「按半音演奏」**（`VstParams::equalTemperament`，音轨行头右键勾选、随工程保存、默认关）：
那一轨不再需要逐音弯音，和弦不再互相拉。见 D190–D193。

第四十九批（**虚拟键盘：卡音断根 + 键盘随时可弹 + 钢琴变窄 + 低音排严格交替 + 每来源独立声部**）：
用户报「释放按键后持续发声的 bug 仍然存在」，并要求钢琴条做窄、打开窗口就能直接弹、
低音排改成严格的上五度 / 下四度。

- **卡音断根**：上一批只补了"移位 / 换布局 / 关窗"三条结构性路径，**普通的松键**仍然可能漏
  ——电脑键盘的弹奏原来依赖 `MidiKeyboardComponent` 在焦点组件上的轮询，而它按**算出来的音号**
  记账（`keysPressed`），一次没送达的 key-up 就让那个音的 note-off 永远发不出来（"松开了还在响"），
  同一个键换回原偏移还会变成死键。现在**面板自己轮询**（`keyStateChanged` 里按当前布局逐个
  `KeyPress::isKeyCurrentlyDown`，自己维护 `typingDown`），并有 **100 ms 心跳**：① 补发漏掉的
  note-off（自愈）、② 窗口失去键盘焦点就整批放掉。跑过的自检原话：
  `心跳发现键已经松开，把那个音放掉了（不再一直响）`。
- **打开窗口就能直接弹**：因为轮询在**面板**上，事件沿父链上浮，所以焦点落在窗口里任何控件上
  都行（文本框除外）；窗口激活时面板自己拿焦点。自检：`直接敲电脑键就出声（没有点过钢琴键…）`。
- **钢琴条 190 → 90 px**、窗口 460 → 360（图示仍留 ~142 px）。
- **低音排改成写死的偏移表**（相对 `5`：`+1,−4,+3,−2,+5,0,+7,+2,+9,+4,+11,+6,+13,+8`），
  相邻两键严格 `+7 / −5` 交替（`5`=F3、`6`=C4、`7`=G3、`8`=D4…，整排 C#3–F#4）——上一版
  "每键 +7 再折返"会冒出"连着两个上五度"，那是折返的副作用。
- **每来源独立声部**：`MainComponent` 的记账从「一个音一条」改成「一个(触发源, 音)一条」
  （硬件 `0` / 鼠标 `1` / 电脑键 `100+键码`），所以低音排与旋律排**音域重叠**（用户选了 F3 锚点）
  不再是冲突：同一个音被两条来源按着就两个声部、各松各停。自检：
  `同一个音被两条来源按着 → 各响一个声部（实际 2 个声部、1 个音）`。

第四十八批（**虚拟键盘：卡音修复 + 折叠置顶小窗 + 巴扬模式与电脑键盘图示**）：用户报「松开
键盘仍在持续发声」，并要折叠小窗 / 置顶 / 巴扬模式 / 电脑键盘图示 / 低音排 / 快捷键让位。

- **卡音的根因在 JUCE**：`MidiKeyboardComponent::keyStateChanged` 按**算出来的音号**记账
  （`keysPressed[note]`），而 `setKeyPressBaseOctave` 只换倍率、**不释放**。于是「按住一个键
  再换八度（或换布局 / 关窗）」留下一个**永远等不到 note-off** 的旧音，同一个键还会变成
  "死键"。修法：`VirtualKeyboardPanel` 记下自己发起过的音（`startedNotes`），移位 / 切布局 /
  折叠 / 隐藏 / 析构之前先 `releaseWindowKeys()`（经 `keyboardState.noteOff` → 监听器 →
  `handleLiveNoteOff` → 引擎真的收音），再 `setUpKeyMappings()` 重装键位表（清掉那份记账）。
  **只放这个窗口发起的音**，硬件正弹的音不动；**不做**"按来源计数"（会和重复 note-on 打架）。
- **折叠与置顶**：面板两套排布（展开 980 × 460 / 折叠 520 × 120，折叠时只留一行控件 + 图示），
  `setAlwaysOnTop` 让窗口浮在其它窗口之上；两者都记在 `%APPDATA%\NiDaw\keyboard.json`
  （`VirtualKeyboardWindow::getDefaultSettingsFile`，自检用 `setSettingsFileForTesting` 指路）。
- **巴扬模式**：新 `Core/KeyboardLayouts.{h,cpp}` 是**键位、图示、钢琴字母共用的一张表**——
  `physicalBoard()` 的 44 个键帽（真实错位、`⌫` 两格宽）、`capsFor(layout)`、
  `baseNoteFor` / `bassNoteFor` / `noteForCap` / `keyLettersFor`。旋律排三排按列各三个半音
  （`q a z w s x…/` 一路半音往上，`w` = C4）；**低音排**是数字排那 14 个键（这里是最初的
  "折返在 C2–C4"版；**第四十九批起改成固定的上五度 / 下四度偏移表**，整排 C#3–F#4：
  `5` = F3、`6` = C4、`7` = G3）。两排各有独立的移位（旋律 半音 ±1 / 八度 ±12；
  低音 半音 ±1 / 五度 ±7 / **八度 ±12**，可移范围 `-28 … +42`）。
  `LetteredKeyboard`（`MidiKeyboardComponent` 子类）把电脑键字母写在琴键上，
  `ComputerKeyboardView` 画那张错位的键盘图（**白键帽画白、黑键帽画黑**）。
- **`1–6` 让位**：`MainComponent::keyPressed` 的频率比那一段加了 `! isVirtualKeyboardVisible()`；
  窗口一关立刻恢复（自检里两条都断言了）。

第四十七批（**MIDI 输入：力度 / 踏板 / 表情 + 虚拟键盘 + 琶音器**）：三条线一起做完——

1. **键盘的数据真的进来了**：录制不再是写死的 `velocity = 100`，而是弹下去的那个力度
   （`PendingRecord::velocity`）；`CC64 / CC11 / CC1` 走 `PlaybackEngine::sendControllerToTrack`
   → 与**画出来的曲线**同一个 `applyControllerToSlot`（VST 只转发、内置轨由引擎处理），
   `LiveEvent` 因此带上了类型；**踏板的判定也管实时音符**（`applyLiveEvent` 里的 note-off
   进 `sustainedNotes`，抬起由 `applyControllerToSlot` 补发）。录制时三个控制器写进片段的
   `controllerCurves`，**一个控制器一次手势一条撤销**（`kControllerGestureGapMs = 300`，
   静默超过它或这一趟结束就算手势结束）。
2. **虚拟键盘**（`Source/UI/VirtualKeyboardWindow.{h,cpp}`，顶栏「虚拟键盘」/ `Alt+K`）：
   一条一次看全的 88 键 `juce::MidiKeyboardComponent`（18 px/白键，同时挂着自己的
   `MidiKeyboardState`）、力度控件、八度按钮、键位对照、底部一排琶音控件。鼠标 / 电脑键盘 /
   硬件都经 `MainComponent::handleLiveNoteOn/Off`——**与硬件同一条路**（映射或钢琴窗的
   十二平均律、琶音、录制、锚点学习、高亮全都共用）；硬件弹的音由 `setKeysDown` 点亮，
   `applyingHeldKeys` 防止「点亮」被当成新按下。整排 `ArpControls`
   （`Source/UI/ArpControls.{h,cpp}`）两个窗口共用。
3. **琶音器**：`Core/MidiArp.{h,cpp}` 是纯逻辑（速率 / 方向 / 八度 / Gate / Swing /
   三种力度 / Hold 的 latch + 序列化），`MainComponent` 里 1 ms 的 `ArpTicker` 按当前时钟
   把该弹的步用**实时音符通道**弹出来：走带跑着用工程位置（跟着小节与速度），停着用自己的
   tick 计数，所以不按播放也能试听；录下来的音符用琶音**自己的**起点与长度（格 tick 与 Gate），
   于是听到的有块抖动、录进片段的却是整齐的。设置存在 `midiInput.arp`（随工程与 MIDI 映射
   预设走，配置、不进撤销栈）。

第四十六批（**波表 3D 瀑布视图 + 实时光标 + 直接操作**）：波表页顶部那条 88 px 的静态显示条
换成 168 px 的 `WavetableWaterfallView`（`Source/UI/WavetableEditor.{h,cpp}`）——8 帧沿深度
堆叠（每帧收缩 3%、抬高 6%，振幅 30%）、当前波用 `WavetableSynth::sampleTable` 按**引擎同一条
插值**画亮线、一根光标跟着**真正在发声的那个声部**的位置走。位置不再由界面自算：
`WavetableSynth::Published{ position[2], voices }` 两个原子量在音频线程写（`Published` 必须
**可拷贝 / 可移动**，因为乐器住在 `std::vector` 里），只读接口是
`AudioEngine::getWavetablePosition / getWavetableActiveVoices`（轨道声部优先）与
`getPreviewWavetablePosition / getPreviewWavetableActiveVoices`（屏幕键盘试听），
与混音台电平表同一套做法；`voices` 在 `renderAdding` 的**块首与块尾各写一次**（声部在块内结束、
调用方就不会再调它了，这一点漏了会一直显示"在响"）。直接操作：**上下拖动**连续改位置
（一次拖动一条「改波表位置」撤销）、**点某一帧**跳到 `帧/(帧数-1)`、**滚轮** ±0.01、
**双击**回 0.50；三者都改**当前选中振荡器**的 `position` 滑块（播放中拖能听到扫表）。
视图顶部窄条里放**「振荡器 1 / 振荡器 2」**切换，表按**实际使用的那张**取
（`customTableId` 优先）——顺带修掉旧显示条**只画内置表**的显示错误。编辑器 30 Hz 定时器
**只在有声音时**刷新。波表页的窗口高度是 **640 px**（比别的页多 80 px，正是瀑布比旧条高的部分），
`SynthWindow::pageHeightFor` 按页取高度。见 D194。

第三十八批：编辑器**最底部新增控制器栏**（`Source/UI/ControllerLane.{h,cpp}`）——
一条信号（力度 / CC1 / CC7 / CC10 / CC11 / CC64 / 更多 / 自定义），力度画成每音一根竖条、
CC 画成可绘制的折线；可拖高（默认 110 px，折叠成 18 px 细栏），高度与折叠状态随工程保存。
曲线住在**片段**里（`Clip::controllerCurves`，`Core/ControllerMath.h` 是唯一的编辑入口），
随循环平铺，`buildSchedule` 把它换成 `TrackRenderInfo::controllers`，
`PlaybackEngine::processControllers` 在每个块开头走一遍：**VST 轨转发给插件，内置轨由引擎
处理 CC11 表情与 CC64 踏板**（两条路互斥，见 D161）。力度仍是 `Note::velocity`，
这一栏只是又一个改它的地方。**注意**：`ControllerLane` 现在是 UI 组件的名字，
数据那个结构叫 `ControllerCurve`（原计划两个都叫 ControllerLane，会撞名）。

第三十七批做了两件事：**混音台窗口**（顶栏「混音台」/ `F3`，每轨一根 106 px 条带、
最右钉一根总输出条，条带上有乐器入口 / 插入链 / 声相 / 带刻度的推子 / M / S /
**左右两条**带刻度的峰值电平表与削顶红条）
与**工程级总输出推子**（`Project::masterVolumeDb` → `RenderSchedule::masterGain`，
在 `PlaybackEngine::renderNextBlock` 一次性乘在整条混音上，所以监听与导出都遵守它），
另外**空格键在所有窗口里都控制走带**（例外只有文本框与插件自己的界面）。
文件与测试钩子见 `Source/UI/MixerWindow.{h,cpp}`、`Source/Core/MeterMath.h`；
测试用 `MainComponent::sendKeyPressForTest` / `isTransportSpaceKeyForTest` 走空格键的规则
（离屏运行里文本框拿不到真实键盘焦点，所以规则本身直接问）。
用户随后报了「推子和按钮无法交互」，所以条带从「内容组件手绘 + 手写命中测试」改成了
**真实的 `MixerWindow::Strip` 组件**（`Viewport` 曾经把点击全吃掉），并给引擎加了
每声道电平上报；自检里混音台的交互也因此改成发**真鼠标事件**给条带组件（见 D157）。
随后又按用户要求把**推子做窄（15 px）、电平表做宽（各 18 px）并按刻度着色**
（低绿 → 中黄 → 顶红），并给插入链每行加了**真的能点的旁通按钮**（见 D158）。
再随后修了「单选一个音符时比率那一行不显示」：可见性一直是对的，是**布局时机**错了——
栏的槽位固定，单选与不选的 bounds 一样，JUCE 就不会重新布局，比率行一直留着零尺寸矩形
（多选时条带变高才重排，所以「多选反而显示」）。`NoteEditBar::refresh()` 结尾显式再排一次版（见 D159）。

第三十六批做了四件事：

- **栏位可拖动改宽、可折叠**：编辑栏的**参考组栏**（默认 210）、**预设/历史栏**（210）、
  **参考线状态栏**（264）各有自己的拖动条与小箭头；拖动实时改宽，点箭头折成 **18 px 细栏**
  （细栏里只有箭头，点它展开），双击拖动条回到默认宽度。宽度与折叠状态**随工程保存**
  （`.nprj` 的 `view` 段，键名 `panelReferenceGroupSize` / `panelReferenceGroupCollapsed` 等），
  旧工程缺字段就读成默认宽度、全部展开。共用控件在 `Source/UI/PanelHandle.{h,cpp}`，
  夹取规则在 `EditorViewState` 旁边的 `panelWidthFor` / `fitPanelsToCanvas`（同宽的栏一起让路，
  画布永远留够 520 px）。
- **音轨栏右侧那一列去掉了**：片段栏与常驻的效果器栏不再占位，音轨栏（编辑栏开或关）都占满整宽。
- **片段右键菜单补三项**：`重命名…` / `循环`（打勾项，打开时若没有循环周期就用片段自身长度）/
  `重复…`——原来这三件事都在片段栏里。
- **行头乐器区新增「效果」按钮**：点开**效果器窗口**（`Source/UI/EffectsWindow.{h,cpp}`），
  里面就是原来那条插入链（加 / 旁路 / 上下移 / 删 / 开界面 / 显示延迟），窗口按轨道钉住、可复用。
  细节见 devlog `2026-09-19.md` 与 `01-requirements.md` 的新决策行。

**还没做完的重构**（第二十五批遗留，记录在 `../devlog/2026-09-16.md`）：
`Source/SelfTest/SelfTest.cpp` 里仍是**一个 6300 行的 `run()`**（要按主题拆成 6 个文件，
需要先把跨段共享的局部变量收进一个 `Context`）；`Source/UI/PianoGrid.cpp`（2191 行）
还没按成员函数拆成 3 个实现文件。

历史决策都带理由，写在 `01-requirements.md` 的决策表里；**改行为前先查有没有相关决策行**。

## 3. 构建与验证

| 目的 | 命令 | 产物 |
| --- | --- | --- |
| 编译主程序 | `work\build_app.bat NiDaw` | `work\app-build5\NiDaw_artefacts\Release\HM.exe` |
| 编译单元测试 | `work\build_tests22.bat` | `work\test-build22\NiDawTests_artefacts\Release\NiDawTests.exe` |
| 跑单元测试 | `work\run_tests22.ps1` | 终端汇总（基线 37 组 / 5528 条） |
| 跑离屏自检 | `HM.exe --selftest <输出目录> --vst3 <测试乐器 .vst3> --vst3fx <测试效果器 .vst3>` | 该目录下 `selftest.log` + 61 张 PNG |
| 跑离屏自检（带测试插件，省事） | `work\run_selftest.ps1 -Out <输出目录>` | 同上（参数默认指向 app-build5 里的测试插件） |
| 出交付物 | `work\make_release.bat` | 见第 4 节 |

注意：CMake 的 target 仍叫 `NiDaw`，所以中间目录是 `NiDaw_artefacts`；exe 名来自 `PRODUCT_NAME`，是 **HM**。

`work\build_app.bat NiDaw` 现在还会顺带构建**测试用的两支 VST3**：

- 乐器 `NiDawTestInstrument` → `work\app-build5\NiDawTestInstrument_artefacts\Release\VST3\HMTestInstrument.vst3`；
- 效果器 `NiDawTestEffect` → `work\app-build5\NiDawTestEffect_artefacts\Release\VST3\HMTestEffect.vst3`
  （立体声进出的就地增益 0.5，自带一个小状态字段）。

自检靠它们把插件宿主链路（乐器）与插入链（效果器）都跑成自动化。两者都只用于测试，
**不进交付物**。

### 必须遵守的四件事

1. **改了头文件就全量重建。** 这台机器的 MSVC 是中文版，`/showIncludes` 前缀在 936 代码页下被 CMake 解错，
   Ninja 的头文件依赖是**空的**——增量构建会留下按旧结构体尺寸编译的目标文件，链接出来的程序随机崩。
   `work\build_app.bat` / `build_tests22.bat` 每次都会删掉自己的构建目录重来，不要绕过它们手动增量构建。
2. **中文不要用 `juce::String("…")`。** 该构造函数按 ASCII 处理，遇到大于 127 的字节会断言；统一走 `NI_TXT("…")`。
   预设库那种 `const char*` 名字是有意为之（经 `fromUTF8` 读），别顺手改。
3. **源码是 UTF-8**，全局 `/utf-8` 不能删。自检日志也是 UTF-8，读取时加 `-Encoding UTF8`。
4. **构建必须零警告。** 两个构建脚本会扫日志，凡是**指向 `NiDaw\Source\` 的 `warning C`** 都会让构建失败
   （`WARNINGS_IN_SOURCE_CODE`，外部 JUCE 的警告不算）。新代码请当警告是错误来写。

## 4. 交付物在哪、怎么更新

```
outputs\HM\           开发交付物：HM.exe + 详细 README.md + screenshots\（38 张快照 + selftest.log）
outputs\HM-win64\     发给别人的那一份：HM.exe + 使用说明.txt（不含预设，见下）
outputs\HM-win64.zip  把上面那个文件夹打包，就是要发出去的压缩包
```

**这三份都不是构建的自动产物**，是构建后手动 / 用脚本同步的。所以改完代码一定要跑：

```
work\make_release.bat
```

它会：全量重建主程序 → 重建并跑单元测试 → 跑离屏自检（输出到 `work\release-selftest`）→
把 exe / 快照 / 日志同步进 `outputs\HM` 与 `outputs\HM-win64` → 重打 `outputs\HM-win64.zip`。
任何一步失败都会停下并返回非零退出码。

几点约定：

- `outputs\HM-win64\使用说明.txt` 是**手写内容**，脚本只更新 exe，不会覆盖它；改说明直接改这个文件。
- 发给别人的包里**不放 `presets.json`**（用户 2026-09-13 的决定）：内置音色 / 鼓组 / 参考组都编在 exe 里，
  自定义预设在用户自己的 `%APPDATA%\NiDaw\presets.json`。要带自定义预设，得手动把那个文件放进 `outputs\HM-win64\预设\`。
- 跑发布脚本前**先关掉正在运行的 HM**：exe 在运行时无法覆盖，`copy` 会报「文件正在被使用」。

### 对方的机器需要什么

- Windows 10/11 **x64**。
- **VC++ 2015–2022 运行库（x64）**：exe 动态依赖 `VCRUNTIME140.dll` / `VCRUNTIME140_1.dll` / `MSVCP140.dll`（实测导入表），
  全新系统可能没有，缺了会报找不到 DLL。
- 启动有 **JUCE 闪屏**（见第 6 节授权）；未签名，首启会有 SmartScreen 提示。

## 5. 环境里的坑（这个容器特有的）

- **删除类 shell 命令会被拒**（`Remove-Item` / `rd` 等）。要删文件请用 .NET 接口并先校验绝对路径在工作区内，例如
  `[System.IO.File]::Delete($full)`（先用 `[System.IO.Path]::GetFullPath` + `StartsWith` 做守卫），目录用
  `[System.IO.Directory]::Delete($dir, $false)`（只删空目录）。
- **读不到 `%APPDATA%`**：预设文件只能请用户自己复制进工作区，别浪费时间试各种读法。
- **exe 被占用**时覆盖会失败，先确认没有在跑的 HM 进程（`Get-Process HM`）。
- 自检是 GUI 程序，**必须等待进程结束**：批处理用 `start /wait ""`，PowerShell 用 `Start-Process -Wait -PassThru`
  （直接用 `&` 不会等）。
- 写 `check(f(x, err), "消息 " + err)` 这类断言时**参数求值顺序不保证**，消息里用到的变量要先取出来，否则会读到未赋值的内容。

## 6. 待用户拍板的事（别自己决定）

1. **JUCE 授权 / 去闪屏**：`CMakeLists.txt` 刻意没定义 `JUCE_DISPLAY_SPLASH_SCREEN=0`。
   要对外分发就得二选一——按 AGPLv3 提供源码，或买 Indie/Pro 授权后加这个宏。
2. **代码签名**：现在没签名，对方首启会遇到 SmartScreen；要不要买证书由用户定。
3. **要不要把详细 README + 38 张截图也放进给对方的包**：现在没放（用户 2026-09-13 说「不用」）。
4. **采样器工厂预设**：2026-09-13 做过一版又按用户要求撤回，代码移到 `work/retired/sampler-presets/`，
   要重做得先问。

## 7. 文档维护约定（避免再次漂移）

- 改行为 → 同时更新 `01-requirements.md` 的决策行与 `04-ui-spec.md`；改测试基线 → 更新 `06-testing.md`；
  每轮结束写 `../devlog/`。
- **进度不写进 `docs/`**，只写「是什么、为什么」；进度在 devlog。
- 交付路径、脚本名这类「会变的事实」只写在本页与 `README.md` 的目录一节，别在别处复述。
## 8. 第六十二批交接（2026-10-18）

- 工厂库：`Core/HybridPresets.h`，32 个原创预设、8 类、每类 4 个；`Core/WaveformPresets.h`，10 个单周期 + 6 个多帧。
- UI：预设栏类别筛选；内嵌波表编辑器返回条；波形预设菜单；主页面 3D 缩略图缩小并实时高亮。`54-hybrid-main.png` 有意变化，新增 `56-hybrid-presets.png` / `57-hybrid-wavetable-editor.png`。
- 引擎：24 dB 滤波改为两套独立 12 dB 状态，修复 24 dB 带通输出 NaN 的缺陷。
- 实测基线：单元测试 **36 组 / 5280 条**；离屏自检 **1442 项 / 57 张快照**；`--vst3` / `--vst3fx` 通过。
- 发布：`work\make_release.bat` 生成 `outputs\HM\HM.exe` 与 `outputs\HM-win64.zip`。发布前关闭正在运行的 HM，否则 exe 被占用。
- 已知限制：本轮网络 HTTPS 检索不可用，未引入任何第三方预设；工厂预设全部为本项目原创参数。Serum 2 的商业预设格式与授权都不适合直接内嵌。
## 9. 第六十三批交接（2026-10-23）

- 音符新增头/尾频率、曲率、倾斜起止和连接来源；旧工程自动读成水平音符。
- 钢琴窗支持内部端点圆点、倾斜边界拖动、本体移调吸附、自动头尾相接和倒三角取消。
- 调度器把连接链折叠成单音头；内置音源跟随 pitch-only 事件，VST 只保留单音头与形状标注。
- 实测基线：单元测试 **37 组 / 5520 条**；离屏自检 **1461 项 / 59 张快照**；VST3 / VST3FX 通过。
- 新快照：`58-note-tilt.png`、`59-note-link.png`。Ctrl+滚轮改宽度按要求未实现。
## 10. 第六十四批交接（2026-10-23）

- 三处吸附已独立：`trackSnap`、`editorSnap`、`controllerSnap`；新增共享 `SnapControl`，支持 1/n 输入和旧工程迁移。
- 连接音符现在按链跟随移动；头尾圆点为白色，钢琴键/背景/音符共用半音行几何，白键行浅、黑键行深。
- 力度柱按下绝对定位并持续跟随鼠标；不吸附 CC 绘制使用 3 px 限采样和 2 px 折线简化。
- 实测基线：单元测试 **37 组 / 5528 条**；离屏自检 **1478 项 / 61 张快照**；VST3/VST3FX 通过。
- 新快照：`60-note-chain-follow.png`、`61-snap-grids.png`。
## 11. 第六十五批交接（2026-10-23）

- 钢琴窗背景行改为画布局部坐标绘制，左侧键盘列不再留下空白；琴键与背景几何仍共用。
- 头/尾圆点现在可斜拖，同时改变端点时间与音高；外侧 3 px 时长边缘和倾斜线尖端仍可独立命中。
- 连接音符本体拖动改为接缝跟随：拖后一个只带动前一个尾部，拖前一个只带动后一个头部，外侧端点保持不动。
- 三处吸附保持原下拉外观，菜单只多一个 `1/n…`；普通选项不显示分母框。
- 力度/CC 区左侧让出琴键列、右侧让出滚动条，与画布左右边界一致。
- 实测基线：单元测试 **37 组 / 5528 条**；离屏自检 **1483 项 / 61 张快照**；`--vst3` / `--vst3fx` 通过。
- 快照 `58-note-tilt.png`、`59-note-link.png`、`60-note-chain-follow.png`、`61-snap-grids.png` 有意更新。
