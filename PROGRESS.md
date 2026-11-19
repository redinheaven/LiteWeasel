# PROGRESS.md — 进展日志

最新的记录放在最上面。格式见 `AGENTS.md` 第 5 节。

---

## 2026-11-19 · 第 0 阶段：按键延迟计时（ETW，T1/T3/T5/T7）

分支：`perf/phase0-timing`（从 master `f96ed24` 切出，**尚未提交**）。

**做了什么**：用 ETW（TraceLogging）加了可开关的计时，只有 T1、T3、T5、T7 四组。没有行为改动，没有改 COM 接口、IPC 消息格式和 DPI 逻辑。

| 阶段名 | 位置 | 测量内容 |
|---|---|---|
| `tsf.ipc_key_roundtrip` | `WeaselTSF/KeyEventSink.cpp:39` | T1：`m_client.ProcessKeyEvent` 的整个 IPC 往返 |
| `srv.on_key_event` | `WeaselIPCServer/WeaselServerImpl.cpp:238` | T3：服务端 `OnKeyEvent` 从进入到返回 |
| `tsf.edit_session_wait` | `WeaselTSF/Composition.cpp:399` → `WeaselTSF/EditSession.cpp:9-13` | T5：`RequestEditSession` 到 `DoEditSession` 被回调的等待 |
| `tsf.do_edit_session` | `WeaselTSF/EditSession.cpp:15` | T5：`DoEditSession` 全程（含其中同步的 UI 刷新） |
| `tsf.get_response` | `WeaselTSF/EditSession.cpp:25` | T5：`GetResponseData` 读取并解析回包 |
| `ui.do_paint` | `WeaselUI/WeaselPanel.cpp:425` | T7：`DoPaint` 全程 |
| `ui.present` | `WeaselUI/WeaselPanel.cpp:502` | T7：`swapChain->Present(1, 0)` 单独计时（验证它是否被垂直同步阻塞） |

**新增文件**

- `include/WeaselPerf.h`：provider 声明、`WEASEL_PERF_SCOPE` 宏和 `Emit`。provider 名 `LiteWeasel-Perf`，GUID `{5974258d-005e-4490-adef-9e265b26abf5}`，事件 `Stage{stage, us}`。
- `WeaselUI/WeaselUI.cpp:6` 定义并注册 provider。`WeaselUI.lib` 同时被 `weasel.dll`、`WeaselServer.exe`、`WeaselDeployer.exe` 链接，所以只需定义一次。
- `tools/perf/summarize.py`：读 `tracerpt` 导出的 CSV，输出各阶段的 p50、p95、最大值。

**设计取舍**

- 关闭时（没有 ETW 会话监听）每个点只多一次内联的启用判断；不读注册表，不写文件，不分配内存，不抛异常。
- **不记录键码**，避免变成键盘记录。事件只有阶段名和耗时，进程号、线程号、时间戳来自 ETW 事件头（QPC，跨进程可比）。
- 不改 IPC 协议。宿主进程和服务端的事件靠时间戳对齐。
- `g_editRequestQpc` 是 `thread_local`，因为 `RequestEditSession` 和 `DoEditSession` 在同一个 TSF 线程上。

**验证**

| 平台 | 结果 | 警告 |
|---|---|---|
| x64 Release | 通过，0 错误 | 19（均为上游已有的 C4244/C4267/C4018 等；`WeaselPerf.h` 及我改动的行没有产生警告） |
| Win32 Release | 通过，0 错误 | 15（同上） |

这两个警告数是增量编译的数字，不是完整基线，不能直接和之前的 31 比。`weaselx64.dll`、`weasel.dll`、`WeaselServer.exe`、`Win32/WeaselServer.exe` 里都能找到字符串 `LiteWeasel-Perf`，说明 provider 已进二进制。

**ETW 验证（只验证了服务端，T3）**

本机已装官方小狼毫 0.17.4（`C:\Program Files\Rime\weasel-0.17.4`，System32/SysWOW64 有官方 `weasel.dll`）。用户选择**不替换已安装的输入法**，所以只验证了服务端：

1. 临时把 `HKCU\Software\Rime\Weasel\RimeUserDir` 指向空的测试目录 `C:\Users\Administrator\lwtest_rime`（只装 `luna_pinyin`），避免新版 `rime.dll`（2026-06）动用户的词库（原用 2025-02 版）。
2. 用 Idle 优先级启动 `output\WeaselServer.exe`，开 ETW 会话，用测试客户端 `ipc_keys.py` 经命名管道发按键。
3. 验证完恢复 `RimeUserDir`（原值 `C:\Users\Administrator\AppData\Roaming\Rime`），删除测试目录，停掉我们的服务端。`%APPDATA%\Rime` 大小前后一致（267.56MB）。

结果：发送 180 个按键事件（90 个按下、90 个松开，其中 90 个被处理），**抓到 180 条 `srv.on_key_event`**。`tracerpt` 导出的 CSV 行形如 `Stage, ..., "srv.on_key_event", 1084`，`summarize.py` 已按此格式修正并验证：

| 阶段 | 次数 | p50 (µs) | p95 (µs) | max (µs) |
|---|---|---|---|---|
| `srv.on_key_event` | 180 | 542 | 1314 | 1529 |

这是台式机在 Idle 优先级下、测试方案 `luna_pinyin`、无候选窗的数据，**不是基准，不能用来和目标机器比较**。

**验证过程中发现并修复的错误**

- 第一次抓取 0 个事件。原因是 `WeaselUI/WeaselUI.cpp` 里 provider 的 GUID 字节写错了（最后 8 字节写成 `ad ef 9e 26 5a bf 26 b5`，应为 `ad ef 9e 26 5b 26 ab f5`），注册的是另一个 GUID。已修正，重编 x64、x86 均 0 错误 0 警告。
- 抓取时 `logman` 要显式写 `0xffffffffffffffff 0xff`（关键字和级别）。`TraceLoggingWrite` 默认级别是 Verbose，会话级别太低会被过滤。

**还没验证的**

1. **T1、T5、T7（在宿主进程里的 `weasel.dll`）没有验证**，因为没有装我们的 `weasel.dll`。用户盲打那次也没有产生事件，原因没查：我们的服务端当时用的是官方 `weasel.dll`，且 GUID 当时是错的。
2. 没有测过开启和关闭时的额外开销。
3. 没有任何目标机器上的延迟数据。

**抓取方法**

```
logman create trace lw -p "{5974258d-005e-4490-adef-9e265b26abf5}" 0xffffffffffffffff 0xff -o lw.etl -ets
（打字）
logman stop lw -ets
tracerpt lw.etl -o lw.csv -of CSV -y
python tools/perf/summarize.py lw.csv
```

**遗留和下一步**

1. 在目标机器（X1 Carbon 5th、X220）上完整安装我们的构建，验证 T1、T5、T7 能抓到。那里没有官方版本需要保护的话，这一步最合适。
2. 在目标机器（X1 Carbon 5th、X220）上测四个场景：新程序第一个字、连续快速输入、部署期间输入、睡眠唤醒后第一个字。
3. 看数据再决定要不要补 T0、T2、T4、T6、T8、T9。
4. `Present(1, 0)` 是否阻塞 UI 线程，只是推测，要看 `ui.present` 的数据。

---

## 2026-11-19 · 第 0 阶段：台式机构建环境搭建与基线编译

**结果**：未改动的代码（master，`f96ed24`）在 x64 和 x86 下都编译通过，0 个错误。没有修改任何受版本控制的源文件。

| 平台 | 结果 | 警告数 | 说明 |
|---|---|---|---|
| x64 Release | 通过 | 56（首轮因缺 MFC 在资源编译处中断，数字不是完整基线）；补装 MFC 后增量编译 0 个新警告 | 要拿到完整的 x64 警告基线，需要做一次 rebuild |
| Win32 Release | 通过 | 31 | 耗时约 2 分 22 秒 |
| ARM64 | 未编译 | — | 已装 ARM64 工具和 MFC，尚未验证 |

警告大多是上游已有的 `C4267`（size_t 转 int）。`output/` 下已生成 `WeaselServer.exe`、`WeaselDeployer.exe`、`weasel.dll`（x86）、`weaselx64.dll`。

**环境（本机）**

- VS 2022 Build Tools 17.14，MSVC 14.44.35207，Windows SDK 10.0.22621，已装 x64/x86/ARM64 工具、ATL、MFC（含 ARM64）和 CMake。
- 7-Zip、aria2 通过 winget 安装。
- Boost 1.84.0 放在 `deps/boost_1_84_0`（x86 和 x64，静态库，Release 和 Debug）。
- librime 用 `get-rime.ps1 -use dev` 下载预编译包（`33e7814`），已放入 `lib`、`lib64`、`output`。
- `env.bat`（已在 `.gitignore`）：`BOOST_ROOT=deps\boost_1_84_0`，`BJAM_TOOLSET=msvc-14.3`，`PLATFORM_TOOLSET=v143`，`DEVTOOLS_PATH` 含 Git、7-Zip、CMake、WinGet Links。

**硬件限制（重要）**

本机主板有 CPU 过热保护性断电的问题，24 个逻辑核心不能满载。所有编译都必须限制并行度：

- b2 用 `-j2`，不要直接用 `build.bat boost`（它的 `-j%NUMBER_OF_PROCESSORS%` 在 vcvars 之后会变回 24，我设的覆盖没有生效）。
- msbuild 加 `/p:CL_MPCount=2 /m:1`。项目里 `MultiProcessorCompilation=true` 没有限制个数，不加会用满 24 核。
- 整条构建链用 `start /low`（Idle 优先级）启动。
- 编译期间要看 HWiNFO 的 CPU 温度。本次编译时用户确认温度在 30 多度。
- 不要强杀正在编译的 cl，会留下损坏的 `.obj`（`LNK1136`），本次 Boost 就因此重编过一次。

**遇到的坑和解决方法**

| 问题 | 原因 | 解决 |
|---|---|---|
| `call env.bat` 报"不是内部或外部命令" | vcvars 设置了 `NoDefaultCurrentDirectoryInExePath`，当前目录不再被搜索 | 在 vcvars 之后 `set NoDefaultCurrentDirectoryInExePath=` |
| b2 所有目标被跳过（`msvc-setup.nup`） | 装了 ARM64 工具后，b2 选中 `Hostx64\arm64\cl.exe`，又找不到 `bin\Hostx64\vcvarsall.bat` | 改 `deps/boost_1_84_0/project-config.jam`：`using msvc : 14.3 : <x64 的 cl.exe> : <setup><VC\Auxiliary\Build\vcvarsall.bat> ;`（该文件在被忽略的 `deps/` 下） |
| x86 的 filesystem、json、locale 没有生成 | b2 缓存了编译器配置失败时的检测结果 | 删除 `bin.v2/project-cache.jam` 和 `config.log` 后重跑。注意：x64 库是在缓存失效时编的，个别特性检测（如 BCrypt）可能走了回退路径，功能上没发现问题，出现异常时先怀疑这里 |
| `render.js` 没有执行，`weasel.props` 缺失 | 本机 `.js` 没有脚本引擎关联（`There is no script engine for file extension ".js"`） | 手动执行 `cscript //nologo //E:jscript render.js weasel.props <变量名...>` |
| `RC1015: cannot open include file 'afxres.h'` | 没装 MFC | `setup.exe modify --add Microsoft.VisualStudio.Component.VC.ATLMFC --add Microsoft.VisualStudio.Component.VC.MFC.ARM64` |
| `build.bat` 的 data 步骤失败 | `plum` 子模块没有初始化（`plum/rime-install` 不存在）。`librime` 子模块同样没有初始化，但本项目用预编译包，不需要 | 本次绕开 data 步骤，直接调用 msbuild。`output/data` 里目前只有 `opencc` 和 `weasel.yaml`，没有 `essay.txt` 等，运行和打包前需要 `git submodule update --init plum` |

**手动编译命令**（在 `vcvars64.bat` 之后，工作目录为仓库根目录，先 `call env.bat`）：

```
cscript //nologo //E:jscript render.js weasel.props BOOST_ROOT PLATFORM_TOOLSET VERSION_MAJOR VERSION_MINOR VERSION_PATCH PRODUCT_VERSION FILE_VERSION
msbuild weasel.sln /t:Build /p:Configuration=Release /p:Platform=x64   /p:CL_MPCount=2 /m:1
msbuild weasel.sln /t:Build /p:Configuration=Release /p:Platform=Win32 /p:CL_MPCount=2 /m:1
```

**遗留和下一步**

1. 做一次 x64 rebuild，记下完整的警告基线（目前只有 x86 的 31 是完整数字）。
2. ~~初始化 `plum` 子模块，补齐 `output/data`~~ 已完成（`plum` 在 `cab9ed3`，`output/data` 里有 7 个方案包共 29 个文件，含 `essay.txt`、`luna_pinyin` 等）。注意子模块里下载的方案包在 `plum/package/`，不进仓库。
3. 加可开关的计时日志（第 0 阶段第 2 项），先出方案：计时点放在哪些文件的哪些位置。
4. 开始改代码前，先从 master 切出功能分支。
5. 建议把"CPU 不能满载"的硬件限制写进 `AGENTS.md`，等用户确认。

---

## 2026-11-19 · 第 0 阶段：可行性评估与顶层设计

**做了什么**

- 阅读 `initial task.txt`，浅克隆 fork 仓库并对照代码，评估四个阶段的可行性。
- 和用户确认取舍，更新顶层设计（见 `initial task.txt`），新建本文件、`AGENTS.md` 和 `README.md`。

**发现：任务书和代码不一致的地方**

| 任务书的假设 | 代码里的实际情况 |
|---|---|
| `WeaselUI/DirectWriteResources.cpp` 用 `CreateDCRenderTarget` + `TYPE_DEFAULT` | 没有这个文件。`WeaselUI/d2d.cpp:43` 已经优先用 WARP 创建 D3D11 设备，失败时退回硬件，交换链走 DComp |
| 需要把 UI 改成懒加载 | `WeaselTSF/CandidateList.cpp:316` 是有意提前预热的 |
| 把 Server 设成 HIGH 优先级就够了 | 部署维护跑在 Server 进程内（`RimeWithWeasel/RimeWithWeasel.cpp:113`），整个进程提权会连部署一起提上去 |
| 服务端没运行时连接会阻塞 | 服务端没运行时，`CreateFile` 会立刻失败并返回，这个场景本来就很快。真正的风险是：`PipeChannel.cpp:43` 在管道一直 BUSY 时会无限循环；`_Receive` 和 `_WritePipe` 同步读写、没有超时 |
| — | `PipeChannel.cpp:76` 每次写完都调用 `FlushFileBuffers`，相当于多等一次对端读取 |

**用户决定**

- 第 1 阶段：只验证，不改代码，保留退回硬件的逻辑。
- 第 2 阶段：保持现在的预热，不做懒加载。理由是第一个字快比省内存更重要。
- 本机没有 VS，项目会转到装了 VS 的台式机上继续。
- 把 fork 仓库完整克隆进项目文件夹（master 分支），用新写的 `README.md` 替换上游的那份。上游的安装说明仍在 `INSTALL.md` 里。

**遗留和下一步**

1. 在台式机上搭好构建环境，确认 x64 和 x86 的基线都能编译通过。
2. 加计时日志，测出按键全链路的基准耗时。
3. 第 1 阶段验证：用 Process Explorer 确认宿主进程没有加载显卡驱动的用户态 DLL。
