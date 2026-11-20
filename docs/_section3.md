## 三、2026-11-09 第三轮：shim 语法复活后的真病因（Worker 与 IDB get 语义）

### 1) 一句话结论

`0% Loading required libraries` 不是纹理、不是 WebGL、也不是网络。它是
`Promise.all([preload, turnstileLoadedPromise, waitUntilAllSdksReady()])` 里 firebase 的
IndexedDB 读取抛了 TypeError，整条 Promise 静默 reject，`.then()` 永远不执行，
所以进度条停在 0%，而且一条错误都不告诉你。

### 2) 本轮三处修复（均已进 `build\nefuOS.exe`，build46/build47）

| # | 问题 | 根因 | 修复 |
| - | ---- | ---- | ---- |
| 10 | 世界生成 worker 报 `error from worldgen worker`，加载卡死 | shim 里 `g.Worker` 只是"报一次错就作废"的桩 | `jsdom_shim.h` 换成真实现：Blob 经 `createObjectURL` 已是 base64 `data:` URL，直接解出源码；`new Function(...)` 在同一上下文里按定时器队列交错执行（无 OS 线程、无第二个 QuickJS 上下文）；`postMessage` 深度克隆，两侧不共享引用 |
| 11 | `.err` 里密密麻麻的 `log#N:` 全是空行，看不到游戏自己的状态 | console 用 `Array.prototype.join(' ')`，对象一律变空串 | `minijs.cpp` 加 `__ncFmt`：深度 2 的 JSON 展开（数组 10 项、对象 14 键、最长 420 字符） |
| 12 | `Loading required libraries` 的 `Promise.all` 静默 reject | `IDBObjectStore.get()` 未命中返回 `null`，而规范要求 `undefined`；firebase 写的是 `void 0===Vi?null:Vi.value`，`null` 能过判空、下一句就爆 `Cannot read property 'value' of null` | `jsdom_shim.h` 改为 `hit ? m[k] : void 0` |

### 3) 实测证据（bd10 / bd11，`https://bloxd.io/game/survival`）

- `[WORKER-OK] data:application/octet-stream;base64,LyohIEZvciBsaWNlbnNlIGl 1014329`
  —— 1,014,329 字节的 worldgen 脚本真正载入并执行，`error from worldgen worker` 彻底消失，
  全程无 `WORKER-ERR` / `WORKER-MSGERR` / `WORKER-LOADERR`。
- `log#112 [2026-11-09T06:15:36.796Z] All required libraries loaded`
  —— 首次出现，说明 `LOAD_REQUIRED_LIBS` 这一步真的跑完了。
- 真网络开始工作（此前全程 0 条 POST）：
  `POST https://social2.bloxd.io/matchmake/joinOrCreate/social_updates_2`、
  `social/get-game-ccus`、`social/get-favourite-lobbies`、`social/get-open-custom-lobbies`、
  `social/get-social-information`、`social/analytics/report-events`、
  `https://bloxd.io/index/payment/history`、`https://bloxd.io/index/xsolla/catalogue`。
- 上述请求全部 `401`（`Failed to fetch game CCUs / refresh social state / Super Rank catalogue / payment history`）
  —— 未登录访客没有 greenlight token。
- `dom_bd11.html`（45,589 B）：`<canvas` 计数 = 0、`nfu_cv` 计数 = 0。
  仍能看到 `0%` + `Loading required libraries` 的隐藏 overlay（`visibility:hidden;opacity:0`）。
- `GL: ctx nfu_cv1@300x150 300x150 rc=0x10000` 是本工程自建桥的上下文，不是 bloxd 创建的。

### 4) 现在卡在哪一道墙

`bloxd_main.js` 偏移 ~438,717 处写得很清楚：

```
const kt = useTurnstile ? {
   turnstileLoadedPromise: new Promise(res => window.onTurnstileLoaded = () => res()),
   getTurnstileToken: async go => new Promise((res, rej) => Lg.enqueue(new cg(res, rej, go))),
   tryShowWidgetPopup: () => Pg && Wg(true)
} : {...}
...
window.turnstile.render('#turnstile-widget', { sitekey: '0x4AAAAAAAa4cz8QxEw-M2SE',
                                               action: 'Greenlight', ... })
```

即：登录/配对需要 Cloudflare Turnstile 的 token，而 widget 是在 **iframe** 里跑的。
本工程的 iframe 计数一直是 0，所以 token 拿不到 → 服务端 401 → matchmaking 没有 seat →
不会创建 canvas → 3D 世界永远不出现。

边界不变：本工程只做"让真实的 Turnstile 脚本能在我们的 iframe 里跑完"，不造假 token、不破解人机验证。

### 5) 下一步（按依赖顺序）

1. shim 补真 iframe：子文档 + 独立 document/window + 跨文档 postMessage，让 turnstile api.js 自己跑；
2. 拿到 seat 后接 Colyseus 长连接（`__nfuWsOpen` 钩子已有，需核对握手与帧协议）；
3. 纹理图集：`createImageBitmap` / `2d drawImage` / `getImageData` 必须给真像素；
4. 最后才是 swgl 渲染与帧循环。

### 6) 回归

- `bd11`（build47）：未崩，DOM 396~400 节点，`layout_page runs=84`。
- `x1.html` 探针（shim 语法回归件）仍不崩；`node --check verify_out\shim.js` exit 0。
