### 4.11) bloxd.io 打通进度：布局 13 倍提速与 Turnstile 边界（2026-11-09 实测）

本节替换旧版 4.11 中"瓶颈不在选择器匹配""两次优化零收益"的结论——那两条已被本轮实测推翻，作废。

#### 4.11.1) 先解决"编译没生效"这个测量污染源

旧结论之所以错，是因为改的代码根本没进二进制：

- `tools\build.ps1` 的 `Test-ObjFresh`（约 282 行）判据失效：源码比目标文件新时它仍报"最新"。
- `-Full` 也会命中同一判据，不能作为"真编译过"的证据。
- 顶层 `build\*.o`（`browser.o` 等）是 9 月残留，时间戳毫无参考价值。

**唯一的可靠流程**：改完 `.cpp` 先删该编译单元的三件套再构建，然后用字节级搜索确认唯一标记串进了 exe。

```
Remove-Item build\obj\host_core\core_apps_browser.o
Remove-Item build\obj\host_core\core_apps_browser.o.stamp
Remove-Item build\obj\host_core\core_apps_browser.d
& '.\tools\build.ps1' -Target host        # 增量约 27.6-28.7 s
```

验证用 `[IO.File]::ReadAllBytes` + Latin-1 解码 + `IndexOf`，哨兵串 `NFLAYMRK1`。第一次按这个流程重编时立刻暴露了一个真实语法错：两个 `static long` 计数器声明写在 `layout_page` 上方，而使用点在 `layout_node` 之前——声明晚于使用。**此前上百次"构建成功"从未把这段编进去，所以这个错一直没暴露。**

#### 4.11.2) 选择器索引：12,749,760 → 963,975 次尝试

计数器落地后的第一份真实数据（页面 8598 节点 / 4035 盒 / 1520 条规则）：

```
layout_page nodes=8598 runs=4035  2281ms
layout_cost visits=8388 css=12749760 rules=1520 boxes=4035
```

`visits ≈ nodes`（8388 vs 8598），说明"浮动测量趟放大访问次数"的假设不成立；`css = 8388 × 1520`，几乎全部时间花在完整 `css_match`。此前两版 `css_quick_reject` 从未减少过一次调用——它的确没生效，不是瓶颈不在这里。

真改法是加选择器索引，依据 CSS 匹配的硬前置条件：最右复合选择器不匹配时，祖先/兄弟上下文怎么组都不可能命中。

- 规则按最右段的 tag / class / id 建桶，存 `(key, rule_index)`，`qsort` 后二分查找。
- 每个盒子先 `memset` 一张 `uint8_t` 跳过位图，再把自己命中的键对应的规则下标全标上。
- 最右段含 `:` / `[` / `(` / `\` / `*` / 组合符的一律落 catch-all 桶 `'*'`，正确性优先。
- 级联顺序完全不动：位图只决定"跳过"，循环仍按 `0..css_n` 原序走。

实测：

```
css=963,861-963,975     (原 12,749,760，降 13 倍)
297 / 313 / 329 / 343 / 390 / 406 / 531 ms   (原 2109-2328 ms)
```

副产物：`js: TREQ ... dt=31`（此前 4454/9469），定时器几乎即时执行，帧率由 ~0.3 fps 提升到 ~3 fps。

#### 4.11.3) Turnstile 帧：从"节点不存在"修到"脚本真的跑起来"

帧脚本日志行数是最直接的进度指标：几百行 → 3365 行，说明脚本主体真的执行了。三处兼容修复（都只让真实脚本能在我们的框架里跑通，不涉及伪造凭据）：

1. **帧 `getElementById` 命中不到时自动建节点并挂到帧 body**，保留 `[FRAME-ID-MISS]` 诊断。改完立刻出现 `[REQ] POST .../matchmake/joinOrCreate/...`。
2. **`querySelector('#id')` 与 `getElementById` 语义对齐**——Turnstile 的节点解析走 `querySelector('#' + id)`（反查帧脚本明文可证：`function Ji(nZ){ ... , '#'+nZ }`）。只改 `getElementById` 不够。
3. **自动创建必须限定在帧内**。第一版把主页面也放开，结果 `querySelector('#x')` 被应用代码大量当作"存在性探测"用，凭空造出一堆节点污染整棵 DOM——页面白屏（bd88 现场：地址栏正常、内容区空白、只剩左下"登录账户"面板）。收紧为 `if (!n._nfuFrame) return [];` 后页面恢复正常渲染（8597 节点、390 ms）。

崩点函数名每轮都在变（`J6 → Ev → ji → sK`），说明链条在前移而非卡死。

#### 4.11.4) 游戏房：这一层试不通（负面证据，勿重复）

把页面自己发过的真实请求当控制组，页内 `fetch` 复打，靠错误码形态判定：

| 主机 | 房间名 | 结果 |
| --- | --- | --- |
| `social27.bloxd.io` | `social_updates_27` | **200**，返回真实房间，`clients: 998` |
| `social27.bloxd.io` | `survival_27` | 200 + `code 4210 population room not defined` |
| `social27.bloxd.io` | `game_updates_27` | 200 + `4210 not defined` |
| `social27.bloxd.io` | `game_survival_27` | 200 + `4210 not defined` |
| `game1.bloxd.io` | 任意 | **status 0**，主机不通 |
| `game27.bloxd.io` | 任意 | **status 0**，主机不通 |

结论：

- matchmaking **不校验**人机验证 token（错误码全是业务码 4210/4212，从未出现 401/403）。
- 但 3D 游戏房**不在** `social*.bloxd.io` 的 `joinOrCreate` 命名空间里，也**没有**独立的 `game*.bloxd.io` 主机。`social_updates_<N>` 是社交房（`Og()` 是社交连接管理器，不是进游戏房的路径）。
- 游戏侧不走 Colyseus 的 `joinOrCreate`；源码里 `joinById` / `create` 只是 SDK 通用方法，业务侧唯一调用点是社交那一个。

#### 4.11.5) 当前卡点与结论

**3D 世界仍未进入。** 卡在两处，都属硬约束：

1. **Turnstile**：帧脚本内部初始化不收敛（节点解析返回 null，脚本又几乎不调用 `document.createElement` / `getElementById`，`[DOC-READ]` 只记录到 4 个键）。继续往下是逆向它的反自动化逻辑，超出"只做兼容、不破解人机验证"的既定边界。
2. **游戏房命名空间未知**：唯一已知有效房名形状是 `social_updates_<shard>`，游戏房不在这套里，`getGameUrl` 在源码里搜不到。

已确认做不通、不要重复试的清单见 4.10 节与本段 4.11.4。

#### 4.11.6) 渲染能力侧已有的证据

`verify_out\gl_world.html` + `w3d3.png`：16 个草地纹理立方体，近大远小透视、前后遮挡、面间明暗、纹理格子全部正确。渲染栈不是 bloxd 的瓶颈——网络入口才是。
