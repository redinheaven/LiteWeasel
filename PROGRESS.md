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

改动（未提交）：

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
4. **还没测内存**：预热让每个获得过焦点的进程都提前加载 D3D11/WARP 等 DLL，会多占内存。需要在新进程获得焦点前后分别取工作集和私有字节。
5. 首键之后到候选框画完仍然有：`update_ui`（≈6ms）、`do_paint`（≈6.5ms）、`start_composition` 里 `start_ui` 之外的约 7ms。这是下一批可优化项。

**还没验证的**

1. 内存增量没测（见上）。
2. 目标笔记本上的焦点卡顿和首键延迟没测。
3. 没有测过开启和关闭 ETW 时的额外开销。
2. 没有分离"新程序里的第一个字"（冷启动）、部署期间、睡眠唤醒后这几个场景的数据，这次是混在一起的一次连续输入。
3. 没有目标笔记本上的数据。
4. 用户在本机的输入法现在是我们的测试版（官方 0.17.4 已卸载）。要退回，卸载测试版、重装官方版即可，词库不用动。

**抓取方法**

```
logman create trace lw -p "{5974258d-005e-4490-adef-9e265b26abf5}" 0xffffffffffffffff 0xff -o lw.etl -ets
（打字）
logman stop lw -ets
tracerpt lw.etl -o lw.csv -of CSV -y
python tools/perf/summarize.py lw.csv
```

**遗留和下一步**

1. 在目标机器（X1 Carbon 5th、X220）上完整安装我们的构建，验证 T1、T5、T7 能抓到。**已出安装包**：`output/archives/weasel-0.17.4.0.199bfae-installer.exe`（12,186,806 字节，SHA256 `59cd2c73d73d1360024767114853b6ac47cc28fb70bf7d094a24651ea8e13aa1`，未签名，只含 x64 和 x86，不含 ARM；NSIS 通过 winget 装在台式机上，用 `makensis /DWEASEL_VERSION=0.17.4 /DWEASEL_BUILD=0 /DPRODUCT_VERSION=0.17.4.0.199bfae output\install.nsi` 生成，Idle 优先级）。用户决定先卸载笔记本上的官方版再装，卸载前必须备份 `%APPDATA%\Rime`。
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
