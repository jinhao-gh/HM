# HM 项目开发转接资料

交接对象：DeepSeek Harness  
交接日期：2026-10-28  
项目代号：HM  
当前完成批次：第六十六批  
工作目录：`C:\Users\ASUS\Documents\Codex\2026-09-11\ni`

---

## 0. 可直接使用的开场指令

下面这段可以直接发给接手方：

> 你将接手 Windows 桌面 DAW「HM」的后续开发。先完整阅读本文件，再读 `NiDaw/docs/01-requirements.md`、`02-architecture.md`、`03-pitch-model.md`、`04-ui-spec.md`、`05-build-env.md`、`06-testing.md`、`07-roadmap.md`，以及 `NiDaw/devlog/2026-10-23.md` 的最后两节。不要先改功能：先在当前机器复现 37 组 / 5528 条单元测试和 1483 项 / 61 张快照离屏自检，确认 0 失败后，再为当前代码建立 Git 基线提交。以后每次改动都要写测试、跑全量验证、同步需求 / 架构 / UI / 测试 / 交接文档和 devlog，最后执行发布脚本。不要依据旧文档开头的旧基线判断现状；以本文件的第六十五批基线为准。  
> 工作语言使用中文。遇到含义会改变产品行为的歧义时先问；普通工程实现细节自行决定并继续推进。

---

## 1. 项目是什么

HM 是一个 Windows 原生数字音频工作站，核心差异是“频率是第一公民”：

- 音符直接保存频率，不强迫所有音乐落到十二平均律。
- 参考线、参考组、频率比和八分/任意比率分割共同构成音高编辑系统。
- 同时保留钢琴窗模式，以十二平均律编辑。
- 内置 FM、减法、波表、鼓机、采样器、粒子合成器和混合合成器。
- 支持 VST3 乐器宿主与每轨 VST3 插入效果器链。
- 支持 MIDI 硬件输入、MIDI 映射、虚拟键盘、巴扬模式、琶音器、实时控制器和录制。
- 工程、音频、实时预览、导出共用同一套渲染与调度逻辑。

技术栈：

- C++20
- JUCE 8.0.15，源码已内置于 `NiDaw/external/JUCE`
- CMake + Ninja
- Windows / WASAPI
- Visual Studio 2026 Enterprise，MSVC 14.51

本项目不是纯 Web UI，也不是常规 MVC 应用。大量行为集中在 JUCE 组件和实时音频引擎中；修改前必须同时考虑界面、模型、序列化、实时线程、导出和撤销。

---

## 2. 当前状态

### 2.1 自动化基线

最近一次完整发布验证：

- 单元测试：**38 组 / 6395 条，0 失败**
- 离屏自检：**1506 项 / 64 张快照，0 失败**
- 真实 VST3 乐器测试：通过
- 真实 VST3 效果器测试：通过
- 主程序与独立单元测试目标均完成干净重建
- `NiDaw\Source\` 编译警告：0

最近交付物（2026-10-30，含第六十六至六十九批，已用本机证书签名）：

- `outputs/HM/HM.exe`
  - 大小：9,492,800 bytes
  - SHA-256：`EFDFBF9EDA5D717C285F87D9B0FBE499EA8B080D85F815BDEF23350B2E40547D`
  - 签名：`CN=HM, O=HM, OU=Local build`，Authenticode `Valid`（只对本机当前用户可信）
- `outputs/HM-win64.zip`
  - 大小：6,451,462 bytes
  - SHA-256：`909A0AA12257AFC9E3127A0FFBB1F7B57A3EDD4B43E4133BCF72961F28E2862C`

这些是 2026-10-30 最后一次完整发布（`make_release.bat`）的产物，已签名。

### 2.2 第六十五批刚完成的内容

- 修复钢琴窗左侧背景空白。
- 头/尾白色圆点可同时拖动时间与音高。
- 外侧 3 px 时长边缘仍可独立拖动。
- 倾斜线从伸出音符上下的尖端拖动，不与圆点和时长边缘抢命中。
- 连接音符拖动改为接缝跟随：
  - 拖后一个音符的本体或头部，只带动前一音符尾部。
  - 拖前一个音符的本体或尾部，只带动后一个音符头部。
  - 另一侧端点时间和音高保持不动。
- 吸附下拉框保留旧样式，只新增 `1/n…`；普通选项不显示分母输入框。
- 力度 / CC 区左右边界与钢琴画布对齐。

详细记录：

- `NiDaw/docs/01-requirements.md` 末尾需求 297–301、决策 D250–D254
- `NiDaw/docs/02-architecture.md` 末尾第六十五批
- `NiDaw/docs/04-ui-spec.md` 末尾第六十五批
- `NiDaw/docs/06-testing.md` 末尾第六十五批
- `NiDaw/devlog/2026-10-23.md` 末尾第六十五批

### 2.3 已实现的主要能力

轨道与音乐编辑：

- 音轨、片段、循环片段、重叠片段、粘贴、重复、切割、粘合、重命名、M/S。
- 参考线模式、钢琴窗模式、侧视其他轨道。
- 参考组、参考线组、频率比、叉点、等分、锚点、历史与预设。
- 音符独立头/尾频率、倾斜区间、正负曲率、连接链和单音头播放。
- 音轨区 / 钢琴区 / 力度 CC 区三套独立吸附，支持 `1/n`。
- 力度绝对拖动、CC 曲线绘制与折点简化。

乐器与音频：

- FM 合成器、减法合成器、波表合成器、鼓机、采样器、粒子合成器。
- 混合合成器：3 主振 + Sub + Noise、每声部双滤波、4 包络、6 LFO、16 槽调制矩阵、4 FX 槽。
- 48 个混合合成器工厂预设，8 个二级分类；FM 为 DX7 ROM-1A 的 32 个经典音色（4 类，中文名）。
- 波表编辑器：64 帧、64 次泛音振幅/相位、手绘、谐波、平滑、归一化、导入与波形预设。
- VST3 乐器宿主、每轨 VST3 插入链、插件状态嵌入工程、缺插件诊断、插件扫描子进程。
- 总输出推子、混音台、轨道电平表、导出与分轨导出。

MIDI：

- 硬件 MIDI 输入、音符映射、循环/非循环映射。
- 力度、CC1、CC11、CC64 实时处理与录制。
- 虚拟键盘：普通布局、巴扬模式、旋律排、低音排、电脑键盘图示、折叠置顶。
- 琶音器：同步/自由速率、方向、八度、Gate、Swing、力度模式、Hold/Latch。

---

## 3. 第一件必须做的事：保护当前工作区

### 3.1 Git 状态警告

交接时：

- `.git` 目录存在。
- Git 报告处于工作树中。
- **没有任何历史提交**。
- `NiDaw/` 和 `.gitignore` 全部显示为未跟踪。

因此：

- 不要运行 `git clean -fd`。
- 不要运行 `git reset --hard`。
- 不要假设 `git diff` 能告诉你上一批改了什么。
- 若要建立基线，先验证测试，再一次性提交当前已验证状态。

建议首次接手流程：

```powershell
cd C:\Users\ASUS\Documents\Codex\2026-09-11\ni
git status --short
# 先完成第 4 节的构建与测试
git add .gitignore NiDaw DEEPSEEK_HARNESS_HANDOFF_2026-10-28.md
git commit -m "chore: establish verified HM baseline through batch 65"
```

不要直接把 `work/`、`outputs/` 或构建目录提交进主源码仓库；它们已在 `.gitignore` 中。发布产物可以另行归档。

### 3.2 转移完整工作区时必须带上隐藏目录

当前构建脚本位于 `work/`，而 `.gitignore` 会忽略整个 `work/`。如果只复制 Git 跟踪文件，会缺少构建与测试入口。

推荐转移的最小集合：

- `.gitignore`
- `NiDaw/Source`
- `NiDaw/cmake`
- `NiDaw/TestPlugin`
- `NiDaw/docs`
- `NiDaw/devlog`
- `NiDaw/CMakeLists.txt`
- `NiDaw/.vscode`，可选
- `NiDaw/external/JUCE`，或另行提供 JUCE 8.0.15
- `work/*.bat`
- `work/*.ps1`
- `work/testproj/CMakeLists.txt`
- `outputs/HM` 与 `outputs/HM-win64.zip`，可选，用于对照发布结果

不要复制：

- `work/app-build5`
- `work/test-build22`
- `work/selftest-*`
- 旧构建目录

这些应由脚本重新生成。

---

## 4. 构建与验证

### 4.1 推荐命令

在 `C:\Users\ASUS\Documents\Codex\2026-09-11\ni\work` 下执行：

```powershell
cmd /c build_app.bat NiDaw
cmd /c build_tests22.bat
powershell -NoProfile -ExecutionPolicy Bypass -File run_tests22.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File run_selftest.ps1 -Out C:\Users\ASUS\Documents\Codex\2026-09-11\ni\work\selftest-incoming
```

完整发布：

```powershell
cmd /c make_release.bat
```

发布约需 5–6 分钟，包含：

1. 主程序干净重建
2. 独立单元测试目标干净重建
3. 全量单元测试
4. 带真实测试 VST3 的全部离屏自检
5. 同步 `outputs/HM`、`outputs/HM-win64` 和 zip

### 4.2 产物位置

- 开发主程序：`work/app-build5/NiDaw_artefacts/Release/HM.exe`
- 单元测试：`work/test-build22/NiDawTests_artefacts/Release/NiDawTests.exe`
- 测试乐器：`work/app-build5/NiDawTestInstrument_artefacts/Release/VST3/HMTestInstrument.vst3`
- 测试效果器：`work/app-build5/NiDawTestEffect_artefacts/Release/VST3/HMTestEffect.vst3`
- 发布目录：`outputs/HM`
- 外部分发：`outputs/HM-win64`、`outputs/HM-win64.zip`

CMake 目标名仍叫 `NiDaw`，所以中间目录是 `NiDaw_artefacts`；最终 exe 名来自 `PRODUCT_NAME=HM`。

### 4.3 构建硬规则

- 所有 `.cpp/.h` 是 UTF-8，必须保留 `/utf-8`。
- 中文界面字符串统一使用 `NI_TXT("…")`，不要直接使用 `juce::String("中文")`。
- `NiDaw/Source` 内任何编译器警告都按错误处理。
- 改结构体或头文件后，若出现异常崩溃或行为像旧对象布局，执行 clean 构建。
- 构建脚本会修复 MSVC 中文 `/showIncludes` 前缀；不要绕过脚本随意配置新的增量构建。
- 不要删除或替换 vendored JUCE，除非任务明确要求升级。

环境细节见 `NiDaw/docs/05-build-env.md`。

---

## 5. 目录结构

```text
ni/
  .gitignore
  NiDaw/
    CMakeLists.txt
    cmake/
      SharedSources.cmake
    Source/
      Main.cpp
      Core/
      Audio/
      UI/
      Tests/
      SelfTest/
    TestPlugin/
    docs/
    devlog/
    external/JUCE/
  work/
    build_app.bat
    build_tests22.bat
    run_tests22.ps1
    run_selftest.ps1
    make_release.bat
    testproj/CMakeLists.txt
  outputs/
    HM/
    HM-win64/
    HM-win64.zip
```

源码规模约为 177 个 `.h/.cpp`、3.7 MB，不含 vendored JUCE。

---

## 6. 关键代码入口

### 6.1 应用与 UI

| 范围 | 文件 | 说明 |
| --- | --- | --- |
| 程序入口 | `NiDaw/Source/Main.cpp` | JUCE application、命令行 `--selftest` / `--scan-plugin` |
| 主控制器 | `NiDaw/Source/UI/MainComponent.{h,cpp}` | 主窗口、快捷键、窗口生命周期、MIDI、录制、琶音、文件操作 |
| 编辑区 | `NiDaw/Source/UI/EditorPanel.{h,cpp}` | 编辑栏布局、三列面板、标尺、控制器栏、钢琴画布容器 |
| 钢琴/参考线画布 | `NiDaw/Source/UI/PianoGrid.{h,cpp}` | 音符绘制、命中、拖动、倾斜、连接、吸附、自动滚动 |
| 音符编辑栏 | `NiDaw/Source/UI/NoteEditBar.{h,cpp}` | 头/尾频率、频率比目标、曲率与倾斜 UI |
| 力度/CC | `NiDaw/Source/UI/ControllerLane.{h,cpp}` | 力度柱、CC 折线、区域吸附、简化 |
| 吸附控件 | `NiDaw/Source/UI/SnapControl.{h,cpp}` | 保留旧下拉样式，自定义 `1/n` 分母 |
| 音轨区 | `NiDaw/Source/UI/TrackArea.{h,cpp}` | 轨道、片段、滚动、新建菜单、轨道吸附 |
| 混音台 | `NiDaw/Source/UI/MixerWindow.{h,cpp}` | 每轨条带、总输出、电平表 |
| VST 界面 | `NiDaw/Source/UI/PluginWindow.{h,cpp}` | 插件编辑器窗口 |

### 6.2 MIDI 与虚拟键盘

| 范围 | 文件 | 说明 |
| --- | --- | --- |
| MIDI 设备 | `NiDaw/Source/Audio/MidiInputManager.{h,cpp}` | 设备枚举、打开、原始消息 |
| MIDI 映射逻辑 | `NiDaw/Source/Core/MidiInputMap.{h,cpp}` | 循环/非循环映射，纯逻辑 |
| MIDI 映射窗口 | `NiDaw/Source/UI/MidiInputWindow.{h,cpp}` | 琴键、连线、圆点、预设 |
| 琶音逻辑 | `NiDaw/Source/Core/MidiArp.{h,cpp}` | 速率、方向、Gate、Swing、Hold |
| 琶音控件 | `NiDaw/Source/UI/ArpControls.{h,cpp}` | 两个窗口共用 |
| 虚拟键盘 | `NiDaw/Source/UI/VirtualKeyboardWindow.{h,cpp}` | 88 键、电脑键盘、巴扬、图示、折叠置顶 |
| 键盘布局 | `NiDaw/Source/Core/KeyboardLayouts.{h,cpp}` | 键帽、旋律排、低音排、移位 |

### 6.3 模型与持久化

| 范围 | 文件 | 说明 |
| --- | --- | --- |
| 核心数据 | `NiDaw/Source/Core/Types.h` | Project、Track、Clip、Note、所有乐器参数 |
| 文档与撤销 | `NiDaw/Source/Core/ProjectDocument.{h,cpp}` | 当前工程、applyEdit、undo/redo、变更广播 |
| 工程序列化 | `NiDaw/Source/Core/ProjectDocument.cpp` | `.nprj` 字段读取、默认值和旧工程迁移 |
| 预设库 | `NiDaw/Source/Core/PresetStore.{h,cpp}` | `%APPDATA%\NiDaw\presets.json` |
| 音高数学 | `NiDaw/Source/Core/MusicMath.{h,cpp}` | 轴、频率、snap、连接校验、片段展开 |
| 音高规范 | `NiDaw/Source/Core` + `docs/03-pitch-model.md` | 参考线、比率、倾斜、连接语义 |

### 6.4 音频引擎

| 范围 | 文件 | 说明 |
| --- | --- | --- |
| 调度结构 | `NiDaw/Source/Audio/SynthEngine.h` | `SynthEvent`、`ControllerEvent`、`TrackRenderInfo`、`RenderSchedule` |
| 调度构建 | `NiDaw/Source/Audio/SynthEngine.cpp` | 音符、倾斜 pitch 事件、循环、CC、插件状态 |
| 播放引擎 | `NiDaw/Source/Audio/AudioEngine.{h,cpp}` | 离线/实时渲染、声部、试听、实时事件、电平 |
| 混合合成器 | `NiDaw/Source/Audio/HybridSynth.{h,cpp}` | 三主源、Sub、Noise、双滤波、包络、LFO、宏 |
| 波表 DSP | `NiDaw/Source/Audio/WavetableSynth.{h,cpp}` | 单表、齐奏、离散位置、实时位置发布 |
| 粒子 DSP | `NiDaw/Source/Audio/GranularSynth.{h,cpp}` | 颗粒云、扫描、反向、试听 |
| VST 宿主 | `NiDaw/Source/Audio/PluginHost.{h,cpp}` | 插件生命周期、每音通道、pitch bend、状态 |
| 导出 | `NiDaw/Source/Audio/Exporter.{h,cpp}` | 主混音、分轨、格式与采样率 |

### 6.5 混合合成器 UI

| 范围 | 文件 | 说明 |
| --- | --- | --- |
| 主窗口 | `NiDaw/Source/UI/HybridEditor.{h,cpp}` | 主页面、页面切换、预设 |
| 主操作页 | `NiDaw/Source/UI/HybridMixerPanel.{h,cpp}` | 五源、包络、滤波器、LFO、键盘 |
| 调制页 | `NiDaw/Source/UI/HybridModulationPanel.{h,cpp}` | 宏与 16 槽矩阵 |
| FX 页 | `NiDaw/Source/UI/HybridFxPanel.{h,cpp}` | 四个插入槽 |
| 振荡器窗口 | `NiDaw/Source/UI/HybridOscillatorWindow.{h,cpp}` | 五个音源的可视化与参数 |
| 波表编辑器 | `NiDaw/Source/UI/WavetableEditor.{h,cpp}` | 泛音、手绘、帧、3D |
| 波形预设 | `NiDaw/Source/Core/WaveformPresets.h` | 16 个原创波形预设 |
| 工厂音色 | `NiDaw/Source/Core/HybridPresets.h` | 8 类共 48 个混合音色 |
| DX7 工厂音色 | `NiDaw/Source/Core/Dx7FactoryPresets.h` | ROM-1A 的 32 个音色（4 类，中文名，算法 33–64） |
| DX7 引擎 | `NiDaw/Source/Audio/SynthEngine.cpp` | 64 条算法、dB 曲线、键盘缩放、固定频率、音高 EG、LFO |
| 迷你穆格 | `NiDaw/Source/Audio/MinimoogSynth.{h,cpp}` | 3 振荡器、梯形滤波、双包络、单音/复音、力度 |
| 穆格预设 | `NiDaw/Source/Core/MinimoogPresets.h` | 30 个预设（6 类 × 5） |
| 穆格面板 | `NiDaw/Source/UI/MinimoogEditor.{h,cpp}` | 五段式面板 + HM 扩展条 |

### 6.6 测试

| 范围 | 文件 | 说明 |
| --- | --- | --- |
| 单元测试入口 | `NiDaw/Source/Tests/TestMain.cpp` | 按测试组运行 |
| 测试支持 | `NiDaw/Source/Tests/TestSupport.h` | 断言、临时文件、测试工具 |
| 业务测试 | `NiDaw/Source/Tests/*.cpp` | 37 个测试组按主题分散 |
| 离屏自检 | `NiDaw/Source/SelfTest/SelfTest.cpp` | 单一超大 `run()`，约 1500 项 |
| VST 探针 | `NiDaw/Source/SelfTest/VstPitchProbe.{h,cpp}` | 音高与离线插件验证 |
| 测试插件 | `NiDaw/TestPlugin/*` | 真实 VST3 乐器/效果器 |

---

## 7. 核心数据流

### 7.1 编辑到声音

```text
UI 组件
  -> ProjectDocument::applyEdit
  -> Project / Track / Clip / Note / ControllerCurve
  -> AudioEngine::buildRenderSchedule
  -> RenderSchedule / TrackRenderInfo / SynthEvent / ControllerEvent
  -> PlaybackEngine
  -> 内置乐器或 VST3
  -> 轨道插入链
  -> 推子 / 声相 / 总输出
  -> 音频设备或 Exporter
```

关键原则：

- 编辑先落到模型，再由统一调度进入声音。
- 监听和导出不能各写一套音符解释逻辑。
- 一个用户手势通常对应一条撤销记录。
- UI 中显示的实时位置，应优先来自引擎发布值，而不是界面自行模拟。

### 7.2 音符音高

`Note` 的关键字段：

- `frequencyHz`：头部频率，也是旧代码兼容入口。
- `tailFrequencyHz`：尾部频率；0 表示等于头部。
- `pitchCurve`：-1…+1。
- `tiltStart` / `tiltEnd`：0…1。
- `tieFromNoteId`：连接到前一音符。

必须保持：

- 倾斜音高在对数频率空间插值。
- 倾斜区外为平线。
- 连接只允许头接前尾。
- 一条合法连接链只发一个 note-on / note-off。
- VST 允许显示倾斜和连接，但当前只按首段频率发声。
- 所有音高解释以 `docs/03-pitch-model.md` 和 `Core/MusicMath` 为准。

### 7.3 撤销与持久化

- 用户编辑走 `ProjectDocument::applyEdit`。
- 连续拖动在拖动中只预览，松手时一次提交。
- 工程文件 `.nprj` 采用“缺字段 = 默认值”的向后兼容方式。
- 新增字段必须同时处理：
  - 默认值
  - 序列化读取
  - 序列化写入
  - 旧工程迁移
  - 撤销 / 复制 / 粘贴 / 拆分 / 循环
  - 测试往返

---

## 8. 不能随意改的产品决策

以下规则已经被多轮实现和测试锁定：

1. **不把所有音乐强制到十二平均律**。频率是主数据；钢琴窗只是另一种编辑模式。
2. **参考线与参考组是核心能力**，不是普通辅助网格。
3. **监听与导出必须共用同一套调度和 DSP**。
4. **MIDI 实时音符在走带停止时也要发声**。
5. **琶音只处理实时输入；录制的是琶音后的音符；回放不再过琶音**。
6. **虚拟键盘与硬件 MIDI 走同一套映射 / 钢琴窗音高逻辑**。
7. **硬件、鼠标虚拟键盘、电脑键虚拟键盘的同一音高可以各有独立声部**。
8. **连接音符链只发一个音头**。
9. **三处吸附相互独立**：音轨、钢琴编辑、力度/CC。
10. **吸附菜单保持旧下拉样式，只在菜单里增加 `1/n…`**。
11. **音符外侧时长边缘、内部端点圆点、倾斜线尖端是三种不同命中手势**。
12. **钢琴键、背景行、音符必须共用同一半音行几何，不能各自推算**。
13. **混合合成器位置旋钮支持平滑 / 离散；右键旋钮可分配调制**。
14. **调制名称必须全部走 `NI_TXT`，不得出现 U+FFFD 乱码**。
15. **64 帧 / 64 次泛音的上限已经确定**。
16. **内置波表只读；编辑内置波表时复制成工程自定义表**。
17. **VST3 插件状态属于工程；缺插件必须明确报错，不能静默导出无声文件**。
18. **插件扫描必须保持子进程隔离，坏插件不能拖垮主程序**。
19. **所有窗口必须经 `WindowPlacement` 放到屏幕工作区内**。
20. **空格键默认控制走带；文本框和插件自身界面例外**。

完整历史决策见 `NiDaw/docs/01-requirements.md`。修改任何决策前先找到原决策行和对应测试。

---

## 9. 已知限制与未完成项

自动化测试全绿不代表功能已经完成。当前明确未完成：

- ASIO 后端。
- OGG / MP3 导出。
- MIDI 文件导入 / 导出。
- 参数自动化。
- HM 自身作为独立 VST3 插件导出。
- 音频设备设置随工程保存。
- 可选导出范围。
- 总输出母带效果器链。
- 插件延迟补偿。
- 音轨区音频波形缩略图。
- 参考线状态拖动微调与多选批量操作。
- 预设整包导入 / 导出。
- 重叠片段的分层 comping UI。
- 硬件 MIDI 弯音轮与触后。
- 粒子合成器的 LFO、可选颗粒窗、取样区间、时间拉伸同步。
- 多点音符音高包络、CC 折点框选、连接链力度渐变。

代码结构债务：

- `Source/SelfTest/SelfTest.cpp` 仍是单个超大 `run()`，后续应按主题拆成多个文件，并引入共享 `Context`。
- `Source/UI/PianoGrid.cpp` 仍较大，可按绘制、命中/拖动、序列化辅助拆成多个实现文件。
- `NiDaw/docs/08-handoff.md` 前半部分保留了历史描述；遇到前后不一致时，以该文件末尾最新批次和本文件为准。

---

## 10. 以后每项功能的完成标准

接手方不应把“代码能编译”当成完成。一个功能至少需要：

1. **需求与决策**
   - 在 `01-requirements.md` 增加编号需求和决策。
2. **模型兼容**
   - 新字段有默认值、旧工程迁移和序列化往返。
3. **实现**
   - UI、模型、引擎、撤销、导出链路一致。
4. **单元测试**
   - 纯逻辑、数据模型、DSP 或序列化必须有直接测试。
5. **离屏自检**
   - 有真实组件尺寸、交互、像素或快照断言。
6. **文档**
   - 同步 `02-architecture.md`、`04-ui-spec.md`、`06-testing.md`。
7. **Devlog**
   - 记录完成项、关键 bug 根因、验证基线与未完成事项。
8. **发布**
   - clean 构建两个目标。
   - 单元测试 0 失败。
   - `--selftest --vst3 … --vst3fx …` 0 失败。
   - 执行 `work\make_release.bat`。
9. **基线更新**
   - 更新测试组数、断言数、自检项数和快照数。

如果新增了 UI 快照，确认快照是有意变化；不要为通过测试而盲目接受错误布局。

---

## 11. 常见故障排查顺序

### 程序打不开或构建后随机崩溃

1. 关闭正在运行的 HM。
2. 执行 `work\build_app.bat NiDaw clean`。
3. 再执行 `work\build_tests22.bat clean`。
4. 不要先怀疑业务逻辑，先排除旧对象布局。

### 中文乱码

1. 检查是否用了 `NI_TXT`。
2. 检查源文件是否 UTF-8。
3. 检查构建是否保留 `/utf-8`。
4. 检查 PowerShell 输出是否按 UTF-8 读取。

### UI 改了但运行时仍旧

1. 检查是否运行 `work/app-build5/.../HM.exe`，而不是 `outputs/HM/HM.exe`。
2. 重新构建后再启动。
3. 只有发布脚本执行后，`outputs` 才会更新。

### 测试通过但声音不对

先检查：

- `buildRenderSchedule`
- `TrackRenderInfo`
- `PlaybackEngine`
- 该乐器的 `noteOn / noteOff / renderAdding`
- 导出是否也走了同一个 `PlaybackEngine`

### VST 插件问题

- 查看 `%APPDATA%\NiDaw\plugin-events.log`
- 检查插件扫描是否在子进程执行
- 检查插件状态是否在导出前 `captureStates`
- 缺插件时应报错，不应生成静音文件

### 保存后丢失状态

检查四层：

1. 模型是否写入
2. `applyEdit` 是否记录
3. `.nprj` 是否写入字段
4. 读取时是否有默认值与迁移

---

## 12. 建议接手后的前三个任务

### 任务 A：建立可信基线

先不开发新功能，执行完整验证并建立 Git 基线提交。确认：

- 37 / 5528
- 1483 / 61
- VST3 两段通过

### 任务 B：做一次手工用户路径验收

自动化不覆盖真人操作的所有细节。至少手工确认：

1. 新建混合合成器并演奏。
2. 打开钢琴窗，拖动音符、端点和连接音符。
3. 切换三处吸附。
4. 绘制力度和 CC。
5. 打开虚拟键盘，验证电脑键、折叠、置顶、巴扬模式。
6. 保存、重开工程并导出 WAV。
7. 加载一个 VST3 乐器和一个 VST3 效果器。

### 任务 C：从待办池选一个小范围、可验证的改进

推荐优先顺序：

1. 修复用户实测发现的具体 UI / 音频问题。
2. 补足现有功能的自动化缺口。
3. 再做 ASIO、MIDI 文件、自动化等大型模块。

不要同时开始多个底层重构。这个项目的风险主要集中在“数据模型 + 撤销 + 实时音频 + UI 布局”交界处。

---

## 13. 交接接收确认

接手方阅读后，第一条回复应至少包含：

1. 已确认的工作目录与系统环境。
2. 已复现的单元测试 / 自检基线。
3. 当前 Git 状态及是否已建立基线提交。
4. 接下来准备处理的第一个问题。
5. 若环境缺少 Visual Studio、Windows SDK、CMake 或测试插件，明确列出缺失项。

在基线尚未复现前，不应直接修改功能代码。

---

## 14. 接手记录（2026-10-28，第六十六批）

DSH 接手后按第 4 节复现了基线：37 组 / 5528 条、1483 项 / 61 张快照、VST3 两段全部通过，
并建立了基线提交 `fde4cf5`。

第六十六批修的是虚拟键盘：电脑键盘不再要求焦点在键盘窗口里。HM 在前台时，焦点在音色编辑器、
混音台或效果器窗口都能继续弹（可以边拧旋钮边试听）；只有整个程序切到后台、或焦点进入文本框时
才停止并放掉按住的音。`MainComponent::keyStateChanged` 会把任意窗口的按键状态转发给面板。
键盘在屏幕上时，当前布局映射为音符的键不再触发单键快捷键（`n` 封参考线、巴扬的 Backspace），
窗口关掉立刻恢复。

新基线：单元测试 37 组 / 5528 条；离屏自检 1488 项 / 61 张快照；VST3 / VST3FX 通过。

本机签名（2026-10-29）：双击 exe 报「无法验证发布者」，用 `work\signing\` 里的自签名证书解决，
`make_release.bat` 的 `[1b]` 构建后自动签名并校验 `Valid`。这只对本机当前用户有效；发给别人
仍需正式 CA 证书（OV/EV 或 Azure Trusted Signing），换证书只需替换
`work\signing\thumbprint.txt` 里的指纹。同批把发布脚本的自检目录改成整目录清空（残留的
`selftest-presets.json` 会让波表自检多数一张表），并修复了批处理被写成 LF 换行的问题。

环境注意：受限容器里子进程只能写工作区，测试写 `%TEMP%` 会被拒绝（EACCES）。跑测试或自检前
要把 `TEMP/TMP` 指到工作区内，否则预设相关用例会崩溃——那是环境限制，不是代码缺陷
（见 `NiDaw/devlog/2026-10-28.md`）。

## 15. 第六十七批接手记录（2026-10-29）

- FM 工厂库换成 DX7 ROM-1A 的 32 个经典音色（中文名、4 类 × 8），由公开的 `dx7-patches`
  集合转换成 `Core/Dx7FactoryPresets.h`；算法按最近拓扑 + 操作器置换映射（`engineAlgorithmFor`）。
- 预设栏在有两类以上时改为二级菜单（点开先见大类，悬停/点击展开子类）；单类别编辑器保持原下拉。
- 混合合成器工厂库重做为 48 个（8 类 × 6），第二振荡器、调制矩阵、双滤波与 FX 链覆盖更多。
- 新基线：单元测试 37 组 / 5954 条；离屏自检 1488 项 / 61 张快照；VST3 / VST3FX 通过；
  发布产物已签名（`outputs\HM\HM.exe` SHA-256 `097D3A05…97DC`）。
- Serum 预设：本机 Serum 2 的 `.SerumPreset` 是 `XferJson` 容器（元数据 JSON + zstd 参数树），
  导入需要 zstd 依赖与参数树逆向，且不能内嵌 Xfer 的工厂内容；已记入 devlog，未在本批实施。

## 16. 第六十八批接手记录（2026-10-29）

- 引擎 64 条算法：1–32 保留 HM 路由（旧工程不变），33–64 是 DX7 原厂路由；反馈运算器写在
  `FmAlgorithm::feedbackOp`（原厂 2/8/10/12/15/18/20/21/27 不在 OP6）。
- `FmPatch::classicEngine` 开关：新工程/新预设为 true，走 DX7 语义（0.75 dB/步电平与包络、
  1 ms–20 s 速率、力度曲线、键盘电平缩放、固定频率、音高 EG、6 种 LFO 波形、按键同步与
  音高调制灵敏度）；旧工程没有该字段，读成 false，声音与第六十七批一致。
- DX7 工厂库重新生成：算法 `32 + 原厂编号`，固定频率/键盘缩放/AM/音高 EG/LFO 全部保留，
  删掉了第六十七批的 `engineAlgorithmFor` 近似表。
- 界面：算法库分 `HM 1-32` / `DX7 33-64` 两页；运算器详情新增“细节”页（模式、固定频率、AM、
  断点、左右曲线/深度、音高 EG）；LFO 页补正弦、按键同步、音高灵敏度。
- 新基线：单元测试 37 组 / 6125 条；离屏自检 1498 项 / 63 张快照；VST3 / VST3FX 通过；
  发布产物已签名（`outputs\HM\HM.exe` SHA-256 `27059EA6…77EA`）。
- 已知取舍：曲线常量是 DX7 参数语义级近似，不是 LSI 仿真；原厂 4/6 的跨链反馈按读反馈运算器
  自反馈近似；不模拟 12-bit DAC。用 E.PIANO 1 / BRASS 1 / TUB BELLS 试听校准。
- 发布脚本：自检改走 `run_selftest.ps1`（私有 TEMP），修掉共享临时目录让波表检查多数一张表的问题。

## 17. 第六十九批接手记录（2026-10-30）

- 新乐器 `TrackKind::minimoog`（值 9）：`Core/Types.h` 的 `MinimoogParams`、`Audio/MinimoogSynth.{h,cpp}`、
  `Core/MinimoogPresets.h`、`UI/MinimoogEditor.{h,cpp}`；`AudioEngine`、`SynthWindow`、`TrackArea`、
  `PresetStore`、`ProjectDocument`、`CMakeLists.txt`/`cmake/SharedSources.cmake` 均已接通。
- 声音：3 振荡器（6 波形 × 6 音域、osc2/3 ±7 半音）、白/粉噪声、过载混音、自写四极梯形滤波、
  滤波/响度指数包络、振荡器 3/噪声调制混合、单音滑音；8' 时 A4 = 440 逐档位单测。
- 演奏：单音后按优先（默认）；复音数 2/4/8/16/32/不限（64）；力度 0–10 可调，0 时忽略力度。
  复音数与力度是演奏设置，工厂预设不覆盖。
- 界面：1120×680 五段式面板 + HM 扩展条（复音数、力度、预设栏）+ C2–C6 键盘；30 个预设、6 类；
  新建轨道默认「经典主音」。
- 新基线：单元测试 38 组 / 6395 条；离屏自检 1506 项 / 64 张快照；VST3 / VST3FX 通过；
  发布产物已签名（见 §2.1 的哈希）。
- 已知取舍：包络“维持”为固定 0.8 平台；复音模式无滑音；PITCH 轮只影响试听；外部输入/A-440/
  耳机为禁用装饰；未做原机逐点 A/B。
