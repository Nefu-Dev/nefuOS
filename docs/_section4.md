## 四、2026-11-09 第四轮：从"全 401"推进到 Create World 对话框

这一轮的目标只有一个：把 bloxd 的"进世界"链路推到能出现 3D。起点是上一轮结束时 `cv=0` 且所有 social/matchmake 请求全 401。结论先说：**身份链路已经彻底打通，建房对话框已经真实渲染，但最后一道门是 Cloudflare Turnstile 挑战帧在自研 JS 引擎里跑不完**，因此拿不到 greenlight token，join/create 永远发不出去。以下每一条都有运行日志作为证据。

### 1) 401 的真因不是 cookie，而是我们伪造了身份

上一轮的推断（"cookie 没带"）是错的，被两条实验同时否掉：

- 给 cookie jar 做同站放宽（`_sameCookieDomain`：`a===b` 或 `.` 后缀匹配，并加 `[CK] <host> <name>` 打印）后跑测，`[CK]` **一条都没有** —— 服务端压根没下发 cookie，不是我们丢的。
- `curl -c jar https://bloxd.io/` 落出的 jar 为空；`GET /` 200 但零 `Set-Cookie`。

真正的身份在响应体里：bloxd 自己造 `metricsCookies`（含 `3PSIDMC` 签名串）发 `POST /index/metrics/cookies`，服务端凭它认人，响应体 958 B 里带 `{name, cosmetics, ranks, availableUserContextTypes, 3PSIDMCPP, countryCode, ...}`。我们此前用本地 `_identityJson()` 假造这份身份，于是后续每个 social/matchmake 都被 401 打回。

修法：`g.fetch` 的 `index/metrics/cookies` 分支改成"先发真请求，2xx 且有 body 才采用，否则回退"，成功后置 `g.__nfuMcookReal=1`。证据 `bd15.err`：`[MCOOK] real status=200 bodyLen=958`，同时 `401 计数 = 0`。

### 2) 响应头每行首字符被吞（Colyseus 判型的隐形杀手）

`platform_http_last_headers()` 返回的每行都少第一个字符（`Content-Type` 变 `ontent-Type`）。根因在 `backends\win32\win32.cpp` 的 `win_capture_headers`：跳过状态行后 `p` 已指向首个头字符，旧代码却从 `p+1` 取行。重写为"p 指向首字符 → 逐行 `p..le` 取 → 跳过 CRLF"（注释 `NEFU-FIX`）。这同时解释了 `consumeSeatReservation` 报的 `cannot read property 'name' of undefined`：`Content-Type` 被读坏后 `.json()` 判型失败，seat 响应没被解析成对象。

### 3) 250 ms 帧预算把自己的初始化链掐断

`mini_js_tick` 里 `rc.deadline = platform_tick_ms() + 250`。身份返回后的那一帧要同时跑 React 重渲染 + Turnstile + 广告 adaptor，超预算被 QuickJS 中断，整条初始化链作废并抛 `UNHANDLED-REJECT: InternalError: interrupted`。改成命名常量 `kFrameBudgetMs` 并把预算设为 **6000 ms**（注释写明 250 ms 的后果；回调数上限仍是 rAF 64 / timers 192，不会跑飞）。`bd19` 起 `interrupted` 计数为 **0**。

### 4) 大厅页与建房对话框的真实状态（本轮最重要的证据）

- `bd21.err` 的 `[LOBBY]` 三连，全部 200 且是真实数据：
  - `social/get-favourite-lobbies` → `[]`
  - `social/get-open-custom-lobbies` → `[{"game":"classic","name":"[建国サバイバル2026]","locked":false,"players":39,...}]`（39 人在线的大厅）
  - `index/lobby/get-new-starter-name` → `{"starterName":"grim_ruins_259657"}`
- `bd26.png` / `bd27.png`（1000×800 截图）：`/game/survival` 上真的渲染出 **Create World 对话框** —— 用户名 chip（`FeralWombat478558` / `LumpyBoss7601773`）、`Show Advanced Options`、`Name` 输入框、`Default Gamemode`、`Randomized`。这推翻了此前"点 Create 没反应"的判断：对话框是渲染出来的，只是不进 `NF_DUMP_DOM` 的产物（`dom_bd20.html` 里搜不到对话框节点）。
- 注意：对话框的**布局是坏的**（两列被拉成 1000/800 下的错位，确认按钮落在可视区以下），所以"点 Create"仍然点不到确认键，`bd27.err` 里两次 `text:Create` 都落在 `#529`（`CreateCustomLobby` 外层），只重复触发 `get-new-starter-name`。

### 5) 当前唯一硬卡点：Turnstile 挑战帧跑不完，拿不到 token

链路是 `pg(gg)` → 等 `turnstileLoadedPromise` → `getTurnstileToken(gg)` → 失败则整条 play 流作废。我们自己不造 token，只要求"Cloudflare 的真实脚本能在我们的 frame 里跑通"。现状：

```
[DYN-IFRAME] .../turnstile/f/av0/rch/<id>/0x4AAAAAAAa4cz8QxEw-M2SE/dark/fbE/new/normal?lang=auto 636456
[FRAME-EVAL-OK] 0
[FRAME-TMO-ERR] msg=not a function (got undefined) || at VJ (...:2:610404)
```

- 帧脚本本身能下载（本次 636,456 B）、能 eval、能进 setTimeout 回调，死在某次"取到 undefined 的函数"调用上。
- 本轮新增取证开关 `NF_DUMP_FRAME=1`：在 shim eval 帧脚本前注入 `__nfuDumpFrame`，把脚本按 200 字符切块打 `[FD]` 行（单行日志上限约 254 字符，4,000 字符/块会被截断）。`bd26` 全量导出了 636,456 字符，`tools\frame_ctx.py` 自动按报错 `:2:<col>` 切上下文。
- 已确认这不是"缺全局"：`[FRAME-MISS]` 计数为 **0**，所以是某个真实对象上的方法/属性在我们这里返回 `undefined`。
- 本轮已按"桌面浏览器一定有"补齐 `PerformanceObserver`、`AudioContext`（含 `createBuffer/startRendering` 等）、`webkitAudioContext`、`visualViewport`。补完后失败点从 `:2:486439` 后移到 `:2:622007` 再到 `:2:610404`（不同 challenge 脚本偏移不可跨次比较），说明方向对，但还没到终点。
- 同时修掉 `Error playing new music` 这一类 `not a function` 的同类问题（`g.Audio` 只有 play/pause/canPlayType）。

### 6) 下一步（按依赖顺序，不再跳步）

1. 帧内缺 API 诊断升级：把 `document` / `navigator` / `screen` 在 frame scope 里包一层 Proxy，属性读到 `undefined` 就打 `[FRAME-MISS-OBJ] <obj>.<prop>`，直接指出 `VJ` 里那个 undefined 是谁（当前 `[FRAME-MISS]` 只能覆盖全局，覆盖不到对象成员）。
2. 拿到 token 后才会出现 `POST /matchmake/joinOrCreate/<lobby>`，之后是 Colyseus 长连接（`__nfuWsOpen` 钩子已在，需核对握手与帧协议）。
3. 3D 之前还有纹理图集：`createImageBitmap` / `2d.drawImage` / `getImageData` 必须给真像素（现在 `toDataURL` 用 `data:image/x-nfucv;base64,NFUCV:<id>` 句柄冒充 PNG）。
4. 最后才是 swgl 渲染与帧循环。

### 7) 回归

- `node --check verify_out\shim.js` 每轮 exit 0（本轮改了 shim 的 API 补齐、`[LOBBY]` 打印、`[FD]` dump）。
- 构建：build55 48.3 s（win32.cpp 未动则为 12–15 s），build56 15 s，build57 13.7 s，build58 13.5 s，build59 12.9 s。
- 未回归项：`cv=0` 依旧；`ByteBrew Status 0` 依旧（站点不在白名单，不发请求）；`[TMO-SKIP]` 时钟基差异未查。
