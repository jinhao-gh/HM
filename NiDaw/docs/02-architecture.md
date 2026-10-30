# 技术架构

## 分层

```
Source/
  Core/     纯数据与数学，不依赖界面，可单独单元测试
  Audio/    合成器内核、播放引擎、离线导出
  UI/       JUCE 界面组件
  Tests/    单元测试（juce::UnitTest）
  Main.cpp  应用入口 + 离屏自检
```

依赖方向严格单向：`UI -> Audio -> Core`。**Core 不得包含任何界面头文件**，
这样合成器内核将来可以脱离界面单独打包成插件。

## Core 层

### Types.h —— 全部数据结构

| 类型 | 说明 |
| --- | --- |
| `TrackKind` | `synth / sampler / audio / subtractive / wavetable / drums / vst`：轨道类型（序列化值 0–6，只增不改）。前六种是内置乐器，`vst` 是外部 VST3 插件 |
| `VstParams` | 外部乐器：`name / pluginPath / pluginUid / stateBase64`，外加运行时字段 `loadedOk`（不序列化）。插件状态是 base64 的二进制块，内嵌在 `.nprj` 里 |
| `FmOperator` | 一个 FM 运算器：`ratio / detune / outputLevel / velSens / rateScale / rate[4] / level[4]` |
| `FmPatch` | 六个运算器 + `algorithm(1..32) / feedback(0..7) / LFO(速率,延迟,波形,音高深度,幅度深度) / transpose` |
| `FmAlgorithm` | 一个算法的路由：`modulators[6]`（谁调制运算器 i）、`carriers`（谁进输出）、`diagram`（编辑器用的连线示意） |
| `SynthParams` | `patch`（FmPatch）+ `gain` + 低通 `filterCutoffHz / filterResonance`。带 `operator==` 供播放引擎判断是否需要重设参数 |
| `SamplerParams` | 采样器：`filePath / fileName / rootHz / looped + loopStart + loopEnd / ADSR / 一个 LFO（速率,音高音分,幅度）/ gain / 低通` |
| `Note` | `id / startTick / durationTicks / frequencyHz / velocity / muted / comment`，另提供 `endTick()`。**`startTick` 是相对片段内容原点的 tick**，不是时间轴绝对位置；**没有比率字段**（比率是视图状态，见下面 `AppState`） |
| `Clip` | `id / name / startTick / lengthTicks / loopLengthTicks / looped / notes[]`，加上音频片段的 `audioFilePath / audioFileName / audioOffsetSeconds / audioGain`。窗口长度与循环周期分开；音符可以落在窗口之外（保留但不发声）；`isAudio()` 表示这是一个音频片段 |
| `TempoMarker` | `tick / numerator / denominator / bpm`，横向生效到下一个标记之前 |
| `InstrumentTrack` | `id / kind / name / volumeDb / pan / colourIndex / synth / clips[]`。乐器与音轨是同一个对象，音符装在片段里 |
| `ReferenceLineKind` | `fixedHz / noteRatio / group / multiNote`：参考线状态的四种类型 |
| `ReferenceLineEntry` | 参考控制栏里的一条「参考线状态」：`id / kind / name / colourIndex / locked / visible`，外加各类型自己的内容（`baseHz`；`anchorHz + anchorPosition + dots`；`frozenHz + frozenRatio`；`frozenNotes[]`） |
| `EditorViewState` | 编辑栏视图与编辑状态（缩放、滚动、取景模式、吸附、指示线、**两种画布模式** `editorMode` / `showReferenceLinesInPianoMode`），随工程保存；缺字段按旧默认值读 |
| `music::piano` | 12 平均律（A4 = 440 Hz）的几个纯函数：音号↔频率、量化到最近半音、黑白键与音名。钢琴窗模式与插件宿主的 MIDI 映射都按同一套 12 平均律换算 |
| `PluginGrouping` | 插件选择器的分组：厂家（`manufacturerName`，空 → 未知厂家）、类型（VST3 子类别 `category`，去掉 `Fx`/`Instrument`/`Tools` 后翻中文，未知原样）、以及两条兜底组的排序规则。纯逻辑，两个目标都编译 |
| `EffectChain` | 插入链的模型与操作：`EffectSlot`（插件路径 / 唯一 id / 状态 / 旁路）、增删移、上限 16、JSON 往返。纯逻辑，两个目标都编译 |
| `Project` | `version / sampleRate / bufferSize / tempoMarkers[] / tracks[] / view / referenceGroup / referenceLines[] / nextTrackId / nextNoteId / nextClipId / nextReferenceLineId` |
| `MidiInputState` | `Project::midiInput`：`map`（`midimap::Map`，循环格 / 逐音号两张表，见下）+ 窗口正在看的 `groupSource` / `group` 快照 + `presetName`。**它进工程**（换歌就该换映射），设备选择反而存在 `%APPDATA%\NiDaw\midi.json` |
| `midimap::Map` | `Core/MidiInputMap.{h,cpp}`：`mode`（循环 / 非循环）、`loopKeys`（1–24）、`anchorNote`、`tile`（循环格）、`free`（逐音号）。**一个键存一组频率**（`vector<vector<double>>` / `map<int, vector<double>>`，空 = 不发声），因为一个钢琴键可以连好几个圆点（第四十四批）：`addFrequency` 加一条、`removeFrequency` 只去掉一条、`clearFrequency` 去掉整个键、`setFrequency` 是"替换成这一条"。绑定存**频率数值**（锚点那个八度的），取频率 = `格里的值 × 2^floor((n − 锚点) ÷ 格长)`：**整格跟着键盘升/降八度**，空格步在任何八度都不发声。`frequencyFor` 返回最低的那条（读数 / 标题用），`frequenciesFor` 返回全部（引擎与录制用）。其它纯函数：`bindingCount / boundKeyCount / cableCount / tileOffsetOf / visibleNotes / setLoopKeys / isSame / noteName` |
| `CustomWavetable` | `Project::customWavetables`：本工程自己的波表（`id / name / frameCount(1–8) / samples`，16 位 PCM，帧长固定 2048）。播放用的 mipmap 是**派生数据**（`wavetable::buildTableFromFrames`），按 `id + 数据哈希` 缓存在 `Core/WavetableTables.cpp` 里，改了才重建。振荡器用 `WavetableOsc::customTableId` 引用（0 = 内置 `tableIndex`） |

常量：`kTicksPerQuarter = 960`、`kMaxTracks = 64`、`kVoicesPerTrack = 16`、
可视频率范围 `kMinVisibleHz = 8`、`kMaxVisibleHz = 20000`、
新建片段默认长度 `kDefaultClipTicks`（4 小节）。

**空工程是合法状态**：`makeDefaultProject()` 只带一个 120 bpm 4:4 的速度标记、**没有轨道**；
`addTrack (project, kind)` 是新建轨道的唯一入口（＋ 菜单与测试都走它），它建一条带单个空片段、
颜色按轨道序号取的轨道，并在达到 `kMaxTracks` 时返回 0。

### MusicMath —— 领域数学

见 [03-pitch-model.md](03-pitch-model.md)。要点：
`VerticalAxis`（频率与屏幕 y 的互换）、`ReferenceLine` 与参考线生成、`findNearestLineIndex`、
拖动相关的 `resizeNoteStart / resizeNoteEnd / clampTickDeltaForGroup / scaledFrequency`、
比率文本解析、`TempoMap`（tick 与秒互换、小节:拍、吸附）。

片段相关的纯函数也在这里：`clipRepeatCount` / `expandClip`（把片段按循环平铺并把窗口外的
部分裁掉，产出带绝对 tick 的 `ClipNotePlacement`）、`findFreeSlot`（贴片段时找第一个放得下的空位）、
`repeatClip`（向后依次追加副本，每份落在第一个空位，绝不裁剪已有片段）。

**片段之间可以重叠**（第四十一批起）：`trimOverlapsInTrack` 与它在新建 / 拖动片段时的调用
已经删掉，重叠的地方两段一起发声；可见性与命中是「后建的画在上面、点最上面的那条」
（`TrackArea::clipAt` 反着找）。粘贴仍然先过 `findFreeSlot`，所以粘出来的片段不会藏在别人下面。

参考线状态的纯函数：`makeFixedHzEntry` / `makeGroupEntry` / `makeNoteRatioEntry (id, note, ratio)` /
`makeMultiNoteEntry`（构造四类条目，比率由调用方传入，因为它是视图状态）、
`clearUnlockedEntries`（新建参考线时清掉临时条目）、`moveEntry`（上下调序）、
`resolveReferenceEntries (entries, selectedNotes, currentRatio, soloEntryId)`
（未锁定的二 / 四类按**当前选中音符 + 当前比率**实时解算，顺带滤掉隐藏与非独显的条目）、
`buildReferenceLinesForEntries`（把条目展开成实际画线，跨条目按频率去重，先到先得）。

**所有几何与时间换算都必须走这里的纯函数**，不要在界面代码里另写一套，
否则单元测试覆盖不到，且容易与渲染不一致。

### ProjectDocument —— 文档与撤销

- 持有 `Project`、当前文件路径、修改标记。
- **快照式撤销**：每次编辑前把整个 `Project` 拷进撤销栈（上限 128 步），不做反向操作。
  对当前规模（音符量级）足够，且不可能出现「撤销后状态不一致」。
- 三种编辑入口：
  - `applyEdit(name, mutation)` —— 常规编辑：压快照、修改、广播。
  - `beginEdit(name)` —— 连续交互（滑条拖动、拖动音符）开始前压一次快照。
  - `notifyEdit()` —— 连续的中间过程只广播、不压快照，于是整段交互只占一条撤销记录。
- 通过 `juce::ChangeBroadcaster` 通知界面。
- 视图状态（缩放、滚动等）也保存在 `Project` 里，但**不进撤销栈**，避免滚动一次就产生撤销步。
- 参考组与参考线组的编辑同样走 `applyEdit` / `beginEdit`，因此可撤销。

### 序列化

`projectToJson` / `projectFromJson`，纯文本 JSON，扩展名 `.nprj`。
字段缺失一律取默认值，因此**新增字段不会破坏旧工程**（例如滤波器字段，旧文件加载后截止 = 20 kHz，音色不变）。
加载后会修正 `nextTrackId / nextNoteId`，保证不会与已存在的 id 冲突。

音轨字段从 `notes[]` 变成 `clips[]`（片段含 `loopLengthTicks` 与 `looped`），并新增 `nextClipId`。
音轨还带一个 `kind`（整数），旧工程缺这个字段一律按 `synth` 载入。
`synth` 从「波形 + ADSR + 调制比/深度」变成 `operators[]`（六个运算器各带 `ratio / detune /
outputLevel / velSens / rateScale / rate[4] / level[4] / enabled`）加上 `algorithm / feedback / lfo* / transpose`；
`enabled` 缺字段按「开启」读，所以旧工程的音色完全不变。
**音色 JSON 的读写函数只有一份**，放在 `Core/PresetStore.cpp`（`synthParamsToVar/FromVar`、
`samplerParamsToVar/FromVar`），工程文件与预设文件共用；字段布局保持原样，
「有 `operators` 就是 FM patch」这条判断因此不会失效。
**没有 `operators` 字段的旧工程按两运算器 FM 迁移**：算法 1、运算器 1 为载波、运算器 2 为调制器，
ADSR 秒数经 `fm::rateForSeconds` 折成 RATE，旧 `fmDepth`（周期数）折成调制器电平。
**旧工程迁移**：加载时若某音轨没有 `clips` 字段（或为空）却带旧版 `notes`，
就把该轨全部音符包成一个片段（起点 = 第一个音符起点，长度 = 到最后一个音符结束），
于是旧工程能原样打开、原样发声。

参考线状态存在 `referenceLines`（有序），但**只写 `locked == true` 的条目**：未锁定条目是本次
会话的临时状态，本来就会被下一条新参考线清掉。读取时若没有 `referenceLines`，就把旧的
`referenceLineGroups` 与 `view.manualInputHz` 迁移成**已锁定**的条目（它们以前是会保存的），
旧字段 `referenceLock / lockedBaseHz / lockedRatio` 读到即忽略。

## Audio 层

### SynthEngine —— 合成器内核

```
每个声部：六个运算器（相位累加 → 正弦 → RATE/LEVEL 包络 → 电平/力度）
          → 按算法做相位调制 → 载波求和 → 低通 SVF → 声相/音量 → 累加
```

- **六个运算器**：每个都是「按 RATE/LEVEL 包络加权的正弦」，频率 = 音符频率 × 该运算器的 `ratio`
  × 失谐 × LFO 音高调制。`ratio` 可自由取值（不限于十二平均律音级）。
- **32 个算法**决定谁调制谁、谁进输出。约定「运算器编号越大越靠上游，且每个调制器编号都大于它调制的目标」，
  因此引擎固定从运算器 6 迭代到 1 就能一次算完，不需要按算法排序。
- **相位调制**：`phase = basePhase + Σ(调制器输出) × 2.0`，即输出电平 99 的调制器让目标相位偏移两个周期。
  调制指数大时「音高」由频谱而非零交叉决定，这是 FM 的正常行为。
- **RATE/LEVEL 包络**：R1 从静音升到 L1，R2 到 L2，R3 到 L3 后保持，音符结束时 R4 降到 L4。
  速率曲线是 `0.0015 × 5000^(1−rate/99)` 秒（RATE 0 约 7.5 秒，RATE 99 约 1.5 毫秒），
  映射函数放在 `Core/FmMath.h`，引擎与工程迁移共用。
- **力度**：整体输出乘力度，同时每个运算器按 `velSens` 再降低自己的电平——所以力度既改响度也改音色。
- **键盘速率缩放**：`rateScale` 让高音的包络更快，这是 FM 键盘乐器音色统一的关键。
- **运算器 6 可自反馈**（0..7 档）：上限约 0.35 个周期的相位自调制，保证高反馈下仍然稳定。
- **LFO**：三角 / 上升锯齿 / 下降锯齿 / 方波 / 随机保持，速率约 0.07–21 Hz，可延迟淡入；
  音高深度最高约 ±100 音分，幅度深度是颤音。
- 每声部一个 `juce::dsp::StateVariableTPTFilter<float>`（低通，单声道处理后再做声相）。
  滤波系数**每个音频块更新一次**（约 10 ms 粒度），不逐采样重算，避免 16 声部 × 48 kHz 的系数开销。
- 复音 16；声部抢占顺序：空闲 → 释放中最早 → 最旧。
- 声部音量含固定 `kMasterTrim = 0.22` 的余量，保证 16 声部齐奏不削顶。
- 声相用等功率定律：`left = cos(θ)`、`right = sin(θ)`，其中 `θ = (pan+1) × π/4`。
- **每轨的合成器放在堆上**（`PlaybackEngine` 里是 `std::vector<TrackSynth>`，`prepare()` 时 resize 到 64）：
  64 轨 × 16 声部 × 6 运算器太大，放在组件里会把栈顶穿。

`buildSchedule(project)` 把工程编译成**不可变快照** `RenderSchedule`：
每个音轨一份 `TrackRenderInfo`（合成器参数、音量、声相）与按时间排序的 `SynthEvent`（note-on / note-off）。
同一时刻的 note-off 排在同音高的 note-on 之前，避免重复音粘连。

**静音音符被整条跳过**：既不出事件，也不参与 `lengthSeconds` 的计算，所以实时播放与离线导出都不会听到它，
导出的长度收在最后一个可听音符上（`projectEndTick` 仍算上静音音符，时间轴视图因此不会跳）。
note-off 事件会带上对应 note-on 的频率，让事件对自解释。

调度按**片段展开**：逐轨逐片段调用 `expandClip`，循环片段按周期平铺内容、超出 `lengthTicks` 的部分裁掉，
再把相对 tick 换算成绝对 tick（`clip.startTick + repeat × loopLengthTicks + note.startTick`）。
同一音符在不同重复里使用不同的 note id，避免互相抢占。落在窗口外的音符保留在文档里但不产生事件。

**这个快照是音频线程唯一读取的数据**，界面怎么改都不会让音频线程看到半成品状态。

### SamplePlayer —— 采样器内核

同一个模块里还有**音频轨的混音**：`AudioPlacement { startSeconds, lengthSeconds,
sourceOffsetSeconds, gain, sample }` 描述时间轴上的一段音频，
`renderAudioClips(...)` 按块把落在块里的片段线性插值写进输出。
它是**无状态**的——给块起始时间、块长度、采样率就能算出结果，所以跳转、实时播放与离线导出
走的是同一段代码，不需要为导出准备第二条路径。`buildSchedule` 对 `kind == audio` 的轨道
把每个音频片段折成一条 `AudioPlacement`（文件用同一个缓存解码），并让 `lengthSeconds` 把它们算进去。

```
每个声部：按速率推进读指针（线性插值） → ADSR → 一个低通 SVF → 声相/音量 → 累加
```

- **音高映射**：`播放速率 = 音符频率 ÷ 基准音高 ×（源采样率 ÷ 设备采样率）`。
  基准音高是「这份录音本身是什么音高」，所以 220 Hz 的录音在 A4 音符上会快一倍（听上去 440 Hz）。
- `Core/SamplerParams` 里的 ADSR / 低通 / 增益与 FM 合成器的声部结构平行；LFO 可以同时颤音与颤幅度。
- **循环**：`looped` 打开时，读指针越过 `loopEnd` 会绕回 `loopStart`；关掉就一直播到样本结束。
- **样本缓存**：`Audio/SamplePlayer` 里的 `getCachedSample (path)` 按路径共享一份解码结果
  （`std::map` + `mutex`），因为调度在每次工程改动时都会重建，重复读盘会卡。
  `buildSchedule` 只对 `kind == sampler` 的轨道取样本，并把 `shared_ptr` 放进 `TrackRenderInfo`，
  音频线程因此只拿到一个不可变的只读缓冲。
- 采样器与合成器**共用同一套片段 / 音符 / 走带**，渲染时按 `TrackRenderInfo::kind` 分流：
  `PlaybackEngine` 里 `synths` 与 `samplePlayers` 是两个平行的堆上数组（各 64 个）。
- 音色窗口的试听也按乐器类型走：`triggerPreviewNote`（FM）或 `triggerPreviewSampleNote`（采样器）。

### GranularSynth —— 粒子合成器内核（第五十二批）

同一个"从文件里读声音"的家庭里的第二件乐器：采样器逐声部播放一个读指针，粒子合成器**每声部
撒一把短颗粒**（默认 20 颗/秒、每颗 120 ms）。音源用的是**同一个 `LoadedSample` 与同一份
解码缓存**，所以 `buildSchedule` 对 `kind == sampler` 与 `kind == granular` 走同一段取样本的
代码，`TrackRenderInfo::sample` 两种轨都装。

```
每个声部：按密度撒颗粒（间隔 × (1 ± 间距抖动)）
每颗颗粒：自己的起点（扫描位置 ± 位置抖动） → 自己的速率（音符频率 ÷ 根音，× ±音高抖动，
         反向概率命中就倒着读）→ 长度 = 粒度 → Hann 窗 → 线性插值读文件 → 累加
每声部：累加 → 一或两级 SVF（左右各一套，保住颗粒自己的声相）→ 音量包络 → 声相/音量
整条云：输出再乘 1/√(密度 × 粒度)（重叠补偿，见 01-requirements 的 D213）
```

- **精确频率**：`颗粒速率 = 音符频率 ÷ rootHz ×（源采样率 ÷ 设备采样率）`，和采样器一字不差，
  所以参考线系统里那些"不是十二平均律"的频率照样成立。
- **扫描**：`位置 + 扫描速度 × 已按下时长 + 位置包络 × 包络量`，`fold01` 折返；颗粒的起点在
  这之上再加自己的位置抖动，最后钳进 0…1。
- **上限**：16 声部（沿用 `kVoicesPerTrack`），每声部最多 32 颗颗粒，满了丢这个声部里最老的一颗。
- **给编辑器看**：控制率把最多 64 颗颗粒的（位置、年龄）发布到一组 `std::atomic<float>`
  （`GranularSynth::Published`，和 `WavetableSynth::Published` 同样的做法），`AudioEngine`
  用 `getGranularGrainCount / getGranularGrainPosition / getGranularGrainAge` 转发给界面；
  音频线程只写、消息线程只读，不加锁。没有音源时 `isActive()` 保持 false、渲染全零。
- **引擎接线**：`PlaybackEngine::granularSynths` 与 `synths` / `samplePlayers` 平行（各 64 个），
  `triggerEvent` / `applyLiveEvent` / `renderVoices` / `allNotesOff` / `releaseAllVoices` /
  `getActiveVoiceCount` 各加一条 `TrackKind::granular` 分支；试听走
  `triggerPreviewGranularNote` / `updatePreviewGranularParams`（与采样器同形）。

### PlaybackEngine —— 播放引擎

- 位置以秒计，tick 与秒的换算通过 `TempoMap`。
- 按块渲染，块内按采样精度切分：每段先处理事件，再渲染到下一个事件时刻。
- **线程模型（重要）**：
  - 消息线程只能写原子量：`pendingSeekSeconds`、`pendingReleaseVoices`、预览门控与序号。
  - `setSchedule` 用 `SpinLock` 保护 `shared_ptr` 交换。
  - 跳转时音频线程先释放所有正在发声的声部，再重新定位游标，
    否则「note-off 已经落在新位置之前」的音符会永远挂住。
  - 停止时不立刻掐断，而是走释放包络；停止状态下仍然渲染声部，让尾音自然收尾。
- **三种「停下来」的语义（第四十三批）**：`停止`、`跳转`、`循环回卷` 都先释放正在发声的声部，
  但对插入链的处理**故意不同** —— `停止` 走 `setStopRewindSeconds()`（= 置 `pendingStopRewind`
  之后照旧定位），音频线程把音符放掉、把控制器游标倒回起点，但**跳过 `effect->reset()`**，
  所以混响 / 延迟的尾巴继续响完；`跳转`（拖播放头 / `Home` / 打开工程）**重置**每条插入链，
  免得延迟线把跳转之前的声音再放一遍；`循环回卷` 本来就不重置，尾巴连着下一遍响。
  释放外部乐器时也分两种：`releaseHeldNotes()`（每个按着的音符一条真 note-off，尾巴计时
  保留，插件继续渲染到释音结束）用于停止 / 跳转 / 循环；`allNotesOff()`（note-off + CC123 +
  `channels.reset()`）只留给换工程 / 卸载插件 / 槽位换乐器这种确实要立刻安静的场合。
- **重新开始 = 掐断尾巴**：`AudioEngine::play()` 在**从停着开始**时（`! isPlaying()`）调
  `PlaybackEngine::cutSoundingTails()`，音频线程在块首 `releaseSustained()` +
  `releaseAllVoices(true)`（硬放；外部插件因此收到 `allNotesOff`）+ 冲掉每条插入链，
  并把 `chainSilenceBlocks` 记满（链已经空了，不必再数那 8 块）。于是新的一遍从安静开始，
  不会叠在上一次的混响 / 延迟尾巴上。**正在播时再点播放不算重开**，什么也不掐。
- **预览声部**：给音色窗口的小键盘试听用。参数用带锁快照传递，门控与重触发用原子量；
  停止播放时依然渲染。离线导出使用另一个 `PlaybackEngine` 实例，因此**试听永远不会混进导出的 WAV**。
- **实时音符通道**（第四十一批）：MIDI 键盘（与鼠标写音符时的那一下试听）走
  `noteOnTrack / noteOffTrack`，消息线程把事件压进一个定长 `juce::AbstractFifo`，
  音频线程在**每个块的开头**取出来施加到对应轨道的乐器上 —— 内置五种乐器走各自的
  `noteOn / noteOff`，VST 轨走 `HostedPlugin::noteOn`（它本来就每音一个 MIDI 通道，
  所以和弦天然可用）。这条通道**与走带无关**（停着也出声）、**与预览声部互不影响**，
  而且**不被导出**（导出用的是另一个 `PlaybackEngine`）。它的事件同样经过该轨的音量 /
  声相 / 效果器链，所以「弹下去听到的」与「录下来播放的」是同一个声音。
- **实时控制器走同一个入口**（第四十七批）：同一个 FIFO 里还走 `CC64 / CC11 / CC1`
  （`PlaybackEngine::sendControllerToTrack`，`LiveEvent` 带上了类型）。处理规则与
  **画出来的曲线**共用一处：有外部插件的轨只调 `HostedInstrument::sendController`，
  内置轨交给 `applyControllerToSlot`——`processControllers`（曲线）与 `applyLiveEvent`
  （手弹）调的是同一个函数，所以表情是乘在这条轨输出上的增益、踏板是把 note-off 挂住。
  **踏板对实时音符也生效**：`applyLiveEvent` 里的 note-off 同样过那套判定，进
  `sustainedNotes`，踏板抬起时由 `applyControllerToSlot` 补发（外部插件不在此列，
  踏板是它自己的事）。消息线程那一侧把这些变化**录进片段的控制器曲线**（一个控制器
  一次手势一条撤销），所以「弹进去的踏板」与「画出来的踏板」是同一份数据。
- **自定义波表怎么到音频线程**（第四十二批）：`buildSchedule`（消息线程）把每个振荡器的
  `customTableId` 解析成一张 `shared_ptr<const wavetable::Wavetable>` 放进
  `TrackRenderInfo::wavetables`——**和音频片段的 `sample` 是同一条路**：不可变快照交给
  音频线程，指针活到那份调度被换掉为止。mipmap 按 id + 数据哈希缓存，所以「画一笔 →
  重建调度」只会重建真正改过的那张表。`matchTracks` 在指针变化时调
  `WavetableSynth::setTables`；试听那条走 `AudioEngine::updatePreviewWaveTables`。
- **波表的实时位置怎么到界面**（第四十六批）：与上面那条**方向相反**的是读数——
  `WavetableSynth` 在控制率分支算出每个声部的 `v.position[o]` 之后，把它与「这条轨有没有声部」
  写进一组原子量（`Published{ position[2], voices }`），界面按 **30 Hz** 去读它画瀑布视图上的
  光标（`AudioEngine::getWavetablePosition / getWavetableActiveVoices`，轨道声部优先；
  没有轨道声部时读预览声部的那一对），**与混音台电平表是同一套做法**：音频线程只写数字、
  不加锁，界面只读、不等待、读到的值最多差一块（≈10 ms）。两点容易踩：`Published` 必须
  **可拷贝 / 可移动**（乐器住在 `std::vector` 里，直接放 `std::array<std::atomic<float>, 2>`
  编译不过），`voices` 必须在 `renderAdding` 的**块首与块尾各写一次**（声部可能在块内就结束，
  之后调用方不再调它，漏掉块尾那次界面会一直以为"在响"）。界面只在有声部时开 30 Hz 定时器，
  静下来就停表。
- **琶音器是演奏者，不是轨道效果**（第四十七批）：`Core/MidiArp.{h,cpp}` 是纯逻辑
  （步长、方向与八度展开、Gate、Swing、三种力度模式、Hold 的 latch），`MainComponent`
  里那个 1 ms 的 `ArpTicker` 只做一件事：按当前时钟算出该弹的步，然后用
  **现有的实时音符通道**把它弹出来。于是琶音不碰渲染路径：它**只作用于实时输入**，
  生成的音照旧过该轨的音量与效果器、照旧能被录制、照旧被踩着的踏板挂住。
  时钟有两套：走带跑着时用工程位置（`TempoMap` 的 tick，所以跟着小节与速度变化——
  「锁在格子上」），走带停着时用它自己的 tick 计数（从按下的第一个键起算），
  所以不按播放也能试听。录下来的音符用琶音**自己的**起点与长度（格 tick 与 Gate），
  不是 note-on 落到音频块上的时刻。

### AudioEngine —— 设备与走带

`juce::AudioDeviceManager` 加 `AudioIODeviceCallback`，默认 48 kHz / 512 帧，
共享或独占 WASAPI，设备变更时重新 `prepare`。对外提供播放、停止、回到开头、定位。

### Exporter

与实时播放**共用同一个 `PlaybackEngine`**，因此导出结果与听到的一致。
范围从 0 到最后一个音符结束加最长释音时间，输出立体声、工程采样率、可选的 16/24-bit PCM。

### PluginHost —— 外部 VST3 乐器

`PluginHostManager` 管两件事：**找插件**（`VST3PluginFormat` + `KnownPluginList`，按需在后台扫描、
结果缓存到 `%APPDATA%\NiDaw\pluginlist.xml`）和**养实例**（每条 VST 轨一个
`HostedPlugin`，在消息线程创建、`prepareToPlay` 之后挂进调度）。

`HostedPlugin` 把工程里的音符翻成 MIDI：音号 `round(69 + 12·log2(f/440))` + 剩余音分，
音分落在**该音符独占的 MIDI 通道**的 pitch bend 上（`vst::ChannelAllocator`，16 个通道，
实例就绪时给每个通道发一次 RPN 0 声明 ±2 半音的弯音范围）。渲染循环按事件边界切块，
所以每个 note-on / note-off 落在正确的采样位置。

**通道怎么取用（第四十五批）**：一个音一个通道，而**通道的取用次序**决定了和弦会不会互相拉
音高——插件在 note-off 之后还会在那个通道上把音尾放完，谁接手这个通道，谁就把自己的 pitch bend
盖到那条音尾上。所以 `allocate()` 是：**先用从没用过的**（最小号优先），十六个都用过之后才用
**释放最久**的那个；`release()` 会记下释放时刻，刚放开的通道不会立刻被下一个音拿走。四个音的
柱式和弦连弹四遍，正好用满十六个通道，而不是每遍都压在 0–3 号上把上一遍的音尾拉走。十六个
通道全忙时，取**占用最久**的那个并**报出它的音 id**，`HostedPlugin::noteOn` 先给那个音发一条
真正的 note-off（它照常释音、自然收尾），再做 RPN → 补发 CC → bend → note-on；被抢的音自己
迟到的 note-off 找不到通道，因此不会误杀新音。

**一个音一个通道的代价：通道级状态要跟着通道走。** 踏板 / 表情这类 CC 是「这个通道的状态」，
而每个音各自在一个通道上，所以 `HostedPlugin` 记住每个已转发控制器的最后一个值，一个通道
**刚拿到音**时先补发到该通道，然后才 note-on——否则「先踩踏板、再起一个音」的那个音永远收不到
踏板（`reset()` / `prepare()` 清空这份记忆；`allNotesOff()` 不清，踏板状态是插件自己的）。

**音符编号有两套，互不相干。** 调度器给每轨的事件编号 1、2、3…（`buildSchedule` 自己编的，
与工程里的音符 id 无关），实时音符（MIDI 键盘 / 屏幕琴键 / 写音符试听）则在消息线程就被翻译进
一个私有的负数空间（`kLiveNoteIdBase`，见 `AudioEngine.h`）。两套编号一旦相同，乐器会认为这是
「同一个音」，让它们共用一个通道（一条弯音），实时音的 note-off 还会把工程音符的通道提前收掉。

**插件整机弯音时的退路：按半音演奏。** 上面那套「一个音一个通道 + 各自一条弯音」只在插件把
弯音当成**每个通道各自**的时候才成立。实测（`HM.exe --vstprobe <plugin.vst3>`，见
`Source/SelfTest/VstPitchProbe.cpp`）Serum 2 是把弯音当成**整个音色**的：一条弯音把所有正在
响的音一起拉走，而它自己的 16 个 `Pitch Bend Chan N` 参数设满也改不了这件事；Pianoteq 9 则是
按通道的、也接受 HM 声明的 ±2 半音范围。对这种插件，MIDI 层面没有"同时响、各自不同音分"的
做法，所以每条 VST 轨可以打开 `VstParams::equalTemperament`（行头右键）：引擎在把频率交给
乐器之前先过一遍 `vst::snapToSemitone`（调度音符在 `triggerEvent`、实时音符在 `applyLiveEvent`，
两者同一条路），于是那一轨的音全落在最近的半音上、**不需要任何逐音弯音**（发出去的是正中间
的弯音值，等于不弯）。代价是丢掉 1:3 这类精确频率，换来和弦不再互相拉音高。

释放也分两种（第四十三批）：`releaseHeldNotes()` 给每个还按着的音符发一条**真正的 note-off**
（不发 CC123、不 `channels.reset()`）并**保留尾巴计时**，于是插件的释音与它自己的混响照常衰减，
`isActive()` 一直为真到尾巴放完；`allNotesOff()` 是硬停（note-off + CC123 + reset），只在换工程 /
卸载 / 槽位换乐器时用。

`PlaybackEngine` 通过 `HostedInstrument` 这套纯虚接口看待宿主乐器，和内置乐器完全同一条渲染路径；
实时实例由 `LiveInstrumentProvider` 提供，导出则由 `HostedInstrumentFactory` 为每个文件新建
**私有离线实例**（导出与监听不会互相抢一个实例，stem 与总混音也因此一致）。
接口定义在 `Core/PluginRendering.h`（不带任何插件依赖），所以单元测试可以不链接 JUCE 的宿主代码。

一条实现约束：实例**只在消息线程创建与销毁**，也**只在 HM 退出时释放**——
音频线程可能正拿着某个实例的引用渲染，让消息线程始终持有最后一份引用是最省事的保证
（代价是被删掉的轨道其插件仍在内存里，退出才回收）。

### 插入链（效果器）

每条轨道有一串 `EffectSlot`（`InstrumentTrack::effects`），信号是
**乐器 / 音频片段 → 效果器（按顺序）→ 音量与声像 → 总输出**。

`PlaybackEngine` 为此多了一块**每轨复用的 `trackBuffer`**：有链的轨道以 unity 增益渲染进它，
逐个调用链上的 `HostedEffect::process`（就地处理），再按音量/声像并入 `output`；
**没有链的轨道仍走原来那条直接并入的路径**，逐样本结果不变。试听声部与节拍器不经过任何链。
**链跟着尾巴走**（第四十三批）：乐器最后一个声部的释音结束、这条轨「不活跃」之后，只要它
**有插入效果器**，引擎仍会 `trackBuffer.clear()` → `runEffectChain` → `mixTrackIntoOutput`
（所以电平表会继续跟着落），直到连着 `kChainSilenceBlocksBeforeStop = 8` 块（512 帧 @48k
≈ 85 ms）的峰值都低于 `kChainSilenceThreshold = 1e-5`（≈ −100 dBFS）才回到「跳过这条轨」；
没有插入效果器的轨道照旧立刻跳过（零开销）。换轨 / 换链时这个计数归零。

跳转（seek）时链上的实例会被 `reset()`，免得跳到新位置还在响跳之前的声音；**循环回卷与
「停止」的那次回卷都不重置**（尾巴自然延续，见上面的三种语义）。

效果器实例是 `HostedEffectPlugin`（`HostedEffect` 接口）：**立体声进 / 立体声出**，
按 JUCE 的**就地**约定处理——缓冲宽度是 `max(进, 出)`，前 `进` 个通道是输入，插件把输出写回
前 `出` 个通道；旁路是一个原子开关（旁路时不调用插件）；`getLatencyInSamples()` 上报给
效果器行显示（**不做补偿**，见 D132）。

宿主两侧的接口与乐器对称：实时实例经 `LiveInstrumentProvider::effectFor`、
导出经 `HostedEffectFactory` 为每个文件新建私有效果实例（`Exporter` → `buildSchedule`）。
实例的缓存按「轨道 + 槽位」记，槽位顺序变了会重新加载（顺序变了，链的语义也变了）。

## UI 层

组件树（自上而下）：

```
MainComponent
  TopToolbar          新建 / 打开 / 保存 / 另存为 / 导出
  TrackArea           Cubase 风格排列区。顶栏左侧是「＋ 新建轨道」（弹出类型菜单），
                      下面是节拍标记轨与时间轴标尺（与编辑栏共用视图），再下面每条音轨一行
    LaneHeader        每条音轨左侧的行头 —— **乐器唯一的显示处**：名称 / 音量 / 声相 /
                      音色按钮 / 类型文字与片段数 / 删除
                      片段可创建 / 拖动 / 拉伸 / 循环 / 跨轨移动 / 右键删除，点击即展开编辑栏
                      （第三十六批起**音轨栏右侧那一列已经取消**：片段名 / 循环 / 重复在片段的
                       右键菜单里，效果器插入链在行头「效果」按钮打开的窗口里，音轨栏占满整宽）
  EditorPanel         主窗口下方展开区（Lower Zone）
    [头部]  锁定音符 / 吸附 / 纵向取景 / 定位选中音符 / 关闭编辑栏
    NoteEditBar       选中音符时出现：频率 / 力度 / 备注 / 静音开关 + 六个频率比按键（多选时
                      频率比那一行隐藏、下面改为逐音符列表，每行还有自己的静音开关）
    TempoMarkerTrack  节拍标记轨
    TimelineRuler     时间轴标尺（小节号与播放位置，点击定位）
    PianoGrid         编辑画布（对数频率轴 / 节拍网格 / 参考线 / 音符）。显示的是当前打开的片段，
                      窗口 `[startTick, startTick + lengthTicks)` 正常绘制、窗口外变暗且不发声
                      （`view.editorMode == piano` 时改成半音行 + 左侧 44 px 键盘条，
                      写入与纵向拖动量化到半音，参考线降级为背景）
    OptionsPanel      右侧：参考线控制栏。顶部「＋」+ 参考线状态列表（每条 3 行：
                      类型 / 名称 / 五个按钮），底部操作提示
    ReferenceGroupPanel 左侧：历史 / 预设按钮、八度线段、色板、比率按键、选择基准音 / 保存预设 / 应用
      OctaveLadder    线段画布（圆点 / 叉点 / 多八度重复 / 缩放滚动）
    PresetPanel       参考组栏右侧的侧栏（历史 或 预设），可拖拽调整宽度
  TransportBar        播放 / 停止 / 回到开头 / 循环 / 节拍器 / **录制·停止录制 + 片段内·片段外**
                      （MIDI 录制）/ 位置读数 / 速度 / 音频设置
  SynthWindow         音色窗口（非模态，按需创建，全局复用）
    SynthEditor       三层 FM 编辑器：算法图库（32 张缩略图谱）→ 运算器总览（大图谱，
                      方块 + 调制线 + 输出条，每块右上角一个开关）→ 运算器详情
                      （包络曲线 + 比率 / 失谐 / 电平 / 力度敏感 / 速率缩放 + R1–R4、L1–L4，
                      OP6 另有反馈）；另有 LFO 与输出两个侧页 + C4–C5 试听键盘
      AlgorithmDiagram 画一张算法图谱（缩略 / 大图两种尺寸，可点方块、可点小开关）
      EnvelopeCurve  画一个运算器的 RATE/LEVEL 包络曲线（只读）
    SamplerEditor     采样器编辑器：导入音频 + 波形（含循环标记）+ 基准音高 / 循环 /
                      包络 / 滤波 / 调制 / 增益 + C4–C5 试听键盘
                      （`SynthWindow` 同时持有两个编辑器，按轨道类型显示其中一个）
    WavetableEditor   波表编辑器：**3D 帧瀑布视图**（8 帧沿深度堆叠 + 当前波亮线 + 跟随
                      引擎上报位置的实时光标，168 px；视图上可直接拖动 / 点帧 / 滚轮 / 双击改
                      `position`）+ 振荡器 1/2 切换 + 四列参数（振荡器 / 包络 / 滤波 / 调制）。
                      打开它时窗口按页取高度（波表页 **640 px**，其余乐器页 560 px）
      WavetableWaterfallView  继承 `juce::Component`，自己画瀑布与光标、自己收四种鼠标手势；
                      位置的**读数来自引擎**（`AudioEngine::getWavetablePosition` 等），
                      30 Hz 定时器只在有声部时跑
  WavetableTableWindow 自定义波表的编辑窗口（第四十二批）：名字 / 帧数 / 导入 WAV… /
                      从片段取样… / 拷内置… / 试听 C4 / 删除这张表 + 可画的大画布 +
                      帧缩略图 + 谐波条（非模态、设计尺寸面板 + 滚动视口）
  PresetBar           两个编辑器共用的预设行：单类别库用下拉（内置 / 我的），多类别库用二级菜单
                      （大类 → 悬停展开音色）+ `<` `>` + 保存 / 重命名 / 删除
  PluginPicker        选插件用的对话框（搜索框 + 列表 + 重新扫描 / 添加目录 / 确定 / 取消）
  PluginWindow        插件自己的界面（非模态、可缩放、全局复用；没有界面时用通用参数面板）
  EffectsWindow       一条轨道的效果器插入链（行头「效果」按钮打开，关闭只隐藏）
  MixerWindow         混音台（顶栏按钮 / F3，每轨一根条带 + 最右钉住的总输出条）
    Strip             一根条带 = 一个**真实组件**（自己画、自己收鼠标）：乐器入口 / 插入链 /
                      声相 / 推子（带 dB 刻度）/ M / S / **左右两条**峰值电平表（带刻度）。
                      它读 AudioEngine 的每轨 / 总输出读数，衰减与峰值保持在 UI 侧，
                      见 Core/MeterMath.h
  MidiInputWindow     MIDI 输入（顶栏「MIDI输入」）：左边竖琴键、中间连线、右边所选参考线组的
                      只读圆点。拖键到圆点即**加**一条连线（一个键可以连好几个圆点），
                      右键琴键断全部 / 右键连线断一条 / 右键圆点断连到它的所有线；`应用` 在
                      循环模式下学习锚点。右列有**自己的缩放与滑动**（`groupZoom` / `groupScroll`，
                      `yForHz` 在全览映射上再套一层：滚轮滑动、Ctrl+滚轮以指针为锚点缩放、
                      双击回全览，见 `clampGroupView`）。弹下的键经
                      `MainComponent::handleMidiMessage` → 实时音符通道出声（一个键一条连线一个
                      声部）；「录制」把这些键按播放头落成音符（一个频率一个音符）
    ArpControls       琶音器的那一排控件（速率 / 自由 Hz / 方向 / 八度 / Gate / Swing /
                      力度 / 固定力度 / Hold + 开关）。**这个窗口与虚拟键盘窗口共用同一个
                      组件**，读写的都是 `Project::midiInput.arp`，所以两处的读数永远一致，
                      改动也不进撤销栈（配置）
  VirtualKeyboardWindow 虚拟键盘（顶栏「虚拟键盘」/ Alt+K，第四十七批；第四十八批加了折叠 / 置顶
                      / 巴扬 / 图示）：一条一次看全的 88 键 `LetteredKeyboard`
                      （`MidiKeyboardComponent` 的薄子类，多画一层"这个琴键对应哪些电脑键"）、
                      力度控件、布局下拉、两排移位按钮、`ComputerKeyboardView` 图示、底部一条
                      `ArpControls`。鼠标点键与电脑键盘都经
                      `MainComponent::handleLiveNoteOn/Off`——**与硬件 MIDI 键盘完全同一条路**
                      （映射 / 十二平均律、琶音、录制、锚点学习、高亮）。硬件弹的音由
                      `setKeysDown` 推进键盘状态里点亮；`applyingHeldKeys` 那道守卫保证
                      「点亮」不会被当成新按下的键
    Core/KeyboardLayouts  键位与图示的**唯一一张表**：`physicalBoard()`（44 个键帽、真实错位、
                      `⌫` 两格宽）、`capsFor(layout)`（常规 / 巴扬的旋律与低音映射）、
                      `baseNoteFor` / `bassNoteFor`（低音是"上五度 / 下四度"严格交替、
                      锚在 `5` = F3，整排 C#3–F#4）、外加**触发源编号**
                      （`kHardwareTrigger` / `kMouseTrigger` / `typingTriggerFor`）、
                      `noteForCap` / `keyLettersFor`（一个琴键可以有多个电脑键）。
                      界面把它同时喂给 `setKeyPressForNote`、图示上色与钢琴上的字母
```

`MainComponent` 同时是**所有窗口的空格键监听器**（`juce::KeyListener` 挂到每个顶层窗口上，
见 `attachKeyListenersToWindows`）：空格 = 播放 / 停止；焦点在文本框或插件自己的界面里时不抢
（规则写在 `isTransportSpaceKeyFor`，两个 `keyPressed` 重载都问它）。

**电脑键盘的弹奏与释放纪律**（第四十八批引入、第四十九批改成"面板自己管"）：面板**不用**
`MidiKeyboardComponent` 的键位表（构造时 `clearKeyMappings()`），而是在自己的
`keyStateChanged` 里按当前布局**逐个轮询** `KeyPress::isKeyCurrentlyDown`，与自己的
`typingDown` 集合做差分，变了就经 `keyboardState.noteOn/noteOff` 驱动（钢琴照样亮、监听器照样
转发给 `MainComponent`）。这样做有两个好处：**焦点在窗口里任何控件上都行**（事件沿父链上浮，
不必先点钢琴），而且"哪些键按着"这份状态在**我们自己**手里。
兜底是一条 **100 ms 心跳**（只在可见且有键按着时跑）：① 复查 `typingDown`，已经不按了的补一条
note-off（**漏掉的 key-up 自愈**——这正是"松开了还一直响"的根因，JUCE 那份按音号记的账一旦
漏事件就永远清不掉）；② 窗口（含子件）失去键盘焦点就整批放掉。
结构性变化（两排移位、切布局、折叠、隐藏 / 关闭窗口、析构）仍旧先 `releaseWindowKeys()`——
它逐个按 **(触发源, 音)** 经 `keyboardState.noteOff` 放掉这个窗口发起过的音（监听器 →
`MainComponent::handleLiveNoteOff` → 引擎真的收音），**硬件正弹的音不在其中**。
`MainComponent` 那一侧：**虚拟键盘可见时 `1–6` 不选频率比**（D204），以及**每条来源各自持有
声部**（D208：源 = 硬件 `0` / 鼠标 `1` / 电脑键 `100+键码`，同一个音被两条来源按着就响两个声部、
各松各停）。

电平上报的路径：`PlaybackEngine::mixTrackIntoOutput` 在把轨道推子后的信号并入输出时量这一块的峰值，
**左右声道各量一次**（声像把一边推到底时另一边就是 0），`renderNextBlock` 结尾同样按声道量总输出；
两者都只写 `std::atomic`，读一次即清（`MeterReading{peakLeft, peakRight, clipLeft, clipRight}`）。
**每条轨道（含没有插入链的）都先渲染进 `trackBuffer` 再过链并入输出**——这是混音台能对每一条轨道
都给出读数的前提。

**画出来的控制器曲线**（力度 / CC）走的是另一条路，但同样只有一处入口：

- 数据住在**片段**里：`Clip::controllerCurves`（每个控制器一条 `ControllerCurve`，折点是
  clip 相对的 tick + 0..127 的值），`Core/ControllerMath.h` 是唯一编辑它们的地方——
  取值（点之间线性插值）、插入 / 删除 / 钳制、以及把曲线**按片段窗口裁切、按循环周期平铺**、
  在折点之间以 1/16 拍插出中间值的 `expandControllers`。单元测试直接测这些纯函数。
- `buildSchedule` 把曲线换成 `TrackRenderInfo::controllers`（绝对秒 + CC 号 + 值），
  和音符事件同一个来源、同一套时间轴，所以播放与导出自动一致。
- `PlaybackEngine::processControllers` 在每个块的开头走这些事件（排在音符之前，所以正好压在
  音符结尾上的踏板事件仍然生效）：有外部插件的轨道**只转发** `HostedInstrument::sendController`
  （插件自己实现表情与踏板）；内置乐器没有 MIDI，于是引擎自己处理——CC11 折成该槽的
  `expression` 乘数（在 `mixTrackIntoOutput` 里和推子一起乘下去，所以混音台的表跟着动），
  CC64 踩下时把 note-off 存进该槽的 `sustainedNotes`、抬起时补发（跳转 / 停止 / 循环回卷
  都会先放掉它们）。**两条路只走一条**，任何一侧都不会被应用两次。
- 力度不住在曲线里：它是 `Note::velocity`，控制器栏只是又一个能改它的地方（拖动 = 一次
  `调整力度`），所以引擎侧一行都不用动。

音轨栏与编辑栏**各自有一套** `pixelsPerQuarter` / `horizontalOffsetTicks`：编辑栏用
`EditorViewState` 里原有的那两个字段，音轨栏用同一结构里的 `arrangement*` 一对
（旧工程打开时用编辑栏的值播种，见 D137）。取值统一走 `pixelsPerQuarterFor /
horizontalOffsetFor / xForTimelineTick / timelineTickForX`（`Core/Types.h`，
参数是 `TimelineView::arrangement / editor`）。因此**每一栏内部**（`TempoMarkerTrack`、
`TimelineRuler`、`PianoGrid` 或音轨片段）刻度逐像素对齐，而**两栏之间**互不影响：
在编辑栏放大细看某个片段时，音轨栏可以停在全局概览上。
`ClipInspector` 由 `MainComponent::resized()` 从音轨栏那一行的右端切出来，音轨栏相应变窄，
因此栏内对齐这条约束不受影响。

**源文件清单只有一份**：两个目标共用的 `.cpp`（Core 的领域逻辑 + 测试直接驱动的音频引擎部分）
写在 `cmake/SharedSources.cmake` 的 `NIDAW_SHARED_SOURCES` 里，`NiDaw/CMakeLists.txt`
（主目标与同文件里的测试目标）与 `work/testproj/CMakeLists.txt` 都 `include()` 它。
**加一个共用源文件只改这一处**；只有主程序独有的文件（`Main.cpp`、`Source/UI/*`、
`PluginHost.cpp`、`Tests.cpp` 等）才写在各目标的清单里。

两个画布共用的两段小逻辑也各自只有一份：拖到边缘的自动滚动判据在
`Source/UI/AutoScroll.h`（`autoscroll::stepFor`），波形的「片段时间 → 文件时间 → 峰值桶」
在 `Source/Audio/SamplePlayer.h`（`waveformBucketFor`）。两栏**各自的**配色、指针钉位、
吸附与重放逻辑保持独立（已知分歧见每一批的 devlog）。
主区只有「音轨栏（上）/ 编辑栏（下）」两行：`MainComponent::resized()` 不再从左边切走任何列，
所以**编辑栏从 x = 0 开始**（自检里有断言）。

### 状态归属

| 状态 | 存放位置 | 随工程保存 | 可撤销 |
| --- | --- | --- | --- |
| 工程数据（音轨、片段、音符、速度标记、合成器参数） | `ProjectDocument` | 是 | 是 |
| 参考线状态列表（四类条目、顺序、锁定、显示、颜色） | `Project.referenceLines` | 只有锁定条目 | 是 |
| 当前参考组（线段上的圆点）与已启用的参考线组 | `Project` | 是 | 是 |
| 参考组预设、参考线组预设、**FM 音色预设、采样器音色预设** | 应用配置目录 `presets.json`（`referenceGroups` / `referenceLineGroups` / `fmPatches` / `samplerPatches` 四段） | 独立于工程 | 否 |
| 参考组交互状态（拾取模式、已武装的圆点、基准音关联） | `AppState` | 否 | 否 |
| 本次会话的参考线组历史（上限 50） | `AppState` | 否 | 否 |
| 编辑栏视图（缩放、滚动、取景、吸附、参考线锁、手动输入、当前比率） | `Project.view` | 是 | 否 |
| 选中状态（选中的音轨、**当前打开的片段**、选中的音符集合、锁定音符） | `AppState` | 否 | 否 |
| **当前频率比**（看第二类参考线的镜头，选择一变就回 1:2） | `AppState` | 否 | 否 |
| 面板里选中的参考线状态、独显的那一条 | `AppState` | 否 | 否 |
| 剪贴板（音符与片段各一份） | `AppState` | 否 | 否 |
| 播放位置与播放状态 | `PlaybackEngine` | 否 | 否 |

### 界面刷新链路

```
用户操作 → ProjectDocument::applyEdit → sendChangeMessage()
        → 各组件 changeListenerCallback → 局部 refresh / repaint
        → MainComponent 标记 scheduleDirty
        → 30 Hz 定时器：重建音频快照、刷新走带读数、刷新播放头
```

界面的 `refresh()` 必须对**反馈回路**免疫：控件值回填时用 `dontSendNotification`，
并用 `updating` 守卫标志防止「回填 → 触发回调 → 再次回填」。

## 关键约束

1. **音频线程不分配内存、不加锁（除两处极短的 SpinLock）、不碰 `Project`**。
2. **所有时间换算走 `TempoMap`**，不要在别处用 bpm 直接算秒。
3. **Core 层不依赖界面**。
4. 新增字段一律给默认值，保证旧工程可加载。
5. **参考组的换算一律走 `MusicMath` 的纯函数**（`ratioToPosition`、`dividePositions`、
   `referenceFrequencies`），界面里不重新实现一遍。
6. 颜色在 Core 层一律用**调色板索引**，不用 `juce::Colour`。
7. **片段相关的换算一律走 `MusicMath` 的纯函数**（`expandClip` / `findFreeSlot` / `repeatClip`）；
   音符 tick 是**相对片段**的，绝对 / 相对的加减只在编辑栏读写时进行，别处不要再算一遍。
8. **重复片段用 `MusicMath::repeatClip`**，命令级的入口（`Ctrl+D`、片段竖栏按钮）都经
   `MainComponent::repeatSelectedClip`，一次命令只压一条快照。
9. **画哪些参考线只有一个入口**：`PianoGrid::currentReferenceLines()` 调
   `resolveReferenceEntries` + `buildReferenceLinesForEntries`。绘制、命中测试和拖动吸附
   都用它，所以「隐藏 / 独显」的条目不会出现在任何一处。
10. **派生条目（二、四类）用 `ProjectDocument::applyDerivedEdit`**：改状态但不进撤销栈、
    不标脏。对状态的手动操作才走 `applyEdit`。

## 混合合成器的每声部引擎（第五十九批）

`HybridSynth` 不再是“整条轨一组滤波”。它持有 16 个 `HybridVoice`：

```
noteOn → HybridVoice（频率、力度、4 ADSR、4 LFO、双滤波、调制缓存）
       → 三个主源 + Sub + Noise 按音符单独渲染
       → Filter 1 / Filter 2（串联 / 并联 / 仅 F1）
       → 音量包络 + 声相
       → 四槽 HybridFxChain
       → 轨道插入效果器
```

- 波表 / 采样 / 粒子 / 减法引擎新增 `renderVoiceAdding(noteId, …)`：只渲染指定 note id 的内部声部。
  这样混合器不必为每个音符复制 16 套完整引擎。
- 每个声部有自己的滤波状态、包络值与 LFO 相位；两个音同时按下时，一个音的释放不会改变另一个音的滤波。
- 第 17 个音偷 `startOrder` 最小的声部，并先给旧音发 note-off。
- 陷波是每声部每通道的两级 FIR 零点；梳状是每声部每通道的短延迟反馈（延迟 = 采样率 / 截止频率）。
- FX 链在 `HybridFxChain` 中；槽参数由 `HybridParams::fxSlots` 提供，调制矩阵的 FX 目标在每块叠加。
- 工程 / 预设的 `hybrid` 段包含 `fxSlots`、`macroNames`；旧工程缺字段时取默认值。

工程文件夹保存由 `ProjectDocument::saveToProjectFolder` 完成：创建 `project.nprj`、`samples/`、`exports/`，
把音频轨、单采样、粒子音源复制进 `samples/` 并把保存副本的路径指向副本；界面层的保存命令走这条路径。

## 第六十批：齐奏、六 LFO 与时值同步

- `WavetableOsc` 的齐奏在 `WavetableSynth::Voice` 内部展开：每个主振最多 16 个相位，按对称失谐分布频率，
  输出按 `1/sqrt(n)` 归一化；齐奏为 1 时路径与旧实现一致。
- `WavetableSynth::sampleTable` 的 `discrete` 参数把帧位置量化为最近帧；缩略图、振荡器窗口与引擎共用它。
- `HybridSynth::setTempo` 由 `PlaybackEngine` 每块传入当前 BPM；包络的 A/D/R 与 LFO 的拍分数都按当前速度换算。
- 每条包络有 MS/BPM 模式与 A/D/R 拍分数；每条 LFO 有 MS/BPM 模式、`rateMs` 或拍分数。
- `HybridSynth::Published` 用无锁原子量发布 4 个包络的位置 / 值和 6 个 LFO 的相位 / 值；编辑器 30 Hz 读取。
- `HybridModSource` 保持旧数值并在末尾追加 LFO 5/6；`HybridModDestination` 在末尾追加每个主振的
  Position / Unison / Detune。名称由 `hybridModSourceName` / `hybridModDestinationName` 集中提供。
- `WavetableSynth::setVoiceOffsets` 把位置 / 齐奏 / 失谐调制写入对应内部声部；齐奏数量夹在 1–16。
- 合唱与相位器都不再依赖 JUCE 的单调制立体声 processor：合唱用左右独立延迟线，相位器用每声道四阶全通。
  比特压碎的 `crushCounter` 保存在槽状态里，跨块连续。

## 第六十一批：缩略图会话态与 LFO 深度

- `HybridLfoParams` 增加 `depth` 与 `fadeMs`；引擎把 LFO 输出乘以 `depth` 和按声部年龄计算的 fade。
- 缩略图的 2D/3D 状态保存在 `HybridOscillatorThumbnail` 实例中；`setSource` 不重置它。编辑器 30 Hz 定时器只在有活跃声部时重绘缩略图，高亮切片读取引擎发布的位置。
- 3D 缩略图使用固定等距投影：近切片在右下、远切片在左上；每片只绘制一个波形周期。
- 主页面缩略图变高，电平/声相旋钮使用更大的单元；Sub/Noise 的这两个旋钮纵向排列。
- 3D 缩略图使用固定投影的紧凑切片堆叠；每片只画一个周期，近片在右下、远片在左上，整组落在缩略图内。当前片由 `AudioEngine::getWavetablePosition` 上报的实时位置高亮。

## 第六十二批 · 工厂预设、波形预设与 24 dB 滤波级联

- `Core/HybridPresets.h` 是 header-only 的工厂库：**48 个预设、8 个类别、每类 6 个**（第六十七批）。`apply()` 先清空到默认，再按类别构建波表层（含每层第二振荡器）、Sub/Noise、双滤波、ENV、LFO、调制矩阵和 FX；预设不依赖工程文件或外部采样。
- `Core/WaveformPresets.h` 是 header-only 的波形库：单周期预设直接给出一个 2048 点周期，多帧预设按 8 帧插值出 0…1 的形态变化。`build()` 通过 `wavetabletables::makeFromFrames` 写成工程自定义表，内置表库不被修改。
- `Core/FmPresets.h` 的工厂库是 **32 个 DX7 ROM-1A 音色**（4 类 × 8，第六十七批），实际参数在生成的 `Core/Dx7FactoryPresets.h`：每个 builder 直接写 `FmPatch` 的六个运算器、算法、反馈、LFO 与移调；`engineAlgorithmFor()` 保存 DX7 算法到本引擎算法的对照表。`Preset` 带 `category`，供 `PresetBar` 的二级菜单分组。
- `HybridEditor` 的预设栏从 `PresetBar::Entry` 读取类别；工厂 id 为 1…48，用户预设仍从 `kUserPresetIdBase` 开始。波表编辑器通过 `onWaveformPreset` 回调把生成表写入当前振荡器，整个操作走一次 `applyEdit`。
- `PresetBar` 在 `categories.size() > 1` 时隐藏下拉框与类别框，改用一个显示当前音色名的按钮；点击后 `PopupMenu` 按类别 `addSubMenu`，原生子菜单在悬停/点击时展开，选中项回调 `loadPreset(id)`。单类别库（采样器、鼓机、粒子、减法、波表）继续走原来的下拉路径。
- 内嵌波表编辑器在混合器里预留 30 px 顶部条：返回按钮在 `(8, 4, 96, 24)`，编辑器 bounds 为 `getLocalBounds().withTrimmedTop(30)`；独立波表窗口不受这个条影响。
- `HybridSynth` 的 24 dB 滤波使用两套独立状态：`filters[which][channel]` 与 `filtersStage2[which][channel]`。两套都在 `prepare` / `reset` 里初始化，控制率同时设置类型、截止和共振；处理时第二级从 `filtersStage2` 取样。这样 24 dB 带通不会因同一状态重复推进而发散。
- 缩略图的 2D/3D 状态保存在 `HybridOscillatorThumbnail` 实例里，`setSource()` 不重置；3D 绘制把每片波形闭合到自己的基线并记录整组 extent，供自检确认没有越界。`HybridEditor` 的 30 Hz 定时器只在有活跃声部时刷新，静下来后停表。
- 每个混合层的 `WavetableSynth` 继续发布自己的位置；`HybridSynth::getWavetablePosition(layer)` 读该层的内部振荡器，`AudioEngine::getHybridWavetablePosition(trackId, layer)` 按轨道槽位转发。主页面缩略图使用它作为实时位置，而不是普通波表轨的 `getWavetablePosition`。
## 第六十三批 · 音符音高形状与连接链

- `Note` 保留 `frequencyHz` 作为头部，新增 `tailFrequencyHz`、`pitchCurve`、`tiltStart`、`tiltEnd`、`tieFromNoteId`。旧工程缺字段时头部=尾部、曲线水平、无连接。
- `music::pitchAt` 在倾斜区内做 log-frequency 插值；`sliceNotePitchShape` 负责拆分时重参数化曲线，保证原曲线和目标曲线逐点一致。
- `SynthEvent` 增加 `isPitch`。调度器把每个片段/循环周期展开后的音符按 `tieFromNoteId` 合成链：链首一个 note-on，链尾一个 note-off，中间发送约 5 ms 间隔的 pitch-only 事件。
- 内置音源增加 `setNoteFrequency(noteId, hz)`：FM、减法、波表、混合直接改声部频率；采样/粒子同步改播放速率；鼓机忽略。VST 不接收 pitch-only 事件，但连接链仍折叠成单音头。
- `NoteEditBar` 的头/尾字段写回同一个音符；曲率/倾斜第三行使用浮动布局，不改变画布高度。`AppState::currentPitchEndpoint` 是会话态，不写入工程。
- `music::pruneInvalidNoteTies` 在工程读取、删除、移动、缩放、贴片和拆分后清理不连续、分叉或成环的连接。
## 第六十四批 · 分区吸附、连接链跟随与 CC 简化

- `EditorViewState` 的单一吸附拆成 `trackSnap`、`editorSnap`、`controllerSnap` 三个 `SnapSettings`。`SnapControl` 直接修改对应设置，标尺、音轨、PianoGrid、ControllerLane 和 MIDI 录入分别读取自己的区域。
- 工程文件写 `trackSnapEnabled/Denominator`、`editorSnap...`、`controllerSnap...`；读取时若缺字段，用旧 `snapEnabled/snapDivisor` 给三者做迁移默认值。
- PianoGrid 解析当前音符的合法连接链：中部拖动扩展 drag set 到整链；端点拖动记录接缝 partner；边缘拖动记录前驱或后继链。提交时在同一 undo 快照内更新全部相关音符。
- 钢琴背景行和键帽统一由 `pianoRowRect` 从 log-frequency 轴计算，避免 row 编号取整造成错位；白键行/黑键行使用相反明暗。
- ControllerLane 在无吸附绘制时按像素桶限采样，松手后对该手势区间的折点做屏幕坐标 Ramer–Douglas–Peucker 简化（2 px 容差）。
- 力度拖动在按下时立即写入鼠标 y 对应的绝对力度，拖动阶段持续更新同一组选中音符。

## 第六十五批 · 钢琴窗边界、端点时间拖动与接缝跟随者

- `paintPianoRows` 在已应用画布 transform 的局部坐标中绘制；它把共享的 `pianoRowRect` 减去 `gridArea` 原点，消除左侧键盘列宽的空洞。
- `PianoGrid::hitTestNote` 的命中顺序为连接倒三角、音符内部端点圆点、倾斜线外伸尖端、3 px 外层时长边缘、音符本体。外层时长边缘不会被圆点完全遮挡。
- 端点拖动新增 `pitchHandlePreviewTickDelta`：横向以指针 tick 增量计算并吸附，纵向继续用频率因子；提交时在同一条撤销里同时改端点时间与频率。
- 连接链本体拖动不再把整链装入 `dragOriginals`，而是记录 `SeamFollower`。每个未选中的直接前驱只让尾部跟随拖动音符的头部，每个直接后继只让头部跟随拖动音符的尾部，外侧端点保持原位。
- `EditorPanel` 将 `ControllerLane` 与 `laneResizer` 同时裁掉键盘列和右侧滚动条宽度，力度/CC 的 tick 映射因此与 `PianoGrid::gridArea()` 完全共享左右边界。

## 第六十八批 · DX7 兼容的 FM 引擎

- `kFmAlgorithmCount = 64`：`kAlgorithms` 前 32 条是 HM 原路由，后 32 条是 DX7 原厂路由（`getFmAlgorithm(32 + N)`）。`FmAlgorithm` 新增 `feedbackOp`（0 基），旧条目默认 5（OP6）；原厂 2/8/10/12/15/18/20/21/27 指向别的运算器。
- 引擎按 `FmPatch::classicEngine` 走两套曲线。经典：`levelToAmplitudeClassic(l) = 10^((l-99)*0.75/20)`（0 = 静音）、`fm::rateToSecondsClassic`（99 ≈ 1 ms、0 ≈ 20 s）、速率缩放每八度 ×2、力度按 `(1-sqrt(vel))` 折算、调制指数 1 周期、反馈 1 周期。旧引擎保留原二次曲线、`rateToSeconds`、2 周期调制指数与 0.35 反馈。
- `Voice` 新增每运算器的电平偏移（键盘缩放 + 力度，note-on 时算一次）和 pitch EG 状态；`TrackSynth::globalLfoPhase` 在 `classicEngine && !lfoSync` 时按块推进，所有声部共享同一相位。
- 键盘缩放曲线：`keyScaleShape` 把断点距离（半音，最多 96）映射到 -LIN/-EXP/+EXP/+LIN 的 0..1 形状，乘深度（0..99）得到电平偏移；固定频率运算器忽略音高与移调。
- 音高 EG：从 L1 出发，R1→L2、R2→L3 后保持，释放 R4→L4；电平 50 = 0，`(l-50)/50*48` 半音。
- LFO：经典顺序为三角/下锯齿/上锯齿/方波/正弦/S&H，旧引擎保留原 5 种顺序；PMD 深度按 `pmSens/7 * 200` 音分，AMD 按每运算器 `amSens/3` 作用。
- 序列化：`synthParamsToVar`/`synthParamsFromVar` 写读 `classic`、`pitchEgRate/Level`、`lfoPitchModSens/lfoSync`，每个运算器写读 `mode/fixedHz/breakPoint/leftCurve/rightCurve/leftDepth/rightDepth/amSens`；缺 `classic` 的文件读成 false。
- `Core/Dx7FactoryPresets.h` 由 `work/dx7-convert/generate3.py` 生成：`p.algorithm = 32 + 原厂编号`、`classicEngine = true`，并写入固定频率、键盘缩放、AM 灵敏度、音高 EG 与 LFO 设置。
- 默认运算器包络为 `{99, 99, 99, 0}`（仍是 4 段、L4 = 0）；工厂预设显式覆盖，默认工程因此是常驻正弦而不是会衰减的短音。

## 第六十九批 · Minimoog Model D 乐器

- `TrackKind::minimoog` 追加在枚举末尾（序列化值 9），`InstrumentTrack::minimoog` 保存 `MinimoogParams`；`TrackRenderInfo` 与 `buildSchedule` 原样转发。
- `Audio/MinimoogSynth.{h,cpp}`：`kMaxMinimoogVoices = 64` 预分配声部；单音模式用 `std::vector<HeldNote>` 做后按优先 + legato（无按住音时重触发，已有按住音只改目标音高）；复音模式按 `polyphony` 分配，优先空闲、其次已释放的最旧、最后最旧。滑音只在单音模式生效，log 频率指数逼近。
- 振荡器：`MinimoogRange` 倍率 `lo=1/64, 32'=0.25, 16'=0.5, 8'=1, 4'=2, 2'=4`；`MinimoogWave` 六种，锯齿/方波/脉冲用 PolyBLEP，三角与斜三角用朴素形状；每个振荡器加 ±2 音分慢漂移。`osc3Control=false` 时振荡器 3 用 C4 参考频率，不跟随键盘。
- 混音：`Σ level·sample + noise·level`，`tanh(1.5·x)/0.905` 过载；噪声白/粉（粉用两级一阶滤波近似）。
- 滤波：自写 `minimoog::LadderFilter`，四节一阶低通 + 反馈 + 输入 tanh 饱和；`g = 1-exp(-2π·fc/sr)` 上限 0.85，反馈量 `4×emphasis`。截止 = 基础 × `2^(键盘跟随 + 滤波包络×包络量×力度因子 + 调制)`，clamp 10–20000。
- 包络：自写指数包络（attack→decay→sustain→release），系数 `1-exp(-4/(T·sr))`；维持为固定 0.8，`DecayShort` 把衰减时间乘 0.1，释放 80 ms（关闭时 2 ms）。
- 力度：`velGain = 1-sens + sens·(vel/127)^1.2`，乘输出并缩放滤波包络深度。
- `Core/MinimoogPresets.h` 是 header-only 工厂库：30 个预设、6 类、每类 5；`applyPreset` 重置参数后保留当前 `polyphony` 与 `velocitySensitivity`。
- 序列化：`minimoogParamsToVar/FromVar`、工程 `"minimoog"` 字段、预设库 `"minimoogPatches"` 段；`ProjectDocument` 的 kind clamp 到 0..9。
- 预览：`PreviewKind::minimoog` 与 `triggerPreviewMinimoogNote/updatePreviewMinimoogParams`，编辑器键盘与旋钮试听走它。
- 界面：`UI/MinimoogEditor.{h,cpp}`，设计尺寸 1120×680；`SynthWindow` 为它使用独立页宽高；`TrackArea` 的 + 菜单新增「迷你穆格」（id 10），新建时套「经典主音」。
