# 构建与环境

## 本机环境（已验证可构建）

| 项 | 值 |
| --- | --- |
| 操作系统 | Windows |
| 编译器 | Visual Studio 2026 Enterprise，MSVC 14.51（`C:\Program Files\Microsoft Visual Studio\18\Enterprise`） |
| Windows SDK | **在 D 盘**：`D:\Windows Kits\10\10.0.26100.0`（不在 C 盘，别误判为缺失） |
| CMake | 4.3.2（`C:\Program Files\CMake\bin\cmake.exe`） |
| JUCE | 8.0.15，已随源码放在 `external/JUCE`，**不需要联网** |
| 系统代码页 | 936（GBK），因此源码必须按 UTF-8 编译 |

## 构建

```powershell
# 先进入 MSVC 环境，然后：
cmake -S HMDAW -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target HMDAW        # 主程序
cmake --build build --target HMDAWTests   # 单元测试
```

产物位置：`build/HMDAW_artefacts/Release/HM.exe`（CMake 的 target 仍叫 `HMDAW`，
所以中间目录是 `HMDAW_artefacts`；exe 名来自 `PRODUCT_NAME`，现在是 `HM`）。

## 已知陷阱

### 1. `/showIncludes` 前缀（已修好，增量构建可用）

**根因已查明**：本机 MSVC 是中文版，`/showIncludes` 的输出前缀是「注意: 包含文件:」，
而且是 **UTF-8** 字节；CMake 却按 936 ANSI 代码页解码，于是记成了乱码：

```
set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "娉ㄦ剰: 鍖呭惈鏂囦欢:  ")   # CMake 记下的（错）
真实输出：E6 B3 A8 E6 84 8F 3A 20 ...                             # cl.exe 打印的（对）
```

于是 Ninja 永远匹配不上编译器真实输出的那一行，**依赖文件是空的** ——
改头文件根本不会触发重编（症状是链接出按旧结构体尺寸编译的对象，随机崩溃）。

- `VSLANG=1033` 无效：cl.exe 仍然输出中文。
- **CMAKE_CXX_DEPENDS_USE_COMPILER=OFF 也无效**：Ninja 生成器仍然走 `deps = msvc`。
- **在 `CMakeLists.txt` 里设 `CMAKE_CXX_CL_SHOWINCLUDES_PREFIX` 也无效**：`project()`
  探测完就写进 `CMakeFiles/<ver>/CMakeCXXCompiler.cmake`，之后覆盖不掉（而且 CMake 读
  没有 BOM 的 CMakeLists 时按本地代码页解码，中文字面量还会再乱一次）。

**现在的做法**：`work/fix_showincludes_prefix.ps1` 在第一次配置之后，把
`CMakeFiles/<ver>/CMakeCXXCompiler.cmake` 里那一行改回编译器真正打印的 UTF-8 字节，
再配置一次；`CMakeFiles/rules.ninja` 里的 `msvc_deps_prefix` 就对上了。
两个构建脚本每次都会跑它，并且在「刚刚修好」的那一次自动 clean 一遍，把用坏前缀录下的
依赖数据库重建掉。脚本本身是纯 ASCII，用字节数组拼出前缀（Windows PowerShell 5.1 读
无 BOM 的 .ps1 时按 ANSI 解码，写中文字面量会坏）。

**用法**：

```powershell
work\build_app.bat HMDAW          # 增量（改了头文件也会正确重编）
work\build_app.bat HMDAW clean    # 强制全量（约 80 秒）
work\build_tests22.bat            # 增量
work\build_tests22.bat clean      # 强制全量
work\make_release.bat             # 发布：两个目标都强制 clean
```

实测：改 `UI/GranularEditor.h` 后增量重建 **14 秒**、只重编 5 个对象；改 `Core/Types.h`
会重编 71 个依赖对象（这才是正确行为）。如果以后又出现「改了头文件没重编」，先跑一次
`clean`，再看 `CMakeFiles/rules.ninja` 里的 `msvc_deps_prefix` 是不是
`注意: 包含文件:  `（两个尾随空格）。

如果编译被中途打断、或依赖前缀又坏了，增量构建可能留下**按旧结构体尺寸编译的目标文件**，
表现为随机的访问违例。实际踩过：给 `SynthParams` 加了两个字段后，`TrackArea.o` 仍在按旧的
元素尺寸遍历轨道数组，崩在 `0xFFFFFFFFFFFFFFFF`。遇到这种情况直接：

```powershell
work\build_app.bat HMDAW clean
```

### 2. 中文源码必须 `/utf-8`

系统代码页是 936，不加 `/utf-8` 会把 UTF-8 的中文串按 GBK 解读，产生乱码甚至编译警告。
`CMakeLists.txt` 里有一条全局 `add_compile_options(/utf-8)`，不要删。

### 3. 不要用 `juce::String(const char*)` 直接装中文

该构造函数按 ASCII 处理，遇到大于 127 的字节会断言。统一走 `NI_TXT("…")` 宏。

### 4. JUCE 启动闪屏涉及授权

`CMakeLists.txt` 刻意**没有**定义 `JUCE_DISPLAY_SPLASH_SCREEN=0`：关闭闪屏需要
JUCE Indie/Pro 授权（或按 AGPLv3 发布）。拿到授权后加上该宏即可。

### 5. 崩溃排查工具链（已内置）

用 `--selftest` 运行时若发生访问违例，程序会把异常码、指令地址与模块基址写进
输出目录的 `crash.log`；链接时生成了 `HMDAW.map`，两者结合就能把绝对地址还原成函数名：

```powershell
# 1. 算出偏移： 偏移 = 指令地址 - 模块基址
# 2. 在映射表里找不超过该偏移（相对 0x140000000）的最后一个符号
Select-String -Path build\HMDAW.map -Pattern "^ 0001:"
# 3. 需要更精确时用反汇编核对
dumpbin /disasm:nobytes "build\HMDAW_artefacts\Release\HM.exe" > disasm.txt
```

## 依赖与网络

- 运行时只依赖系统自带的 VC++ 运行库，交付的是单文件 exe。
- JUCE 源码已经在本仓库里，正常构建**不需要网络**。
- 若将来要重新拉取 JUCE：本机 Git 与 curl 的 schannel 取不到 TLS 凭证，
  `git clone` 会失败（`SEC_E_NO_CREDENTIALS`）。可行做法是用 Node 自带的 TLS 栈下载
  GitHub 的 tarball（`node --use-system-ca` 配合一个简单的 https 下载脚本）。

### 6. 本机代码签名（自签名，只对本机有效）

双击 `HM.exe` 报「无法验证发布者」是**没签名**，不是构建问题。本机用一张自签名证书解决：

```powershell
# 一次性：生成（或复用）证书并信任它。写 CurrentUser\Root 只能用 certutil，
# Import-Certificate 会以「此操作中不允许使用 UI」失败。
powershell -NoProfile -ExecutionPolicy Bypass -File work\signing\new_hm_signing_cert.ps1

# 每次构建之后：签名 + 校验（Valid 才算成功）
powershell -NoProfile -ExecutionPolicy Bypass -File work\signing\run_sign.ps1 `
    work\app-build5\HMDAW_artefacts\Release\HM.exe
```

- 证书：`CN=HM, O=HM, OU=Local build`，RSA 3072 / SHA-256 / 5 年；私钥在
  `Cert:\CurrentUser\My`，公钥同时装进 `CurrentUser\Root` 和 `CurrentUser\TrustedPublisher`。
- `make_release.bat` 的 `[1b]` 会自动给主程序签名；签名失败则中止发布。
- 撤销：从 `CurrentUser\My`、`Root`、`TrustedPublisher` 三处删除这张证书即可。
- **边界**：它只让这台机器、这个 Windows 账户把 HM 认成「HM」。换机器要重新生成证书；
  发给别人仍然显示未知发布者，那需要正式的代码签名证书（OV/EV 或 Azure Trusted Signing），
  拿到后把 `work\signing\thumbprint.txt` 换成新证书的指纹，发布流程不用改。
- 自签名**不能**通过 SmartScreen 的「无法识别的应用」检查（它只认微软信誉库）。本机开发机
  若仍被拦，可运行 `work\disable_smartscreen_app_check.ps1`（需管理员，改两个 HKLM 值并
  重启 Explorer）；分发版本只能靠正式证书。
