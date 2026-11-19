# PROGRESS.md — 进展日志

最新的记录放在最上面。格式见 `AGENTS.md` 第 5 节。

---

## 2026-11-19 · 当前状态总览（接手时先读这一条）

**目标**：让小狼毫在老笔记本（X1 Carbon 5 代、X220）上打字丝滑：第一个字快、按键响应不粘滞、后台出问题时宿主程序不被拖死。首要取舍：第一个字的速度优先于省内存。

**重要前提：本文件里的所有数据都来自台式机**（24 个逻辑核心、NVMe 硬盘），**没有在目标笔记本上测过任何东西**，不能直接推到老机器上。

**分支与提交**：`perf/phase0-timing`，从 master `f96ed24` 切出，**没有推送**。代码类提交：`199bfae`（ETW 计时 T1/T3/T5/T7）、`23589cc`（T6/T8，拆开首键）、`92b79df`（获得焦点时预热图形设备）、`7a5b370`（D2D 工厂不带调试层）、`e458984`（服务端按键计时）、`d7281d8`/`9b29e23`/`383d937`（第 4 阶段防卡死的三步）。另有若干 `docs:` 提交记录数据和方案。

**已取得的效果（台式机）**

| 问题 | 改动 | 实测 | 代价 / 局限 |
|---|---|---|---|
| 新程序里第一个字慢 | 获得焦点时预热图形设备（`92b79df`） | 首个按键的 `do_edit_session` 中位数 36.5ms → 19.4ms（5 个新进程） | 每个进程第一次获得焦点多阻塞约 17ms，多占约 3.9MB 工作集（1.3MB 私有字节）；笔记本上没测 |
| 候选框重绘慢 | D2D 工厂改用 `D2D1_DEBUG_LEVEL_NONE`（`7a5b370`） | `ui.do_paint` 中位数 3.38ms → 1.74ms | 目标笔记本上有没有 `D2D1Debug3.dll` 没验证，没有的话收益可能为零 |
| 候选框排版重复测文字 | `D2D::GetTextSize` 复用单个 `IDWriteTextLayout`（`SetMaxWidth`/`SetMaxHeight`）并加 256 项尺寸缓存（未提交） | `HorizontalLayout::DoLayout()`（真实 `WeaselUI.lib`）：全新 7 候选词 p50 1.944ms → 0.676ms（-65%）；重复前缀打字 1.713ms → 0.007ms；几何校验和完全一致 | 每 `WeaselPanel` 最多约 15KB 缓存，样式/DPI 变化自动清空；笔记本未测 |
| 后台卡住拖垮宿主 | 管道调用加超时、熔断，并修了几个缺陷（第 4 阶段 + 探针收紧，探针收紧未提交） | 服务端挂起 4 秒：被卡的按键 5 个/2863ms（只有超时）→ 2 个/755ms（加熔断）；组字中途挂起 8 秒：2 个/约 1.06 秒；探针收紧后管道全忙时探针耗时 250+250ms → 109.5ms | 熔断期间按键直接变成英文字母（这是设计） |
| 一个不读响应的客户端拖住所有宿主 | 去掉两端的 `FlushFileBuffers` | 另一个客户端的 `Echo`：5500ms → 0.4ms（4 轮） | 只用自己写的原始管道客户端测过，不是真实被挂起的宿主进程 |
| 每键多余的 `Echo` 往返 | 1 秒内管道通信正常则跳过 `Echo`（第 5 阶段第 3 项，未提交） | 假服务端 100 键总 IPC 耗时：10.40ms → 5.76ms（-44.6%，0 次 `Echo`）；中途服务端重启自动检测重连并重试，0 丢键 | 若服务端仅做维护而不重启且在 1 秒内完成，最多 1 秒后重新校验；笔记本未测 |
| 服务端与部署优先级 | `WeaselDeployer` 设 `BELOW_NORMAL`；`WeaselServer` 正常服务设 `ABOVE_NORMAL`，`start_maintenance` 期间临时降为 `BELOW_NORMAL`（第 3 阶段，未提交） | 自动化实测：`WeaselServer` 首次维护 10.8s 全程 `BelowNormal` → 就绪后升 `AboveNormal`；`WeaselDeployer /deploy` 10.3s 全程 `BelowNormal` | 目标笔记本低配 CPU 上的部署期打字延迟未实测 |

**已查清的发现（2026-11-19 自动化隔离测速，台式机）**

- **用户词库/配置拖慢主因已查清**（`tools/perf/rime_firstkey_bench.cpp`，从只读备份复制到临时目录，不碰真实目录）：
  - **之前误以为 `3.9–4.7ms` 是"空格上屏写用户词库"，实际空格只要 `0.08ms`**；真正花 `3.5–4.8ms` 的是打到 `nih`（`h` 键）时 `luna_pinyin_simp.custom.yaml` 里的 `translator/enable_correction: true`（相邻键纠错）。关掉这一项后，`h` 键从 `3.54–3.98ms` 降到 `0.57ms`（快 6.5 倍），整个 `nihao` 后台总耗时从 `7.78ms` 降到 `2.36ms`（-70%）。
  - 30MB `luna_pinyin_simp.userdb` 几乎零拖慢（+0.05ms 以内）；`easy_en` 影响很小。
  - `7.7–8.8ms` 的冷首键只在"最后一个会话被关闭后重新创建会话"（或服务端第 1 个会话 `s0`）时出现；只要同时还有其他活跃会话，新会话首键约 `1.5ms`（关纠错后 `0.9ms`）。
- **WARP 下阴影高斯模糊单项开销已测出**（`tools/perf/ui_bench.cpp`）：`shadow_radius=0` 帧绘制 `0.19ms`，`shadow_radius=4/8/12` 为 `0.92 / 1.00 / 1.09ms`（约占整帧绘制 80%）。默认配置与用户的 `youtube` 配色已为 `shadow_radius: 0`。
- **`start_composition` 里 `start_ui` 之外的约 6.5ms 来源（读代码）**：`Composition.cpp:57` `_UpdateCompositionWindow` 同步执行 `CGetTextExtentEditSession` → `_SetCompositionPosition` → `m_client.UpdateInputPosition`（同步 IPC）+ `MoveTo` + `_FlushPendingUI`。

**发现了但还没有解决的**

- 熔断期间本地结束组字时，服务端会话里的拼音是否残留：没有证据显示有，但实验有混淆因素，**没有排除**（见第 4 阶段的记录）。

**没有做的**：目标笔记本上的全部实测（新程序第一个字、连续快速输入、部署期间输入、睡眠唤醒后第一个字）；第 1 阶段要求的"系统性检查宿主进程里有没有加载显卡驱动 DLL"（只在记事本里观察到 `D3D10Warp.dll`）；ETW 自身的开销；x86 宿主里的实测（x86 只验证了编译通过）；第 5 阶段剩余项（交换链只放大不缩小、字体回退、唤醒预热）等笔记本数据出来再决定。

**本机（台式机）现状**

- 官方小狼毫 0.17.4 **已卸载**，装的是测试版，代码对应 `383d937`。要退回：卸载测试版、重装官方 0.17.4，词库不用动。
- 词库备份：`C:\Users\Administrator\RimeBackup_20261119\`（`Rime\` 目录 116 个文件，逐文件哈希一致；`HKCU_Rime.reg`、`HKLM_WOW6432Node_Rime.reg`）。真实用户目录 `%APPDATA%\Rime\build\` 已被新版 `rime.dll` 重新部署过，用户自己的配置和用户词库没有变。`HKCU\Software\Rime\Weasel\RimeUserDir` 已恢复为原值。
- 构建环境：VS 2022 Build Tools 17.14（含 ATL、MFC、ARM64）、NSIS、7-Zip、aria2、Boost 1.84（`deps/`，x86 和 x64）、预编译 librime `33e7814`、`plum` 子模块已初始化、`env.bat`（已被 `.gitignore`）。**本机有 CPU 过热保护断电的问题，构建必须限制并行度**，见 `AGENTS.md`。
- 临时产物已清理：旧行为的服务端副本、测试目录、ETW 会话。

**工具（`tools/perf/`）**：`summarize.py`（各阶段 p50/p95/max）、`by_pid.py`（首键与后续键按进程拆开）、`fi_suspend_server.ps1`（挂起服务端并抓 ETW）、`fi_block.py`、`fi_timeline.py`（故障注入时的阻塞时间和熔断时间线）、`ipc_timeout_test.cpp`（假服务端故障注入，8 组）、`ipc_busy_test.cpp`（管道被占满）、`ipc_stall_ab.py`（不读响应的客户端 A/B）、`memtest.cpp`（预热的内存代价）、`d2ddbg.cpp`（D2D 调试层 A/B）。ETW provider：`LiteWeasel-Perf`，GUID `{5974258d-005e-4490-adef-9e265b26abf5}`，抓取命令见下面"第 0 阶段：按键延迟计时"一条。

**建议的下一步**

1. **在目标笔记本上测一轮**（最大的缺口）：装测试版安装包，用 ETW 抓新程序第一个字、连续快速输入、部署期间输入、睡眠唤醒后第一个字。装之前必须先备份 `%APPDATA%\Rime`，且先让服务端正常退出（`WeaselServer.exe /q`），否则备份里的 leveldb 不完整。
2. 看数据再决定做什么：首键里的服务端耗时（要先隔离是哪份用户数据）、`start_composition` 里的约 6.5ms、每个按键先 `Echo` 一次、第 3 阶段。如果笔记本上已经"感觉不到"，就收手。
3. 熔断探针的小优化（`Echo` 失败后若熔断已打开就直接返回）改动很小，没有做。

**文档分工**：`AGENTS.md` 是规范、结构、已知踩坑；`README.md` 是写给普通用户的大白话；本文件是详细记录；`initial task.txt` 是顶层设计和各阶段的执行状态。

---

## 2026-11-19 · 第 3 阶段（进程/维护优先级）与第 5 阶段第 3 项（跳过每键 `Echo`）实施与自动验证

**状态：方案经用户确认，代码已实施，x64 和 Win32 均编译通过（0 错误、0 新增警告），全自动验证全部通过；未提交。**

### 1. 第 3 阶段：进程与维护线程调度优先级

- 改动文件：
  - `WeaselDeployer/WeaselDeployer.cpp:73-75`：`wmain` 入口设置 `BELOW_NORMAL_PRIORITY_CLASS`。
  - `WeaselServer/WeaselServerApp.cpp:45-51`：`m_handler->Initialize()` 完成后，若当前为 `NORMAL_PRIORITY_CLASS` 则升至 `ABOVE_NORMAL_PRIORITY_CLASS`（不覆写显式以 Idle 启动的测试进程）。
  - `RimeWithWeasel/RimeWithWeasel.cpp:114-143`：`start_maintenance(False)` + `join_maintenance_thread()` 期间用 RAII 守卫临时将进程优先级降至 `BELOW_NORMAL_PRIORITY_CLASS`（让 `rime.dll` 内部创建的维护线程自动继承低优先级），结束后自动恢复原优先级。
- 自动化验证（临时干净用户目录，测完还原 `RimeUserDir` 并清理临时目录，不碰 `%APPDATA%\Rime`）：
  - `WeaselServer.exe` 首次启动维护（约 10.8 秒）：全程 `BelowNormal`（0ms～10800ms），维护结束立刻升为 `AboveNormal`（11000ms）。
  - `WeaselDeployer.exe /deploy` 全量部署（约 10.3 秒）：从 `Normal`（0ms）切至 `BelowNormal`（100ms～10300ms，`exit=0`）；部署完成后 `WeaselServer.exe` 保持 `AboveNormal`。

### 2. 第 5 阶段第 3 项：连续按键跳过多余的 `Echo` 往返

- 改动文件：
  - `include/PipeChannel.h:29-39,169-171,208,271-277`：`ChannelContext` 新增 `reconnected` 标记和 `WasReconnected()`，在 `_Send()` 遇到断管并透明重连时置位。
  - `include/WeaselIPC.h:152-156`、`WeaselIPC/WeaselClientImpl.h:33-34,47-48`、`WeaselIPC/WeaselClientImpl.cpp:10-14,57,66-70,166-180,349-424,504-511`：新增 `IsRecentlyAlive(maxAgeMs = 1000)` 与 `LastRequestFailed()`；仅在未发生断管重连且事务成功时刷新 `last_verified_tick`；若底层透明重连（服务端可能重启导致旧会话失效）则清空活跃时间戳并标记 `last_ipc_failed = true`。
  - `WeaselTSF/WeaselTSF.h:136`、`WeaselTSF/WeaselTSF.cpp:249-259`：`_EnsureServerConnected(bool allowCached = false)` 在 `allowCached && m_client.IsRecentlyAlive(1000)` 时直接返回 `true`。
  - `WeaselTSF/KeyEventSink.cpp:25,52-61`：按键路径传 `allowCached = true`；若 `ProcessKeyEvent` 后出现 `LastRequestFailed() && !Suspended()`，立即走完整 `_EnsureServerConnected(false)` 重建会话并重试该键一次（正常“不吞键”时 `LastRequestFailed()` 为 `false`，不触发重试）。
- 自动化验证（`tools/perf/ipc_timeout_test.cpp` 扩展至 9 组 + `tools/perf/ipc_busy_test.cpp`）：
  - `ipc_busy_test`：全忙探针 **109.5 ms**，PASS（exit=0）。
  - `ipc_timeout_test`：9 组 32 项 **ALL PASSED（0 failures）**：
    - 连续 100 键发送 **0 次 `Echo`**，100 键总 IPC 耗时 **10.40 ms → 5.76 ms（-44.6%）**；
    - 正常不吞键：`LastRequestFailed() == false`，保持活跃；
    - 空闲超过 1000ms 自动失效，下一次 `Echo` 重新刷新；
    - 按键中途模拟服务端重启（强断管道 + 变更有效会话号）：客户端立即识别 `LastRequestFailed() == true`、重连建新会话并重试成功，**0 丢键**。

### 3. 编译结果（低优先级，`/p:CL_MPCount=2 /m:1`）

- x64 Release：通过，0 错误，0 新增警告。
- Win32 Release：通过，0 错误，0 新增警告。

---

## 2026-11-19 · 自动化排查与低风险提速（词库隔离、阴影基准、探针收紧、排版文字测量缓存）

**状态：代码改动已完成，x64 和 Win32 均编译通过（0 错误、0 新增警告），全自动测试全部通过；未提交，待用户确认后决定是否继续第 3 阶段和去 `Echo`。**

### 1. 词库与配置隔离基准（纯只读排查，不动真实 `%APPDATA%\Rime`）

- 新增工具：`tools/perf/rime_firstkey_bench.cpp`（直接链接 `lib64/rime.lib` + `output/rime.dll`，从只读备份 `C:\Users\Administrator\RimeBackup_20261119\Rime` 复制到临时目录跑 6 组隔离，测完删除临时目录）。
- 实测结果（台式机，Idle 优先级，每组开 3～6 个并存会话，打 `n i h a o <space>`，单位 ms，取稳态会话 `s1..s2`）：

| 对照组 | `n` | `i` | `h` | `a` | `o` | `SPC`（上屏） | 整词合计 |
|---|---|---|---|---|---|---|---|
| 干净标准词库（`case1_clean`） | 0.44 | 0.15 | 0.26 | 0.25 | 0.22 | 0.05 | 1.37 |
| 干净 + 30MB `luna_pinyin_simp.userdb`（`case2_userdb`） | 0.34 | 0.15 | 0.28 | 0.23 | 0.20 | 0.05 | 1.25 |
| 完整用户配置（`case3_full`） | 1.54 | 0.66 | **3.54** | 0.87 | 1.17 | 0.08 | **7.86** |
| **完整配置仅关 `enable_correction`（`case4_no_corr`）** | 0.94 | 0.26 | **0.57** | 0.29 | 0.30 | 0.05 | **2.41** |
| 完整配置仅去 `easy_en`（`case5_no_easyen`） | 1.52 | 0.61 | 3.98 | 0.86 | 1.27 | 0.07 | 8.31 |
| 完整配置两项都关（`case6_no_both`） | 0.90 | 0.21 | 0.52 | 0.25 | 0.27 | 0.05 | 2.20 |

- 结论：
  1. **更正旧记录**：此前 `A4_cold.etl` 因 ETW 不记键码，把 `3.9–4.7ms` 误认为"空格上屏写用户词库"；实测空格仅 `0.08ms`，该延迟实际是 `nih`（`h` 键）触发 `translator/enable_correction: true` 的相邻键纠错搜索。关掉此项单词服务端总耗时降 70%。
  2. 30MB 用户词库与 `easy_en` 均非主因。
  3. 仅当最后一个会话关闭后重建（或服务端首个会话 `s0`）时，才付 `6–8.9ms` 一次性冷加载。

### 2. 第 4 阶段收尾：探针失败短路与探针连接预算对齐

- 改动文件：
  - `WeaselTSF/WeaselTSF.cpp:255-260`：`Echo()` 失败后若 `m_client.Suspended()` 直接返回 `false`，不再继续调 `_Reconnect()` 多等一遍 250ms。
  - `include/PipeChannel.h:53-58,170-189`、`WeaselIPC/PipeChannel.cpp:27-46`：`_Ensure` / `_Connect` 接受 `connectBudgetMs`（默认 250ms），`Transact(msg, timeoutMs)` 将连接预算钳到 `min(timeoutMs, 250)` 并与读写共享总预算。
  - `tools/perf/ipc_busy_test.cpp:42-60`：增加"全管道实例繁忙时熔断探针必须遵守 ~100ms 预算"的自动断言。
- 自动化验证：
  - `ipc_busy_test`：全忙探针耗时 **107.7 ms**（旧版最坏 ~530ms），PASS（exit=0）。
  - `ipc_timeout_test`：8 组 24 项全部 **ALL PASSED（0 failures）**，探针超时 109.0ms。

### 3. 第 5 阶段：WARP 阴影基准 + 排版文字测量提速（`D2D::GetTextSize`）

- 新增基准工具：`tools/perf/ui_bench.cpp`（离屏 WARP 阴影与 `GetTextSize` 等价性验证）、`tools/perf/layout_bench.cpp`（直接链接真实 `WeaselUI.lib` 测 `HorizontalLayout::DoLayout()` 并输出几何校验和）。
- 阴影基准（台式机 WARP，560×110，100 帧）：`shadow_radius=0` **0.19ms** → `4` **0.92ms** → `8` **1.00ms** → `12` **1.09ms**（阴影模糊约占绘制耗时 80%）。
- 改动文件：`WeaselUI/d2d.h:16,169-192`、`WeaselUI/d2d.cpp:129,420,485-488,546,679-767`：
  - `D2D::GetTextSize` 改为复用同一个 `IDWriteTextLayout`（`SetMaxWidth`/`SetMaxHeight`），不再为查 `GetOverhangMetrics` 第二次 `CreateTextLayout`。
  - 增加每 `D2D` 最多 256 项的 `textSizeCache`（以格式指针、方向标志、最大宽高、字符串为键；在 DPI 变化、字体/样式重初始化、格式缓存淘汰、设备重置时自动清空）。
- 自动化 A/B 验证（真实 `WeaselUI.lib`，x64，台式机）：

| 场景 | 改前 p50 / p95 | 改后 p50 / p95 | 变化 | 几何校验和 |
|---|---|---|---|---|
| `fresh`（50 组，每组 7 个全新汉字候选） | 1943.9 / 2402.7 µs | **676.5 / 779.2 µs** | **-65.2%（快 2.87 倍）** | `171692900` == `171692900` |
| `typing`（300 次，`n→ni→nih→niha→nihao`） | 1713.3 / 1880.4 µs | **7.4 / 7.6 µs** | **-99.6%** | `877806480` == `877806480` |
| `GetTextSize` 复用等价性（66 组样本×3 宽度） | — | — | **0 像素偏差** | — |

### 4. 编译结果（低优先级，`/p:CL_MPCount=2 /m:1`）

- x64 Release：通过，0 错误，0 新增警告。
- Win32 Release：通过，0 错误，0 新增警告。

---

## 2026-11-19 · 第 4 阶段：IPC 防卡死 · 设计方案、实施与验证

**状态：用户已确认方案；第 1～3 步均已实施并提交（`d7281d8`、`9b29e23`、`383d937`），验证结果见下面各步骤的记录。** 下面第一部分是当时写的设计方案原文（"读代码得到的事实"和"方案"），其中的"待确认""尚未写代码"等措辞已不适用。

**读代码得到的事实（只读，没有改动）**

1. `WeaselIPC/PipeChannel.cpp:43-44` `_Connect` 在管道一直 BUSY 时 `while(...) WaitNamedPipe(name, 500)` 无限循环。
2. `PipeChannel.cpp:71-77` `_WritePipe` 同步 `WriteFile` 后调用 `FlushFileBuffers`，要等对端把数据读走，且无法设超时。`:88-102` `_Receive` 同步 `ReadFile`，无超时。`include/PipeChannel.h:120-125` `Transact` = `_Ensure` → `_Send`（失败重连一次）→ `_ReceiveResponse`。
3. **服务端所有请求都在全局互斥锁 `g_api_mutex` 下串行处理**（`WeaselIPCServer/WeaselServerImpl.cpp:191-194`），每个客户端一个线程（`:432`）。任何一个请求卡住，所有宿主进程的所有请求都排在后面。
4. 服务端响应时同样 `_Send` → `_WritePipe` → `FlushFileBuffers`（`:451`，在持有 `g_api_mutex` 的回调里）。**如果客户端发完请求后被挂起，服务端会卡在 Flush 里，并且持有全局锁**，进而拖住所有宿主。
5. 服务端长时间持锁的路径：`RimeWithWeasel.cpp:491-497` `EndMaintenance()` → `Initialize()` → `rime_api->join_maintenance_thread()`（`:120` 附近），部署没结束会一直阻塞；`AddSession`（`:170-173`）在 `m_disabled` 时也会调用 `EndMaintenance()`，即**新程序激活输入法时可能被前一次部署堵住**。这些是读代码得出的，**没有在本机复现过卡死**。
6. **每个按键都先做一次 `Echo`**：`KeyEventSink.cpp:20` → `WeaselTSF.cpp:249-250` `_EnsureServerConnected()` 里 `m_client.Echo()`，所以每个按键事件至少 2 次管道往返（`Echo` + `ProcessKeyEvent`）。`Echo` 失败还会 `_Reconnect()`（断开、连接、`StartSession`）并在 `retry >= 6` 时拉起 `start_service.bat`，全都在按键路径上。
7. `WeaselClientImpl.cpp:177` `GetResponseData` 解析的是线程缓冲区里现有的内容，不管上一次事务是否成功。**推测**：事务失败后可能重放旧的响应（比如重复上屏），**没有验证**。

**方案（只改客户端和管道层，不改线路格式）**

- **C1 连接限时**：`_Connect` 总时长封顶（建议 250ms），超过就失败返回，不再无限循环。
- **C2 重叠 I/O + 超时 + 取消**：客户端句柄用 `FILE_FLAG_OVERLAPPED`，读写用 `OVERLAPPED` + `WaitForSingleObject(超时)`，超时 `CancelIoEx` 后必须等取消完成再返回。**客户端必须去掉 `FlushFileBuffers`**（它无法设超时）。建议默认超时 250ms（本次实测服务端 p95 约 5ms，最大 97ms，冷启动首键约 9ms）。
- **C3 按消息区分策略**：只有按键路径和 `Echo` 用 250ms，`StartSession` 用 1000ms；`StartMaintenance`、`EndMaintenance`、`ShutdownServer`、`TrayCommand` 保持不限时（部署器的 `EndMaintenance` 本来就可能很久）。
- **C4 超时后的协议恢复**：超时就关闭这根管道（迟到的响应被系统丢弃）、把线程缓冲区标记为无效（`GetResponseData` 返回 false）、返回失败；下一次调用自动重连，服务端的会话 ID 与管道无关，用 `Echo` 确认会话还在，不在才走原来的 `_Reconnect()`。
- **C5 熔断**（进程内共享，放在 `ClientImpl::_SendMessage`）：连续 2 次超时（2 秒内）→ 打开，打开期间所有 IPC 立即返回失败（一次原子读）；3 秒后半开，只允许一个探测请求（超时 100ms），成功则关闭，失败则退避 3s→6s→12s→最长 30s。`Client` 增加一个 `Suspended()` 查询，`_EnsureServerConnected` 在熔断期间直接返回 false，**避免每个按键都 `_Reconnect` 和反复拉起 `start_service.bat`**。熔断期间按键不被吞（`*pfEaten = FALSE`），正在进行的组字由 TSF 本地结束（复用 `_AbortComposition()`，其中的 `ClearComposition` 会立即失败）。
- **C6 服务端去掉响应时的 `FlushFileBuffers`**：服务端管道缓冲 64KB，小消息 `WriteFile` 不会阻塞，Flush 是唯一可能被挂起的客户端卡住的地方。不改线路格式。

**影响面（`AGENTS.md` 第 4 节第 2 条）**

- COM 接口、ABI：不变。只新增 `Client` 的内部查询方法（`WeaselIPC` 是静态库）。
- IPC 消息格式：**不变**。新客户端配旧服务端、旧客户端配新服务端都能用（C6 只是少调一次 Flush）。
- 高 DPI 逻辑：不涉及。
- 宿主进程内约束（第 4 节第 6 条）：不新增线程，只用 Win32 事件；不抛未捕获异常；阻塞上限从"无限"降到约 250ms。
- 受影响的 TSF 回调：`OnTestKeyDown`/`OnKeyDown`/`OnTestKeyUp`/`OnKeyUp`（失败时 `pfEaten = FALSE`，按键交给应用）；`DoEditSession`（缓冲区无效时 `ok = false`，跳过组字更新）；`OnSetThreadFocus`、`FocusIn/Out`、`UpdateInputPosition`（熔断时立即返回）；候选窗鼠标回调（`CandidateList.cpp:405-450`，立即返回 false）。

**风险**

- **误判**：服务端合法地慢过 250ms（部署、杀毒、换页）会触发熔断，期间用户会打出原始字母。缓解：要求连续 2 次超时，半开探测恢复，阈值可调。
- 重叠 I/O 在消息模式下的 `ERROR_MORE_DATA` 路径要小心处理；`CancelIoEx` 后必须等取消完成才能释放 `OVERLAPPED` 和事件。
- 熔断期间候选窗内容可能陈旧一小段时间，需要本地收尾。

**分三步实施，每步单独编译、测试、提交**

1. 低风险：C1 连接限时、C6 去掉服务端 Flush、C4 的"缓冲区无效标记"。
2. C2 + C3：重叠 I/O 超时、按消息策略、去掉客户端 Flush。
3. C5：熔断和 TSF 短路。

**验证方法**

- 故障注入：打字时用脚本把服务端进程挂起 5 秒再恢复。改之前应观察到宿主 UI 线程被卡约 5 秒（用 ETW 的 `tsf.ipc_key_roundtrip` 最大值和记事本是否有响应来测）；改之后最多卡约 250ms 一次，随后每个按键约 1ms 以内放行，服务端恢复后约 3 秒内自动恢复。
- 另外：服务端进程被杀、管道中途断开，确认不会重复上屏、不会残留组字。
- 正常打字不退化：`tsf.ipc_key_roundtrip` 的 p50/p95 与 M4/A5 对比。
- x64、x86 都要编译通过，不新增警告。

**顺带记录（不属于本阶段）**：每个按键先 `Echo` 一次（见事实 6）是第 5 阶段第 3 项的实证，可以改成"距离上次成功事务超过约 1 秒才 `Echo`"，能把管道往返减半，但会改变 TSF 的会话校验时机，要单独出方案。

**第 1 步已实施（用户已确认方案，三步实施中的第一步）**

改动（分支 `perf/phase0-timing`）：

- `WeaselIPC/PipeChannel.cpp`：`_Connect` 总时长封顶 250ms（`GetTickCount64` 计时，`WaitNamedPipe` 只等剩余时间），超过返回 `INVALID_HANDLE_VALUE`；`_WritePipe` 去掉 `FlushFileBuffers`（客户端和服务端共用这个函数，所以两端同时去掉）。
- `include/PipeChannel.h`：`ChannelContext` 新增 `response_valid`，`Transact` 开始时置 false、完整收到响应后置 true；`HandleResponseData` 在无效时返回 false；`Transact` 里 `_Ensure()` 失败直接抛 `ERROR_PIPE_NOT_CONNECTED`，不再对无效句柄写入并再重连一次。
- 新增 `tools/perf/ipc_busy_test.cpp`：用同名管道占满唯一实例，让客户端得到 `ERROR_PIPE_BUSY`，量 `Connect`/`StartSession` 的耗时。

验证：

1. **连接限时**：管道被占满时 `Client::Connect` 254.6–261.5ms 返回失败；走 `Transact` 的 `StartSession` 251.0ms 返回（只等一次预算，不是两次）。没有跑"改之前"的对照，原来的 `while(...) WaitNamedPipe(name, 500)` 是无上限循环，是读代码的结论。
2. **正常打字无退化**（`P4S1_multi.etl` 对比 `M4_multi.etl`，同一个记事本内连续打词，M4 是 8 个词，这次 5 个词）：

| 阶段 | M4 p50 / p95 (µs) | 第 1 步 p50 / p95 (µs) |
|---|---|---|
| `tsf.ipc_key_roundtrip` | 780 / 5229 | 761 / 5215 |
| `srv.on_key_event` | 650 / 4988 | 655 / 5108 |
| `tsf.get_response` | 422 / 528 | 416 / 472 |
| `tsf.do_edit_session` | 1537 / 10131 | 1420 / 9902 |
| `ui.do_paint` | 1736 / 2162 | 1738 / 2423 |

去掉 `FlushFileBuffers` 后往返耗时没有可测的变化（780 → 761µs，在噪声内）。`tsf.get_response`、`ui.update`、`ui.do_paint` 都有正常的事件数，说明新增的 `response_valid` 没有影响正常的组字流程。

3. 编译：x64、x86 均 0 错误；警告都是 `PipeChannel.cpp`、`WeaselClientImpl.cpp`、`WeaselServerImpl.cpp` 里原有的 `size_t`/`LRESULT` 转换，行号因代码增加后移，没有新增。

**还没验证的（第 1 步）**

1. ~~服务端 `_Send` 去掉 Flush 后，"客户端发完请求被挂起"这条路径是否真的不再卡住服务端，没有做故障注入。~~ **已验证**（见下面"服务端去掉 `FlushFileBuffers` 的 A/B 验证"）：旧行为下另一个客户端的 `Echo` 被卡 5500ms，当前 0.4ms。
2. ~~`response_valid` 在"服务端无响应"时是否真的让 `DoEditSession` 跳过组字更新。~~ **部分验证**：第 2 步的假服务端测试里，超时后 `GetResponseData` 被拒绝（返回 false）；`DoEditSession` 在真实宿主里据此跳过组字更新这一点没有单独观察。

**第 2 步已实施：客户端重叠 I/O 超时（`d7281d8` 之后，分支 `perf/phase0-timing`）**

改动：

- `WeaselIPC/PipeChannel.cpp`、`include/PipeChannel.h`：客户端句柄用 `FILE_FLAG_OVERLAPPED` 打开；新增 `_WaitOverlapped`、`_WritePipeTimed`、`_ReceiveTimed`（读写都带超时，超时后 `CancelIoEx`，并用 `GetOverlappedResult(…, TRUE)` 等取消完成才返回，因为 `OVERLAPPED` 在调用者栈上；取消的瞬间 I/O 已完成就按成功处理）。`ChannelContext` 增加每线程的 `io_event`。服务端仍用原来的同步 `_WritePipe`/`_Receive`（`kSyncIo = 0`），没有改。
- `Transact(msg, timeoutMs)`：超时预算覆盖写请求加读响应；超时就关闭这根管道、清掉已暂存但没发出的请求体、抛 `ERROR_TIMEOUT`。写超时不再重试。
- **顺带修了上游一个旧 bug**：`_Send` 在写失败后的重连重试里写的是 `_WritePipe(_GetPipeHandle(), …)`，传的是句柄变量的地址（`HANDLE*` 隐式转 `void*`），不是句柄，所以"重连后重试一次"这条路径从来没有成功过。现在传 `*_GetPipeHandle()`。
- `WeaselIPC/WeaselClientImpl.cpp`：`TimeoutFor()` 按消息类型选超时：`START_MAINTENANCE`、`END_MAINTENANCE`、`SHUTDOWN_SERVER`、`TRAY_COMMAND` 不限时；`START_SESSION` 1000ms；其余 250ms。`Echo()` 的守卫从 `_Active()`（要求管道已连接）改成只要求 `session_id != 0`，因为超时后管道已关闭，`Echo` 正是 TSF 用来重连并确认会话还在的探测。新增 `ipc.echo`、`ipc.key`、`ipc.start_session`、`ipc.other` 计时点。
- `WeaselTSF/KeyEventSink.cpp`：新增 `tsf.process_key`（`_ProcessKeyEvent` 整体）和 `tsf.ensure_server`（其中的 `_EnsureServerConnected`）计时点。
- 新增 `tools/perf/ipc_timeout_test.cpp`（假服务端故障注入）、`fi_suspend_server.ps1`（用 `NtSuspendProcess` 挂起真实服务端）、`fi_block.py`；更新 `ipc_busy_test.cpp`（`WeaselIPC.lib` 现在引用 provider，测试程序要自己定义一个）。

验证：

1. **假服务端故障注入，6 组全部通过**：按键请求被拖 1500ms，客户端 264.3ms 放弃；放弃后 `GetResponseData` 被拒绝；紧接着 `Echo` 0.4ms 重连成功且会话号不变；迟到响应不串线（下一次按键收到的是它自己的 `commit=k3`，不是迟到的 `k2`）；`StartMaintenance` 被拖 700ms，等满 714.2ms 不被打断；`Echo` 被拖 1000ms，261.7ms 放弃并在之后恢复。第一次跑发现 `Echo` 超时后不重连（守卫问题，已改），第二次跑发现测试自己的 `StartMaintenance` 会清空会话号（测试问题，已改）。
2. **真实服务端挂起 4.02 秒**（`NtSuspendProcess`，用户在记事本里连续打 `nihao`+空格）：
   - 基线（第 1 步版本，无超时）：按键往返之间出现一个 4383ms 的空档，宿主线程被卡住。
   - 第 2 步版本（`FI_after2b.etl`）：`tsf.process_key` 里有 5 次被卡：789、524、515、503、532ms，**合计约 2.9 秒阻塞宿主线程**（挂起窗口 4.02 秒）；`ipc.echo` 超时约 265ms，`ipc.start_session` 约 252ms（连接预算），`ipc.key` 本身没有一次超过 4.4ms；其余按键 `tsf.process_key` p50 0.95ms。
   - 度量口径说明：第一次"改之后"抓取（`FI_after2.etl`）里的空档无法区分"宿主被卡"和"按键被快速放行"，因为超时后的按键在调用 `ProcessKeyEvent` 之前就返回，不产生 `tsf.ipc_key_roundtrip` 事件，所以后来补了 `tsf.process_key`，**基线和第 2 步版本不是同一个口径，不能算出改善百分比**。
3. 编译：x64、x86 均 0 错误；`PipeChannel.cpp`、`WeaselClientImpl.cpp` 里是原有的 5 条和 4 条警告，没有新增。

结论：**第 2 步让每次阻塞都有上限**（单个按键最多约 0.8 秒），服务端永久卡死时宿主不再被无限期卡住。但是**挂起期间每个按键仍各自等一遍超时**，4 秒内有 5 个按键各卡半秒，合计 2.9 秒，和基线的 4.4 秒相比改善有限。这正是第 3 步熔断要解决的，所以**第 3 步是第 2 步价值兑现的前提，不是可选项**。

**还没验证的（第 2 步）**

1. 挂起恢复后是否出现重复上屏、残留拼音或候选框、错字：ETW 看不出来。用户在第 3 步及之后的实验里反馈"恢复后能正常打字"，组字中途那两次实验里最后打 `nihao`+空格上屏"你好"；**没有人报告重复上屏或残留拼音，但没报告不等于确认没有**（见"组字中途服务端被挂起"一节的混淆因素）；第 2 步版本那次实验时没有拿到反馈。
2. ~~服务端响应时去掉 `FlushFileBuffers` 后，"客户端发完请求被挂起"这条路径不再卡住服务端。~~ **已验证**（A/B，见下）。
3. `ipc.start_session` 在服务端卡住时只有约 252ms，是因为重连时连接预算（250ms）先耗尽，并没有走到它自己的 1000ms 超时；"服务端慢但能连上"的 `StartSession` 超时路径没有被真实环境覆盖（假服务端测试只测了 `ProcessKeyEvent`、`Echo`、`StartMaintenance`）。

**第 3 步已实施：熔断（`9b29e23` 之后，分支 `perf/phase0-timing`）**

改动：

- `WeaselIPC/WeaselClientImpl.cpp`：进程内共享的 `CircuitBreaker`（原子状态，不加锁）。2 秒内连续 2 次"无响应"（读写超时，或连接时所有管道实例一直忙到连接预算用尽）则打开；打开期间所有**有限超时**的请求立即返回失败；3 秒后放行一个探针（超时 100ms），探针成功关闭，失败则打开时长翻倍（3→6→12→最长 30 秒）。**不限时的消息（`StartMaintenance`、`EndMaintenance`、`ShutdownServer`、`TrayCommand`）不受保护。** 服务端不存在、管道断开等"不是挂起"的错误不会触发熔断，且会让熔断复位，这样 TSF 原有的拉起 `start_service.bat` 逻辑不受影响。熔断状态变化写 ETW 事件 `ipc.breaker.open`/`probe_failed`/`close`（值为毫秒数乘 1000，沿用 `us` 字段）。新增 `Client::Suspended()`（`include/WeaselIPC.h`）。
- `include/PipeChannel.h`、`WeaselIPC/PipeChannel.cpp`：`ChannelContext::connect_timed_out`，让 `Transact` 区分"连接预算用尽（`ERROR_TIMEOUT`）"和"管道不存在（`ERROR_PIPE_NOT_CONNECTED`）"；`_Ensure` 失败的检查移进了 `try`，失败时也会清掉暂存的请求体。
- `WeaselTSF/WeaselTSF.cpp`：`_EnsureServerConnected()` 在熔断期间直接返回 false，不再重连、探测、拉起服务。
- `WeaselTSF/KeyEventSink.cpp`：服务端不可用且熔断打开、并且还有未结束的组字时，调用 `_AbortComposition()` 本地结束它，避免残留候选窗。
- `include/WeaselPerf.h`：新增 `EmitValue()`。
- `tools/perf/ipc_timeout_test.cpp` 新增第 7、8 组（熔断）；新增 `tools/perf/fi_timeline.py`。

验证：

1. **假服务端故障注入全部通过**（8 组 31 项）。熔断部分：两次超时（264.4ms、264.6ms）后熔断打开，之后的 `Echo` 和 `ProcessKeyEvent` 0.0ms 立即被拒；3 秒后探针用约 100ms 超时（108.8ms）失败并重新打开，退避翻倍；服务端恢复后探针 0.4ms 成功，熔断关闭；`StartMaintenance` 在熔断打开时仍然通过并等满 407.7ms。
2. **真实服务端挂起 4.02 秒**（`FI_step3.etl`，用户在记事本里连续打 `nihao`+空格，服务端挂起开始于抓取后 6.02 秒、恢复于 10.04 秒）：

| 版本 | 被卡超过 20ms 的按键数 | 这些按键合计阻塞宿主线程 | 单个按键最长 |
|---|---|---|---|
| 第 1 步（无超时，基线） | 1 次连续卡住 | 约 4.4 秒（按键之间的空档，口径不同） | — |
| 第 2 步（只有超时） | 5 | 2863 ms | 789 ms |
| **第 3 步（超时 + 熔断）** | **2** | **755 ms** | **521 ms** |

时间线（抓取开始后的秒数）：6.59 秒熔断打开（`ipc.breaker.open` 3000ms），在此之前只有一个按键被卡 521 ms（它自己的 `Echo` 先超时 257ms，`_Reconnect` 后再 `Echo` 又超时 264ms，一次按键里连续两次无响应就把熔断打开了）；之后到约 9.8 秒的开放期内没有任何按键被卡；约 9.8 秒的探针花了 234 ms，几乎都是连接阶段在等（服务端还在挂起，所有管道实例都忙，直到 10.04 秒恢复），随后 10.03 秒 `ipc.breaker.close`，之后的 135 次 `ipc.key` 正常（p50 0.80ms）。

3. 编译：x64 33 条、x86 20 条警告，**全是原有警告**（`WeaselPanel.cpp`、`d2d.cpp` 的类型转换，因为改了 `WeaselIPC.h` 导致更多文件重编而显现；`PipeChannel.cpp` 5 条、`WeaselClientImpl.cpp` 4 条与改动前相同）；0 错误。

**注意和局限**

- **探针的连接阶段不受 100ms 探针超时限制**：连接预算是单独的 250ms，所以一次探针最坏约 350ms（本次实测 234ms）。它每个开放期只发生一次。如果要收紧，可以让探针也缩短连接预算，目前没有做。
- 度量口径：基线是"按键之间的空档"，第 2、3 步是逐按键的阻塞时间，**基线与后两者不是同一口径**，表中只对第 2 步和第 3 步做同口径比较。
- 只测了服务端被挂起 4 秒这一种故障。服务端被杀、管道中途断开、服务端在 `EndMaintenance` 里长时间持锁（读代码得出的、没有复现过的路径）都没有做真实注入。

**还没验证的（第 3 步）**

1. **挂起期间用户实际看到什么**：`FI_step3` 那次实验之后用户的口头反馈：服务端挂起期间"输入法突然好像没了"，`nihao` 被当作英文字母直接打进了记事本；恢复后能正常打字。这与设计一致（熔断期间按键放行给应用）。
2. **组字中途服务端被挂起时，服务端会话里是否残留旧拼音**：见下面"组字中途服务端被挂起"一节。**没有证据显示有残留，但实验有混淆因素，没有排除。**
3. ~~服务端响应时去掉 `FlushFileBuffers` 后不再卡住服务端。~~ **已验证**，见再下面的 A/B 一节。
4. 目标笔记本上的行为没测。
5. 只测了"服务端进程被挂起"这一种故障。服务端被杀、管道中途断开、服务端在 `EndMaintenance` 里长时间持锁（读代码得出、没有复现过的路径）都没有做真实注入。

**组字中途服务端被挂起（两次实验：`FI_midcomp.etl`、`FI_midcomp2.etl`，台式机）**

担心的点（读代码得出，**未证实**）：熔断打开后 TSF 本地调用 `_AbortComposition()`，其中的 `m_client.ClearComposition()` 会被熔断直接拒绝，服务端会话里的拼音（如 `nih`）没有被清掉，恢复后可能污染下一次输入。

- **第一次（挂起 5 秒）没有触发熔断**：用户只在挂起快结束时按了一个键，它被卡 189ms（服务端在等待期间恢复，请求正常完成），所以这条路径没有被走到。用户观察到候选框短暂消失一次，原因没查清：ETW 显示 14.03 秒（`nih` 之后）到 19.86 秒之间没有任何 UI 事件，也没有候选窗销毁重建（没有 `ui.create`、`tsf.start_ui`）；一个**未验证**的可能是 `nih` 后面多按的 `a` 让 `niha` 没有候选。
- **第二次（挂起 8 秒，用户在挂起开始时就连续敲字母）触发了熔断**：挂起开始时 `ipc.breaker.open`（一个按键被卡 530ms，里面含两次无响应：`Echo` 超时后 `_Reconnect` 又耗尽连接预算）；约 3 秒后探针失败（`probe_failed`，又一个按键被卡 530ms，开放期退避翻倍到 6 秒）；其余约 5 秒没有按键被卡；服务端恢复后的第一个键 `ipc.breaker.close`，之后 47 次按键往返正常。**宿主线程合计被卡 2 次、约 1.06 秒**。恢复后的第一个键开始了**新的** `tsf.start_composition`，说明 TSF 侧的旧组字已被结束。用户观察：候选框在连续敲字母时消失（与熔断期间一致）；最后清空记事本、打 `nihao`+空格，上屏"你好"。
- **结论与局限**：没有发现旧拼音污染输入，但**不能据此断言不存在残留**：用户"清空记事本"的按键（`Backspace`/`Delete`/`Esc`）会经过输入法，可能把残留清掉；恢复之后到清空之前用户还在敲字母，"候选框消失"既可能是熔断，也可能是拼音没有候选。**干净对照的设计（没有做）**：挂起期间只按一个键，恢复后不做任何清理按键，直接打 `nihao`+空格；若出现 `nih` 开头的错乱，就说明需要在熔断恢复时补发 `ClearComposition`。没有做的原因：发生概率低、影响小（最坏是恢复后第一个词前面多几个字母），不值得继续花时间。
- **探针那次按键被卡 530ms，比预期的约 350ms 多**：探针失败后 `_EnsureServerConnected` 还接着走 `_Reconnect()`，又等了一遍 250ms 的连接预算。小优化（**没有做**）：`Echo` 失败后若 `m_client.Suspended()` 就直接返回 false。

**服务端去掉 `FlushFileBuffers` 的 A/B 验证（`tools/perf/ipc_stall_ab.py`）**

要验证的事：读代码的推断是，旧代码里服务端回响应时 `FlushFileBuffers`（`_WritePipe`）会一直等到客户端读走数据，而且它在持有全局锁 `g_api_mutex` 的回调里（`WeaselServerImpl.cpp:191-194`），所以一个发完请求就不再读响应的客户端（例如被挂起的宿主进程）会拖住所有其他客户端。

方法：临时把 `::FlushFileBuffers(pipe);` 加回 `PipeChannel.cpp:_WritePipe`（**不提交**），编出"旧行为"的 `WeaselServer.exe`，另存为 `output/WeaselServer_flush.exe`，然后 `git checkout` 还原源码（确认工作区干净）。测试程序：客户端 A 开一个会话、发一个按键请求、**故意不读响应**并保持连接 6 秒后关闭；客户端 B 在 0.5 秒后对同一个服务端发 `Echo` 并计时。每次只有一个服务端进程在运行（旧版副本进程名是 `WeaselServer_flush`，按进程名列出确认）。

| 服务端 | B 的 `Echo` 耗时 | 轮数 |
|---|---|---|
| 旧行为（响应时 `FlushFileBuffers`） | **5500.2、5501.0 ms**（卡到 A 关闭为止；另有 2 轮 5499.6、5500.4 ms） | 4 |
| 当前（已去掉） | **0.4 ms** | 4 |

结论：旧行为下，**一个不读响应的客户端会让服务端的所有其他请求排在全局锁后面**，卡多久取决于它何时恢复或退出；当前版本不再有这个问题。这是 `FlushFileBuffers` 在服务端回包路径上的真正危害，它不是慢，而是会被别的进程无限期拖住。

局限：测试程序是自己写的原始管道客户端，不是真实的被挂起的宿主进程；只在台式机上测过；没有在 x86 服务端上测过（x86 的 `WeaselServer.exe` 与 x64 同源，这里没有单独验证）。

**中途的一个小失误（已纠正）**：第一轮 A/B 时我按进程名 `WeaselServer` 去停服务端，漏掉了进程名为 `WeaselServer_flush` 的旧版副本，所以第一次的"当前"对照不能确认当时是哪一个服务端在应答；随后用按模式匹配的方式重做，每一轮都列出了唯一在运行的服务端，表中的数字来自重做后的结果（以及与第一次一致的数字）。

---

## 2026-11-19 · 第 0 阶段：按键延迟计时（ETW，T1/T3/T5/T7）

分支：`perf/phase0-timing`（从 master `f96ed24` 切出）。本条对应的改动已提交为 `199bfae`；后续的 T6/T8（`23589cc`）、预热（`92b79df`）、D2D 调试层（`7a5b370`）、服务端计时（`e458984`）见下面各条记录。

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

**宿主进程（T1、T5、T7）验证：已通过**

用户决定直接在本机（台式机）测，不再倒腾到笔记本。操作：备份词库 → 卸载官方 0.17.4 → 静默安装我们的构建（`weasel-0.17.4.0.199bfae-installer.exe`）。

- 备份：`C:\Users\Administrator\RimeBackup_20261119\Rime`（116 个文件，逐文件 MD5 与 `%APPDATA%\Rime` 完全一致）+ `HKCU_Rime.reg`、`HKLM_WOW6432Node_Rime.reg`。第一次备份有 4 个 leveldb 文件复制失败，原因是官方 `WeaselServer` 自己重新起来占着文件，先用 `WeaselServer.exe /q` 退出再 `robocopy /MIR` 才完整。**备份词库前必须先确认没有 WeaselServer 在运行。**
- 卸载程序只删安装目录和 `HKLM` 项，不动 `%APPDATA%\Rime` 和 `HKCU`。卸载后 Rime 目录仍是 116 个文件。
- **踩的坑**：替换系统里的 `weasel.dll` 后，**已经运行的进程仍然在用内存里的旧 DLL**（explorer、chrome、旧记事本等）。旧 DLL 照样能连上我们的服务端，所以服务端有事件、宿主侧却是 0 条，容易误判成计时代码有问题。必须**新开进程**（或注销）才会加载新 DLL。从这里（Claude 的工具进程）启动的 `notepad` 没有出现在用户桌面上，既没窗口也没加载 `MSCTF.dll`，要让用户自己新开。
- 第 3 次抓取（新开的记事本 PID 25268，14:55 启动，模块里有 `weasel.dll`）抓到全部 7 个阶段。

| 阶段 | 次数 | p50 (µs) | p95 (µs) | max (µs) |
|---|---|---|---|---|
| `tsf.ipc_key_roundtrip`（T1） | 418 | 745 | 5746 | 97418 |
| `srv.on_key_event`（T3） | 421 | 624 | 5616 | 97299 |
| `tsf.edit_session_wait`（T5） | 418 | 12 | 18 | 128 |
| `tsf.get_response`（T5） | 418 | 409 | 461 | 622 |
| `tsf.do_edit_session`（T5，含嵌套的 UI 刷新） | 418 | 1510 | 10802 | 38213 |
| `ui.do_paint`（T7） | 162 | 3311 | 3806 | 14690 |
| `ui.present`（T7，已含在 `do_paint` 内） | 162 | 190 | 274 | 419 |

**这是台式机（24 逻辑核心）、单次、单人打字的数据，不是基准，不能代表目标笔记本。** 事件数 418 包含按下、松开和测试按键。阶段之间有嵌套（`do_edit_session` 包含 `get_response` 和 UI 刷新，`do_paint` 包含 `present`），不能直接相加。

**初步观察（只针对这一组台式机数据，要在笔记本上复核）**

1. 一次 IPC 往返 p50 745µs，服务端处理 p50 624µs，所以管道本身的开销约 120µs，**耗时主要在服务端（librime）**，不在管道。
2. `ui.present` p50 只有 190µs，**"`Present(1, 0)` 被垂直同步阻塞"的推测在这台机器上不成立**。`do_paint` 的 3.3ms 里绝大部分是 `Present` 之外的绘制（D2D 经 WARP 软件渲染），这是第 5 阶段第 1 项（阴影、模糊）要看的东西，笔记本上可能更大。
3. `tsf.edit_session_wait` p50 只有 12µs，这个宿主（记事本）里 `RequestEditSession` 基本是同步回调。别的宿主可能不同。
4. p95 和 max 远大于 p50（服务端 p95 5.6ms、max 97ms），是偶发尖峰，原因没查。可能是首个按键、userdb 写入或其他进程抢 CPU，需要在更干净的条件下重复测。

**场景 A：新程序里的第一个字（台式机，5 个全新记事本进程，各打 `nihao`+空格）**

数据文件 `A_cold.etl`；分析用 `tools/perf/by_pid.py`（按进程拆开，对比"第一个按键"和"后续按键中位数"）。5 个进程的结果几乎一致：

| 阶段 | 第一个按键（5 个进程的范围） | 后续按键中位数 |
|---|---|---|
| `tsf.ipc_key_roundtrip` | 2460–2575 µs | 705–741 µs |
| `tsf.get_response` | 449–467 µs | 408–431 µs |
| `tsf.do_edit_session` | **34627–37147 µs** | 1342–1796 µs |
| `ui.do_paint` | 6487–7421 µs | 2994–3474 µs |
| `ui.present` | 259–284 µs | 189–206 µs |

要点：

1. **第一个按键的 `do_edit_session` 要 35–37ms，后续只要约 1.4ms，相差 25 倍**。这是首字延迟的主要来源。
2. 这 35ms 里已知的只有 `get_response`（约 0.46ms）和首次 `do_paint`（约 6.5ms，嵌套在里面），**剩下约 28ms 没有被现有计时点覆盖**。可能是候选窗创建、D3D/WARP 设备和 DirectWrite 初始化、TSF 组字的建立，但**这只是推测，没有数据支撑**，需要补 T6（`UI::Update`/`Refresh` 分段）、T8（`StartUI` 预热）才能拆开。
3. 第一次 IPC 往返是 2.5ms（后续 0.72ms），多出的约 1.8ms 是首次管道使用的开销，相对小。
4. 台式机上第一个按键到候选框总共约 38ms。笔记本更慢，具体多少没有测，**不能用台式机的数字推算**。
5. 注意：`StartUI` 里预热（`Refresh()`+`Hide()`）是有意保留的。这个数据显示预热之后第一个按键仍然很慢，要查预热有没有覆盖到全部资源，**但不能因此去掉预热**（`AGENTS.md` 的硬约束）。

**场景 A 续：补 T6、T8 后拆开首字的 35ms（台式机，5 个新记事本进程，`A2_cold.etl`）**

新增计时点（只加计时，无行为改动）：`tsf.start_ui`（及 `.begin_element`、`.make_window`、`.warmup`，`CandidateList.cpp:286`）、`tsf.start_composition`（`Composition.cpp:63`）、`tsf.update_ui`（`CandidateList.cpp:369`）、`ui.update`（`WeaselUI.cpp:55`）、`ui.create`（及 `.window/.d2d_ctor/.attach/.dwrite_init`）、`ui.refresh`（及 `.d2d_attach/.dwrite_init/.layout/.resize/.reposition/.redraw`）、`ui.device_init`（及 `.d3d11/.dxgi_d2d_dcomp/.dwrite_wic`，`d2d.cpp:32`）。`WeaselPerf.h` 新增 `Mark()`/`Lap()` 辅助。分析脚本 `tools/perf/by_pid.py`，另有一份按进程列出"第一个按键"各阶段的临时脚本（未入库）。

第一个按键里各阶段耗时，5 个进程的中位数，单位 ms（括号内为 5 个进程的范围）：

| 阶段 | 耗时 | 说明 |
|---|---|---|
| `tsf.do_edit_session` | ≈ 36.5（35.2–66.3） | 第一个按键的编辑会话，后续按键中位数 1.5 |
| └ `tsf.start_composition` | ≈ 35.2（33.9–38.4） | 几乎占满上面的 36.5 |
| &nbsp;&nbsp;└ `tsf.start_ui` | ≈ 27.7（26.9–31.4） | |
| &nbsp;&nbsp;&nbsp;&nbsp;├ `begin_element` | ≈ 0.08 | 可忽略 |
| &nbsp;&nbsp;&nbsp;&nbsp;├ `make_window` | ≈ 24.6（24.1–26.9） | |
| &nbsp;&nbsp;&nbsp;&nbsp;│ ├ `ui.device_init` | ≈ 17.2（16.8–18.5） | 进程内只做一次，D3D11 WARP 设备 ≈ 10.9，DXGI/D2D/DComp ≈ 3.2，DirectWrite/WIC ≈ 3.0 |
| &nbsp;&nbsp;&nbsp;&nbsp;│ └ `ui.create` | ≈ 7.4（7.1–8.4） | 建窗口 ≈ 1.2，`D2D::AttachWindow`（交换链、DComp）≈ 3.1，DirectWrite 字体格式 ≈ 2.9 |
| &nbsp;&nbsp;&nbsp;&nbsp;└ `warmup`（`Refresh`+`Hide`）| ≈ 3.0（2.7–4.4） | 其中布局 ≈ 1.5，调整窗口 ≈ 1.2 |
| &nbsp;&nbsp;`start_composition` 里 `start_ui` 之外 | ≈ 7.5 | **没有被覆盖**，包括创建编辑会话对象和 `RequestEditSession` 里宿主的同步工作 |

之后在别的回调里还有：首次 `tsf.update_ui` ≈ 6.1（后续 0.2），首次 `ui.do_paint` ≈ 6.6（后续 3.1）。台式机上从第一个按键到候选框完全画出来，粗算约 2.4 + 36 + 6 + 6.6 ≈ 50ms（各段在不同回调里串行，未精确对齐，仅作量级参考）。

**关键发现**

1. **`StartUI`（含"预热"）是从 `_StartComposition`（`Composition.cpp:68`）调用的，即第一次组字时才执行，而不是"获得焦点就预热"。** 这和 `AGENTS.md` 第 2 节、`initial task.txt` 里"获得焦点就预热"的说法**不一致，以代码为准**。数据也支持：预热就发生在第一个按键里。
2. 35ms 里最大的一块是 **`ui.device_init` ≈ 17ms，而它与具体窗口无关**（`DeviceResources` 是进程内单例）。其中仅 `D3D11CreateDevice(WARP)` 就 ≈ 11ms。
3. `ui.create` 里 `D2D` 构造为 0：`m_pD2D` 在 `Create` 之前已经建好，设备初始化在更早的 `D2D` 构造里发生，但仍然在 `make_window` 之内。
4. `ui.refresh.redraw` 只有约 0.03ms，`RedrawWindow` 不会同步绘制，真正的绘制在之后的 `WM_PAINT` 里（`ui.do_paint`）。
5. 异常值：进程 2 的 `do_edit_session` 达到 66ms（其余 35–37ms），进程 1、2 的首次 IPC 往返是 8.7–10.5ms（其余 2.4ms），原因没查，样本太小。
6. 现有"预热"是有意保留的（`AGENTS.md` 硬约束），但它**没有起到提前准备的作用**，因为它发生在第一个按键里。这是一个优化机会：把与窗口无关的 `ui.device_init`（约 17ms）提前到进程获得焦点或 TSF 激活时，**要先出方案，不直接改**：它涉及在宿主进程里什么时候、哪个线程创建 D3D 设备，属于 `AGENTS.md` 要求先出方案的高风险改动。

**同一个进程里连续 6 次组字（台式机，一个记事本，`M3_multi.etl`）**

前两次尝试数据无效（`M_multi`：用户开了 5 个记事本各打一个词，每个进程只有 1 次组字；`M2_multi`：抓取为空，连服务端事件都没有，原因不明）。以下是第三次，一个进程内 6 个词，共 86 个按键：

| 阶段 | 第 1 次组字 | 第 2–6 次组字 |
|---|---|---|
| `tsf.start_composition` | 34.4 ms | 8.9–9.4 ms |
| └ `tsf.start_ui` | 27.5 ms | 2.5–2.9 ms |
| &nbsp;&nbsp;└ `ui.device_init` | 17.3 ms（仅此 1 次） | 不再出现 |
| &nbsp;&nbsp;└ `ui.create` | 7.0 ms | 2.5–2.8 ms（窗口 1.2–1.4，交换链 1.3，`dwrite_init` 降为 8µs） |
| `start_composition` 里 `start_ui` 之外 | ≈ 6.9 | ≈ 6.5 |
| 该次组字第一个按键的 `do_edit_session` | 35.5 ms | 10.5–11.1 ms |
| 其余按键 `do_edit_session` 中位数 | 1.57 ms | — |

结论：

1. `EndUI` 每次组字结束都会销毁候选窗（`Composition.cpp:129`、`:437`），之后每次组字重建窗口，但只花约 2.7ms（`DeviceResources` 单例保留，`device_init` 和 `dwrite_init` 不再重复）。**"保留候选窗不销毁"收益很小（约 2.7ms/词），决定不做。**
2. **每次组字的第一个按键仍要约 11ms，其中约 6.5ms 在 `start_composition` 里 `start_ui` 之外**（`CStartCompositionEditSession` 的创建和 `RequestEditSession`，其中有宿主的同步工作），这部分原因未知，每个词都要付一次，值得单独量。
3. 第 1 次组字比后面多出的 ≈ 25ms，几乎全是 `device_init`（17.3）、`ui.create` 里的 `dwrite_init`（2.9）、`attach`（首次多 1.7）和首次 `warmup`（2.9）。其中 `device_init` 与窗口无关，可以提前。

**阶段 A 落地：在线程获得焦点时预热图形设备（台式机，5 个新记事本进程，`A3_cold.etl`，对比基线 `A2_cold.etl`）**

改动（已提交为 `92b79df`）：

- `WeaselUI/d2d.cpp`：`DeviceResources::EnsureInitialized()` 和 `Reset()` 用 `std::mutex` 保护（原来 `initialized` 是无保护的 `bool`）。
- `include/WeaselUI.h`、`WeaselUI/WeaselUI.cpp`：新增 `weasel::PrewarmGraphics()`，内部调用 `DeviceResources::Get().EnsureInitialized()`。
- `WeaselTSF/WeaselTSF.cpp`：`OnSetThreadFocus()` 末尾，在 `_ProcessKeyEvent` 会处理按键的同样条件下（`!((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled())`）调用它，外面套计时点 `tsf.prewarm_graphics`。
- 没有动：COM 接口、IPC 格式、DPI 逻辑、WARP 优先再退回硬件的逻辑、`StartUI` 里的 `Refresh()`+`Hide()`。

x64、x86 均编译通过，0 错误。我改动的文件里没有新警告（`d2d.cpp` 的 5 条是原有的，行号后移 11 行）；总警告数 25/22 比上次的 24/20 多，是因为改了 `WeaselUI.h` 后更多文件被重编，带出了它们原有的警告。

第一个按键的各阶段，5 个新进程的中位数，单位 ms（括号为范围）：

| 阶段 | A2 基线 | A3 预热后 |
|---|---|---|
| `tsf.do_edit_session` | 36.5（35.2–66.3） | **19.4（18.8–20.5）** |
| └ `tsf.start_composition` | 35.2（33.9–38.4） | 18.0（17.5–19.0） |
| &nbsp;&nbsp;└ `tsf.start_ui` | 27.7（26.9–31.4） | 10.9（10.7–11.3） |
| &nbsp;&nbsp;&nbsp;&nbsp;└ `make_window` | 24.6（24.1–26.9） | 8.1（7.6–8.5） |
| `ui.device_init` | 17.2（在按键路径上） | 17.0（16.6–17.4，**现在在 `tsf.prewarm_graphics` 里，发生在获得焦点时**） |
| `tsf.prewarm_graphics`（焦点路径） | — | **17.2（16.6–17.4）** |
| 后续按键 `do_edit_session` 中位数 | 1.5 | 1.4（无退化） |
| 首次 `tsf.update_ui` / 首次 `ui.do_paint` | 6.1 / 6.6 | 5.9 / 6.5（不变） |

结论：

1. **首键 `do_edit_session` 从约 36.5ms 降到约 19.4ms，减少约 17ms（约 47%）**，和预期一致，达到验收标准"中位数 ≤ 20ms"。
2. 代价：每个进程第一次获得焦点且键盘可用时，UI 线程同步多阻塞约 17ms（`tsf.prewarm_graphics`）。这是从按键路径挪到焦点路径，**不是消除**。台式机上 17ms，目标笔记本上多少没有测。
3. 首次 IPC 往返在 A3 的 5 个进程里都是 8.4–9.9ms（A2 里 3 个是 2.4ms，2 个是 8.7–10.5ms）。逐个对照服务端的 `srv.on_key_event`，往返耗时基本等于服务端处理耗时（例如 9875 对 9744），所以**波动在服务端，不在客户端预热**。原因没有查清楚，可能和服务端空闲后的冷启动有关，**这只是推测**。它有时和预热省下的时间一样大，值得单独查（和第 3 阶段"服务端优先级"相关）。
4. **内存增量（已测，见下）**：每个预热的进程约多 3.9MB 工作集、1.3MB 私有字节。
5. 首键之后到候选框画完仍然有：`update_ui`（≈6ms）、`do_paint`（≈6.5ms）、`start_composition` 里 `start_ui` 之外的约 7ms。这是下一批可优化项。

**预热的内存代价（台式机，x64，5 次独立进程）**

方法：写了一个独立小程序 `memtest.cpp`（`/MT`，链接 `msbuild/Release/x64/WeaselUI.lib`，调用 `weasel::PrewarmGraphics()` 本身），调用前后用 `GetProcessMemoryInfo` 取工作集和私有字节，并用 `EnumProcessModules` 对比新加载的 DLL。结果与真实环境一致：预热耗时 16.9–18.8ms（真实环境 `ui.device_init` 约 17ms），返回 `S_OK`。

| 指标 | 预热前 | 预热后 | 增量（5 次一致） |
|---|---|---|---|
| 工作集 | 6.45 MB | 10.35 MB | **+3.9 MB** |
| 私有字节 | 1.41 MB | 2.74 MB | **+1.3 MB** |
| 新加载的 DLL | — | — | `D3D10Warp.dll`、`dxcore.dll`、`cfgmgr32.dll`、`windowscodecs.dll`、`OLEAUT32.dll`、`clbcatq.dll`、`D2D1Debug3.dll`（7 个） |

注意：

1. `d3d11`、`d2d1`、`dwrite`、`dcomp`、`dxgi` 是 `weasel.dll` 的导入库，会在 DLL 被加载进宿主时就已加载，**不属于预热的增量**。上表只是预热额外多出来的。
2. **`D2D1Debug3.dll` 被加载，是因为 `DeviceResources::EnsureInitialized()` 用 `D2D1_DEBUG_LEVEL_INFORMATION` 创建 D2D 工厂**（`d2d.cpp:71` 附近）。该 DLL 来自系统的图形工具组件，本机有，**目标笔记本上不一定有**；没有时 D2D 会静默回退。这个调试级别在发布版里是否有必要，是一个独立的疑点，**还没有测它对耗时和内存的影响**，不要在没有数据的情况下改。
3. 这是单进程、台式机、x64 的数字。真实宿主里已经加载了部分 DLL 的情况（浏览器、Office 等）增量会更小。x86 和笔记本上没测。
4. **环境坑**：从 Claude 的工具进程里直接启动的程序没有交互式桌面权限，`DCompositionCreateDevice` 会返回 `0x80070005`（拒绝访问），第一次测试因此失败，数字作废；之前启动的 `notepad` 没有出现在桌面上也是同一原因。解决办法：用 `schtasks /create ... /ru <用户> /it` 创建计划任务，在用户的交互式会话里运行，再删除任务。

**D2D 调试级别 `D2D1_DEBUG_LEVEL_INFORMATION` 的开销（台式机，x64，独立对照程序 `tools/perf/d2ddbg.cpp`，每种各 5 个新进程）**

代码事实：`WeaselUI/d2d.cpp:81` 创建 D2D 工厂时用 `D2D1_DEBUG_LEVEL_INFORMATION`（仓库里输入路径上只有这一处；`WeaselDeployer/FontSettingDialog.h:166` 在设置界面里，不在输入路径上）。该级别会让 D2D 加载调试层 `D2D1Debug3.dll`，对每次 D2D 调用做参数校验和调试输出。

对照程序：同一个 WARP 设备，只改工厂的调试级别；每帧：`Clear` + `FillRoundedRectangle` + 6 次 `CreateTextLayout`/`GetMetrics`/`DrawTextLayout`，跑 400 帧取中位数和 p95。

| 指标 | `NONE` | `INFORMATION` | 差异 |
|---|---|---|---|
| 工厂创建 | 0.04–0.05 ms | 0.66–0.74 ms | +0.6 ms |
| 每帧耗时 p50 | 0.181–0.186 ms | 0.413–0.418 ms | **+0.23 ms（约 2.2 倍）** |
| 每帧耗时 p95 | 0.249–0.280 ms | 0.485–0.496 ms | +0.23 ms |
| 创建后工作集增量 | 1.59–1.60 MB | 2.38–2.40 MB | +0.8 MB |
| 加载 `D2D1Debug3.dll` | 否 | 是 | |

5 轮结果几乎相同，差异远大于噪声。

局限：这是合成负载，不是真实的候选窗绘制（真实 `ui.do_paint` 中位数约 3.1ms，包含圆角、阴影、图标等更多 D2D 调用）。**真实候选窗里省多少，要把 `d2d.cpp:81` 改成 `D2D1_DEBUG_LEVEL_NONE` 后重新抓 `ui.do_paint` 才知道，这一步还没做。** DirectWrite 的布局（`ui.refresh.layout` ≈ 2.7ms）不受 D2D 调试级别影响。

**把 `d2d.cpp:81` 改成 `D2D1_DEBUG_LEVEL_NONE` 后的真实候选窗数据（台式机，同一个记事本内连续打词，`M4_multi.etl` 对比 `M3_multi.etl`）**

改动：`WeaselUI/d2d.cpp:81`，`D2D1_DEBUG_LEVEL_INFORMATION` → `D2D1_DEBUG_LEVEL_NONE`，一行。x64、x86 编译通过，0 错误，只重编了这个文件，5 条原有警告。注意：用 `sed -i` 改会改变整个文件的换行符（diff 1006 行全变），应该用编辑工具只改那一行。

| 阶段 | M3（INFORMATION，无预热） | M4（NONE，有预热） |
|---|---|---|
| `ui.do_paint` p50 / p95 | 3379 / 4133 µs（n=37） | **1736 / 2162 µs（n=48）** |
| `ui.present` p50 | 185 µs | 195 µs（不变） |
| `ui.refresh.layout` p50 | 2646 µs | 2423 µs（DirectWrite，不受 D2D 调试级别影响，差异视为波动） |
| `tsf.get_response` p50 | 412 µs | 422 µs（不变） |

`ui.do_paint` 中位数 **3.38ms → 1.74ms，减少约 49%**，p95 也从 4.13ms 降到 2.16ms。A3（有预热、INFORMATION）里的后续按键 `do_paint` 中位数是 3.08ms，和 M3 一致，所以这个差异来自调试级别，不是预热。验证：改后新启动的记事本（PID 5428）里 `D3D10Warp.dll` 已加载，`D2D1Debug3.dll` 没有加载。

局限：M3 是预热之前的构建，M4 是预热之后，所以 `start_ui`、`ui.create` 等不能直接比，这里只比较和预热无关的 `do_paint`。M4 里打了 8 个词，M3 是 6 个，次数不同。台式机上的数字，目标笔记本上没测，`D2D1Debug3.dll` 是否存在也没验证。

**服务端首键 9ms 波动的拆解（台式机，场景 A 重做，`A4_cold.etl`）**

先排除的假设：**服务端空闲时长**。对比各客户端第一个真实按键与服务端前一次调用的间隔：A2 里间隔 586ms 的进程要 10.4ms，间隔 572ms 的只要 2.4ms；A3 里间隔 0.5–3.3 秒不等，全是 8–10ms。没有相关性。客户端往返耗时（`tsf.ipc_key_roundtrip`）几乎等于服务端 `srv.on_key_event`（如 9875 对 9744µs），所以波动在服务端。

新增计时点（只加计时，`RimeWithWeasel/RimeWithWeasel.cpp` 的 `ProcessKeyEvent`，6 行）：`srv.rime.process_key`（librime）、`srv.rime.vim_check`、`srv.rime.respond`（`_Respond`，序列化）、`srv.rime.update_ui`（`_UpdateUI`）。

| 按键 | 总耗时 | `process_key`（librime） | `respond` | `update_ui` |
|---|---|---|---|---|
| 每个新进程的第一个真实按键（5 个进程） | 8.5–9.7 ms | **7.7–8.8 ms** | 0.8 ms | 0.04 ms |
| 空格（上屏） | 4.8–5.6 ms | **3.9–4.7 ms** | 0.8 ms | 0.04 ms |
| 所有按键中位数 | 0.64 ms | 0.022 ms | 0.57 ms | 0.039 ms |

结论：

1. **首键的 9ms 几乎全在 librime 的 `process_key` 里**（7.7–8.8ms），我们自己的代码（序列化 0.8ms、服务端界面 0.04ms）只占零头。
2. **慢的是"新会话的第一个真实按键"，不是"每次组字的第一个按键"**。同一个进程连续打词（`M4_multi.etl`）：第一个按键 9.7ms，之后每个新词的第一个按键约 2ms，上屏的空格约 3.9–5.1ms。每个新进程会创建新的 librime 会话，所以每个新进程都付一次。
3. **上屏（空格）一次要 4–5ms**，每个词都要付，也在 `process_key` 里。可能和写用户词库有关，**未验证**。
4. 偶发尖峰：M4 里服务端有一次 26.9ms、一次 11.6ms 的按键，原因没查。
5. librime 内部具体慢在哪**没有查清**。本机环境供参考：当前方案是 `luna_pinyin_simp`（+ `easy_en`），`luna_pinyin.my.table.bin` 28MB、`easy_en.table.bin` 15MB，`luna_pinyin_simp.userdb` 30.3MB（leveldb）。台式机有 NVMe SSD；目标笔记本的磁盘更慢，这个首键延迟可能更大，**没测**。librime 源码子模块没有初始化（本项目用预编译的 `rime.dll` 和 `rime.pdb`）。

可选的下一步（均未做）：
- 用 WPR 对服务端做 CPU 采样，结合 `rime.pdb` 看首键的调用栈（最直接，需要配置符号）。
- 用只装 `luna_pinyin` 的临时用户目录做对照（和之前验证 ETW 时一样改 `RimeUserDir`），看首键耗时是否随词库、用户词库大小变化。
- 在服务端创建会话后、用户按第一个键之前，先喂一个无害按键预热。**会引入服务端并发和会话状态问题，属于 `AGENTS.md` 要求先出方案的高风险改动，没有做。**

**服务端首键耗时与用户数据的关系（干净用户目录对照，`A5_testdir.etl` 对比 `A4_cold.etl`，台式机）**

方法：停掉服务端，临时把 `HKCU\Software\Rime\Weasel\RimeUserDir` 指向空目录 `C:\Users\Administrator\lwtest_rime`（`default.custom.yaml` 里只留 `luna_pinyin_simp`），用 Idle 优先级启动服务端让它自己部署，再做场景 A（5 个新记事本进程，各打 `nihao`+空格）。完成后恢复 `RimeUserDir`、删除测试目录、用原配置重启服务端。

| 对照 | 新进程第一个真实按键的 `srv.rime.process_key` | 上屏（空格）一次 |
|---|---|---|
| 真实目录（A4，服务端 Normal 优先级，28MB 自定义词库 + 30MB 用户词库 + `easy_en`） | **7.7–8.8 ms**，5 个进程都慢 | 3.9–4.7 ms |
| 干净目录（A5，服务端 Idle 优先级，标准 `luna_pinyin` 13MB 表，空用户词库） | **0.30–0.34 ms**，5 个进程里 5 个都快 | 没有超过 3ms 的 |
| A5 里服务端启动后的第一个会话 | 5.9 ms（一次性，之后消失） | — |

结论：

1. **首键 9ms 和上屏 4–5ms 取决于用户的词库/配置，不是 librime 的固定开销。** A5 的服务端是 Idle 优先级，只会让耗时偏高，所以真实差异不会比这里更小。
2. **没有证明是哪一样造成的。** 这次同时换掉了：28MB 自定义词库 `luna_pinyin.my` 与标准词库、30MB 用户词库 `luna_pinyin_simp.userdb` 与空用户词库、`easy_en`（15MB 表）、`*.custom.yaml` 里可能有的补丁或脚本。
3. 没有解释为什么真实配置下**每个新会话**都要付一次，而干净配置下只有服务端的第一个会话付。
4. 台式机是 NVMe SSD，目标笔记本磁盘更慢，真实配置下这个延迟可能更大，没测。

建议的下一步对照（未做）：干净目录里**只加入备份中的 `luna_pinyin_simp.userdb`**（从备份复制，不碰真实目录）；首键回到 8ms 就是用户词库，否则再依次加自定义词库和 `easy_en`。

**对用户真实目录 `%APPDATA%\Rime` 的影响（必须记录）**

本机装了我们的新版（`rime.dll` 2026-06，官方 0.17.4 自带的是 2025-02）后，服务端第一次用真实目录启动时重新部署了 `build/`：`build/` 下的 `default.yaml`、`easy_en.schema.yaml`、`luna_pinyin_simp.schema.yaml`、`luna_pinyin.my.table.bin`、`luna_pinyin.my.reverse.bin`、`stroke.*`、`weasel.yaml` 与备份不同；`installation.yaml`、`user.yaml`（程序自己记录状态的文件）也变了。用户自己写的配置（其余顶层文件）逐个比哈希，没有变化；用户词库总大小一致（30.31MB，文件名变化是 leveldb 正常压缩）；备份 `C:\Users\Administrator\RimeBackup_20261119` 完整保留。目录总大小 267.55MB → 270.63MB，文件数 116 → 115（leveldb 日志文件轮换）。装回官方旧版 `rime.dll` 时会再部署一次，旧版能否直接读新版生成的 `build/` 没有验证。

**还没验证的（截至目前）**

1. 目标笔记本上的焦点卡顿（`tsf.prewarm_graphics`）、首字延迟、部署期间输入、睡眠唤醒后第一个字都没测，**本文件里所有数据都来自台式机**。
2. 没有测过开启和关闭 ETW 时的额外开销。
3. 用户在本机的输入法现在是我们的测试版（官方 0.17.4 已卸载）。要退回：卸载测试版、重装官方 0.17.4，词库不用动（备份在 `C:\Users\Administrator\RimeBackup_20261119`）。注意真实用户目录的 `build/` 已被新版 `rime.dll` 重新部署过，见上文。

**抓取方法**

```
logman create trace lw -p "{5974258d-005e-4490-adef-9e265b26abf5}" 0xffffffffffffffff 0xff -o lw.etl -ets
（打字）
logman stop lw -ets
tracerpt lw.etl -o lw.csv -of CSV -y
python tools/perf/summarize.py lw.csv
```

**遗留和下一步（已被后面的记录取代，仅供回溯；当前状态见最上面的"当前状态总览"）**

1. ~~在目标机器上完整安装我们的构建，验证 T1、T5、T7 能抓到。~~ 后来用户决定直接在本机（台式机）卸载官方版、安装测试版来测，已完成，T1/T3/T5/T7 都抓到了；目标笔记本仍然没测。当时出的安装包：`output/archives/weasel-0.17.4.0.199bfae-installer.exe`（12,186,806 字节，SHA256 `59cd2c73d73d1360024767114853b6ac47cc28fb70bf7d094a24651ea8e13aa1`，未签名，只含 x64 和 x86，不含 ARM；NSIS 通过 winget 装在台式机上，用 `makensis /DWEASEL_VERSION=0.17.4 /DWEASEL_BUILD=0 /DPRODUCT_VERSION=0.17.4.0.199bfae output\install.nsi` 生成，Idle 优先级）。用户决定先卸载笔记本上的官方版再装，卸载前必须备份 `%APPDATA%\Rime`。
2. 四个场景：新程序第一个字（已在台式机测，见场景 A）、连续快速输入（台式机上有数据，见 M3/M4）、部署期间输入（**没测**）、睡眠唤醒后第一个字（**没测**）；目标笔记本上都没测。
3. T6、T8 已补（`23589cc`），服务端计时已补（`e458984`）；T0、T2、T4、T9 没补。
4. ~~`Present(1, 0)` 是否阻塞 UI 线程，只是推测。~~ **已证伪**：`ui.present` 中位数约 190µs，不是被垂直同步阻塞。

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
