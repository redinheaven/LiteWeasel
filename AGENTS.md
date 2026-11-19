# AGENTS.md — Lite_weasel 工程规范

本文件写给参与本项目的 AI 代理和开发者。动手之前必须先读完，并同时阅读 `initial task.txt`（顶层设计）和 `PROGRESS.md`（当前进度）。

## 1. 项目定位

- 上游：`rime/weasel`（小狼毫，Windows 平台的 Rime 输入法前端）
- Fork：https://github.com/redinheaven/Lite_weasel
- 目标机器：低配旧笔记本，比如 ThinkPad X1 Carbon 5th（KabyLake-U）、X220。特点是内存小、CPU 低压低频。
- 核心目标：打字丝滑。这包括第一个字出得快、按键响应不粘滞、宿主程序界面不被输入法拖死。

### 首要取舍原则

**第一个字的出字速度优先于省内存。** 如果延迟和内存冲突，选低延迟。比如图形设备在线程获得焦点时就提前创建（`WeaselTSF::OnSetThreadFocus` → `weasel::PrewarmGraphics()`，实测每个进程多占约 3.9MB），候选窗的 `Refresh()`+`Hide()` 预热保持不变，不做懒加载。

## 2. 代码结构速查

| 目录 | 作用 | 运行在哪个进程 |
|---|---|---|
| `WeaselTSF/` | TSF 文本服务（`weasel.dll`），负责按键拦截和组字 | **每个宿主程序内**（InprocServer32） |
| `WeaselUI/` | 候选窗绘制（D2D/DirectWrite/DComp） | 宿主程序内（TSF 路径） |
| `WeaselIPC/` | 客户端与服务端之间的命名管道通信 | 两端都有 |
| `WeaselIPCServer/` | 服务端 IPC 实现、单实例互斥量 | `WeaselServer.exe` |
| `WeaselServer/` | 服务端入口（`_tWinMain`）、托盘 | `WeaselServer.exe` |
| `RimeWithWeasel/` | 对接 librime，负责会话、部署维护（`start_maintenance`） | `WeaselServer.exe` |
| `WeaselDeployer/` | 部署和设置界面 | `WeaselDeployer.exe` |

关键事实，已在 2026-11-19 对照代码和实测核实过（以代码为准；与旧版本文件和 `initial task.txt` 里的说法不一致处已更正）：

- `WeaselUI/d2d.cpp` 优先使用 `D3D_DRIVER_TYPE_WARP` 创建 D3D11 设备，失败时才退回硬件，交换链用的是 `CreateSwapChainForComposition`。仓库里没有 `DirectWriteResources.cpp`，也没有 `CreateDCRenderTarget`。D2D 工厂现在用 `D2D1_DEBUG_LEVEL_NONE`（原来是 `INFORMATION`，会加载调试层，实测每次绘制慢约一倍）。
- **`CCandidateList::StartUI`（含 `Refresh()` 后接 `Hide()` 的预热）是从 `WeaselTSF::_StartComposition`（`Composition.cpp`）调用的，也就是第一次组字时才执行，不是"获得焦点就预热"。** 预热本身**必须保留**，也不能把 D2D 资源的创建推迟到第一次绘制。与窗口无关、占大头的图形设备初始化（`DeviceResources::EnsureInitialized()`，约 17ms）现在在线程获得焦点时提前做，用 `std::mutex` 保护。每次组字结束时 `EndUI` 会销毁候选窗，下一次组字重建（约 2.7ms），`DeviceResources` 单例保留。
- 词库部署和维护跑在 **WeaselServer 进程内的线程**里，不是只在 WeaselDeployer.exe 里。服务端所有请求都在全局互斥锁 `g_api_mutex` 下串行处理；`EndMaintenance()` 经 `Initialize()` 里的 `join_maintenance_thread()` 可能长时间持锁（读代码得出，没有复现过）。
- `WeaselIPC/PipeChannel.*` 已在第 4 阶段改过：连接总时长封顶 250ms；客户端句柄是重叠 I/O，读写带超时，超时就关闭这根管道；两端都不再调用 `FlushFileBuffers`（它无法限时，服务端还是在持有全局锁时调用）；`ClientImpl::_SendMessage` 有进程内熔断器（`Client::Suspended()`）。线路格式没有变。仍然成立的事实：服务端管道句柄是同步的；TSF 每个按键先 `Echo` 一次再 `ProcessKeyEvent`，所以每个按键至少两次管道往返（还没改）。

## 3. 构建

- 主流程：`weasel.sln` + `build.bat`（MSBuild，VS2022），参考 `env.vs2022.bat`、`weasel.props.template`
- 备选：`xmake.lua`
- 依赖：Boost（`install_boost.bat`）、librime（`get-rime.ps1`）
- **每完成一个阶段，必须保证 x64 和 x86 都能编译通过，且没有新增错误或警告。** 有条件时也要编译 ARM64（本机装了 ARM64 工具和 MFC，但没有编译验证过）。
- 构建要在装了 VS 的台式机上进行。本机环境：VS 2022 Build Tools 17.14（含 ATL、MFC、ARM64）、NSIS、7-Zip、aria2、Boost 1.84（`deps/`）、预编译 librime（`get-rime.ps1 -use dev`）、`plum` 子模块已初始化、`env.bat`（已 `.gitignore`）。

### 本机的硬件限制和构建命令

**本机有 CPU 过热导致保护性断电的主板问题，24 个逻辑核心不能满载，不能长时间高频使用 CPU。** 所有构建和测试都要限制并行度，并且在编译期间请用户留意 HWiNFO 的 CPU 温度：

- MSBuild：`/p:CL_MPCount=2 /m:1`。项目里 `MultiProcessorCompilation=true` 没有限制个数，不加就会用满全部核心。
- Boost：直接调用 `b2 -j2 ...`，**不要用 `build.bat boost`**（它的 `-j%NUMBER_OF_PROCESSORS%` 在 `vcvarsall` 之后会变回 24）。
- 整条构建链用 `start /low` 以 Idle 优先级启动。
- 不要强杀正在编译的 `cl.exe`，会留下损坏的 `.obj`（`LNK1136`）。
- 出安装包：`makensis /DWEASEL_VERSION=0.17.4 /DWEASEL_BUILD=0 /DPRODUCT_VERSION=<版本串> output\install.nsi`，同样用 Idle 优先级。
- 本机跳过 `build.bat` 的 data 步骤，直接：`cscript //nologo //E:jscript render.js weasel.props BOOST_ROOT PLATFORM_TOOLSET VERSION_MAJOR VERSION_MINOR VERSION_PATCH PRODUCT_VERSION FILE_VERSION`，再 `msbuild weasel.sln /t:Build /p:Configuration=Release /p:Platform=x64`（以及 `Win32`）。数据用 `plum` 另外装进 `output/data`。

## 4. 修改规范（硬性约束）

1. **先定位，再修改**：用 grep 或 read 精确找到目标文件和上下文，不凭记忆或文档假设去改。任务书和代码不一致时，以代码为准，并在 `PROGRESS.md` 里记录这个差异。
2. **不能破坏**：
   - COM 接口签名和 ABI 兼容性（`weasel.dll` 与系统、宿主之间的契约）
   - IPC 消息格式，客户端和服务端必须能混用版本，或者明确要求同步升级
   - 高 DPI 及多显示器缩放逻辑（`GetDpiForWindowCompat`、`m_dpiScale*`）
3. **高风险改动要先出方案**：涉及并发、IPC 协议、线程模型、架构重构时，先写设计方案和影响面分析，等用户确认后再写代码。
4. **先测量，再优化**：性能改动要给出改动前后的计时数据，或者说明测量方法。不接受只凭感觉的优化。
5. **最小改动**：一次只做一件事，不顺手重构无关代码，代码风格遵循仓库里的 `.clang-format`。
6. **跑在宿主进程内的代码**（`WeaselTSF`、`WeaselUI`）要格外谨慎：不能抛出未捕获的异常，不能长时间阻塞，不能引入大体积依赖。
7. **Git**：只有用户要求时才提交或推送。在功能分支上工作，不直接改默认分支。提交作者用仓库本地配置（`redinheaven`），不要改全局配置。
8. **不要长时间让本机 CPU 满载**（见第 3 节"本机的硬件限制"）。
9. **碰用户的词库目录之前先备份**，并且先让服务端正常退出（`WeaselServer.exe /q`），否则备份里的 leveldb 文件不完整；备份后逐文件比哈希。装新版 `rime.dll` 会让服务端重新部署用户目录的 `build/`，这是已知现象。
10. **用编辑工具改文件，不要用 `sed -i`**：它会把整个文件的换行符改掉，diff 会显示全文件改动。

## 5. 文档职责

| 文件 | 写给谁 | 内容 | 何时更新 |
|---|---|---|---|
| `AGENTS.md` | AI 和开发者 | 规范、结构、约束 | 规范或结构发生变化时 |
| `README.md` | 普通用户 | 用大白话介绍项目和效果 | 有用户能感知到的变化时 |
| `PROGRESS.md` | 所有人 | 每次的进展、改动、验证结果、遇到的坑 | **每次工作结束时必须更新** |
| `initial task.txt` | AI 和开发者 | 顶层设计和阶段规划 | 规划调整时 |

### PROGRESS.md 记录格式

每条记录写明：日期、阶段编号、改了哪些文件（`路径:行号`）、为什么改、怎么验证的（编译结果和实测数据）、遗留问题。最新的记录放在最上面。

## 6. 阶段交付要求

每个阶段结束时，回复里要包含三样东西：修改摘要、编译结果（x64 和 x86）、用户能照着做的自测方法。

## 7. 测量方法和工具

性能改动必须有前后数据。项目里自带一套按键延迟的 ETW 计时（`include/WeaselPerf.h`，provider `LiteWeasel-Perf`，GUID `{5974258d-005e-4490-adef-9e265b26abf5}`），**没有人监听时几乎零开销，也不记录键码**。

```
logman create trace lw -p "{5974258d-005e-4490-adef-9e265b26abf5}" 0xffffffffffffffff 0xff -o lw.etl -ets
（操作）
logman stop lw -ets
tracerpt lw.etl -o lw.csv -of CSV -y
python tools/perf/summarize.py lw.csv
```

- `logman` 必须显式写关键字和级别（`0xffffffffffffffff 0xff`），否则 Verbose 级别的事件会被过滤掉。
- 已有的计时点：`tsf.process_key`、`tsf.ensure_server`、`tsf.ipc_key_roundtrip`、`tsf.edit_session_wait`、`tsf.get_response`、`tsf.do_edit_session`、`tsf.start_composition`、`tsf.start_ui*`、`tsf.update_ui`、`tsf.prewarm_graphics`、`ui.update`、`ui.create*`、`ui.refresh*`、`ui.device_init*`、`ui.do_paint`、`ui.present`、`srv.on_key_event`、`srv.rime.*`、`ipc.echo/key/start_session/other`、`ipc.breaker.*`。阶段之间有嵌套，不能直接相加。
- `tools/perf/` 里有分析脚本、故障注入工具和测试程序，用途见 `PROGRESS.md` 最上面的"当前状态总览"。
- **对比必须同口径**：例如"按键之间的空档"既可能是宿主被卡，也可能是按键被快速放行，要用 `tsf.process_key` 这种直接量阻塞时间的计时点。
- **台式机的数字不能代表目标笔记本**，结论里必须写明数据来自哪台机器。

## 8. 已知踩坑（本机和本项目）

构建：

- 装了 ARM64 工具后，Boost 的 `b2` 会误选 `Hostx64\arm64\cl.exe` 并找不到 `vcvarsall.bat`，所有目标被跳过。解决：在 `deps/boost_1_84_0/project-config.jam` 里写 `using msvc : 14.3 : "<x64 的 cl.exe>" : <setup>"<VC\Auxiliary\Build\vcvarsall.bat>" ;`。`b2` 还会缓存失败时的配置检测结果，改配置后要删 `bin.v2/project-cache.jam` 和 `config.log`。
- `vcvarsall` 会设置 `NoDefaultCurrentDirectoryInExePath`，之后 `call env.bat` 找不到当前目录的文件，要先把它清空。
- 本机 `.js` 没有脚本引擎关联，`render.js` 要用 `cscript //E:jscript` 运行，否则 `weasel.props` 不会生成。
- 缺 MFC 会在资源编译处报 `afxres.h` 找不到：用 `setup.exe modify --add Microsoft.VisualStudio.Component.VC.ATLMFC`。

测试和环境：

- **从 Claude 的工具进程里直接启动的程序没有交互式桌面权限**：`DCompositionCreateDevice` 返回 `0x80070005`，`notepad` 没有窗口。需要时用 `schtasks /create ... /ru <用户> /it` 在用户的交互式会话里运行，再删除任务。
- **替换系统里的 `weasel.dll` 之后，已经运行的进程仍然在用内存里的旧 DLL**（旧 DLL 照样能连上服务端），所以要让用户关掉旧的程序、自己新开，或者注销。
- 服务端停掉后，任何一个还开着、仍在使用输入法的宿主进程里的 TSF 会按它原有逻辑自动重新启动服务端（`start_service.bat`），测试期间可能出现一个意外的真实服务端。按名字模式 `WeaselServer*` 停进程（临时副本的进程名可能不同），并在每轮测试前列出在运行的服务端。
- 服务端是单实例的（互斥量），同一时间只能有一个。
- 用户在每一步的实际操作时间点是不可控的：设计需要用户配合的实验时，要让时间线足够宽松，并且事先想清楚哪些用户操作本身会改变被测状态（例如"清空记事本"的按键也会经过输入法）。

数据和脚本：

- 写 ETW provider 的 GUID 时字节很容易写错（曾把最后 8 字节写错，provider 注册成另一个 GUID，抓取为空）。改动后先用已知会产生事件的操作验证一次。
- 用 Python 的非原始字符串写 Windows 路径会把 `\U` 当成转义；在某些工具里写含 `\\` 的 heredoc 也会被吞成单个反斜杠。
- 临时为 A/B 测试改源码后，用 `git checkout -- <文件>` 还原并确认 `git status` 干净，再提交，不要把临时代码带进提交。
