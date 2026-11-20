# nefuOS 浏览器渲染对齐报告（2026-11-08）

目标：让自研浏览器（C++ 内核 + QuickJS + 自研 CSS / 布局 / 绘制）在 baidu /bloxd.io/ 20+ 大型站点上达到 Edge 的渲染水平。

本文件记录**这一轮实际做掉的东西**、**24 站实测矩阵的结果**、以及**逐项根因**。结论是：离 "和 Edge 一样" 还有明确差距，差距已经全部定位到具体代码位置，不是玄学。

## 一、本轮落地的修复（已编译进 dist\nefuOS.exe）



| # | 问题                                  | 根因                                                              | 修复                                                                                           |
| - | ----------------------------------- | --------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| 1 | 百度大 logo 解码成功却画成白块                  | `scale_surface()` 只搬 `0x00RRGGBB`，把 alpha 字节清零，导致透明度检测恒为 "全不透明" | 新增 `scale_surface_bgra()`，逐像素复制 BGRA 四字节，仅 `<img>` 分支使用；logo 立即正常显示                          |
| 2 | 所有图片被描灰边（Edge 没有）                   | `paint_page` 对每个 run 无条件 `gfx::rect`                            | 改成 `if (r.noframe) {}`，不再描边                                                                  |
| 3 | `margin-left/right/bottom/top` 被丢弃  | 只解析 `margin` 首值                                                 | `CSSRule/Eff` 加 `mar_l/mar_r/mar_b`，块级分支按 `indent += mar_l`、`w -= mar_r`、退出时回滚并 `y += mar_b` |
| 4 | `left:50%` 这类百分比定位被当成 0             | `css_px` 只认 px                                                  | 新增 `pos_left_pct/pos_top_pct`，绝对定位分支按容器宽 / 视口高换算，且与负 `margin-left` 叠加（百度 logo 的经典居中写法）       |
| 5 | 长选择器（`:where()` 链 >127 字符）把后续整表解析带偏 | `sel[128]` 定长缓冲                                                 | 尚未改（见未完成项 3）                                                                                 |

已验证：`cc_check` 通过（对象 323805 B），`build.ps1 -Target host` 通过，exe 内 marker 校验通过。

## 二、24 站实测矩阵（tools\parity\_run.ps1 + tools\parity\_score.py）

产物：`verify_out\matrix\parity_report.html`（左右并排对照，暗色 GitHub 风格）、`parity_scores.csv`。

口径：Edge headless 755×480 视口截图 vs nefuOS 1000×800 帧缓冲裁出 (20,150)-(775,630) 的页面区；`sig` = 8×8 区块平均色相似度，`mad` = 逐像素平均绝对差，`ink` = 内容像素占比（非近白像素比例，用来抓 "整页空白"）。

**重要提醒**：`sig` 是色块相似度，白底页面之间天然高分，**不能单独当 "一致" 的证据**。例如 baidu 的 sig=0.915，但热搜区、搜索按钮位置仍与 Edge 不同。判定要结合 `ink` 与下面的根因分类看。

### 结果分布（24 站）



* 有可比对截图的 19 站里：A（色块高度相似）3、B（接近）7、C（部分）4、D（结构不同 / 内容缺失）5。

* `X 站点侧拦截`：microsoft /stackoverflow/weibo /zhihu/oschina（详见下表根因）。

* 无 Edge 参照（本机 headless Edge 拉不到页面）：bbc /hn/mozilla /wikipedia；bbc、hn 在 nefuOS 侧同样 `connect fail err=10060`（本机到这两个域名的 TCP 直接超时，与渲染无关）。

* bloxd：两侧都没有可用的成对截图（nefuOS 侧 26s 仍在拉 chunk）。

### 逐站根因（从 `.nefu.out` 日志提取，不是猜的）



| 站点                | 引擎侧事实                                                | 根因分类                                                                        |
| ----------------- | ---------------------------------------------------- | --------------------------------------------------------------------------- |
| stackoverflow     | `tls: status 403` → 5,652 B 质询页，commit nodes=8       | Cloudflare 反爬拦截（站点侧拒绝），非渲染缺陷                                                |
| weibo             | 连续 5 次 `302 Found` → 97 B 空壳                         | 重定向循环（引擎不跨跳转保存 Cookie）                                                      |
| oschina           | `WININET fallback` 3730 B 小页 → nodes=4               | 站点返回反爬小页                                                                    |
| microsoft / zhihu | 403 / `dataset of null`、`InternalError: interrupted` | 站点拦截 + JS 预算中断                                                              |
| github            | 5 个主题 CSS 各 87 KB → **0 rules**（`have=0 max=8192`）   | 现代站点 CSS 全是自定义属性（`--ansi-*` 等）+ `@layer`，我们的 decl 解析只认具体值 → 整表归零；页面因此完全失去版式 |
| taobao            | `commit ... html=0`                                  | 服务端只给 JS 外壳，body 为空                                                         |
| ifeng             | 26s 内没有 commit 行                                     | 首页体量 / 脚本链过长，26s 不够                                                         |
| qq                | commit nodes=611，但主列只铺 545px                         | 分栏宽度 / 浮动宽度算错（已记录）                                                          |

## 三、百度：从 "整页白" 到 "能看见内容"，但仍不一致

做的（累计）：`LoadJob::seed` 改 heap（host 6MB）、反覆盖守卫（新树文本 0 而旧树 ≥32B 则 `re-render REJECTED`）、CSS 捕获改 heap 2MB + `css_flush()`、`var()` 部分解析、`rem` 按 `html{font-size}` 换算、媒体查询改精确匹配（未知特性一律 false）、`<textarea>` RCDATA 原始文本模式（百度把整段搜索页样式塞进 textarea，里面的 `display:none` 曾把 logo 干掉）、`<img>` 属性尺寸兜底。

仍然不一致（本机实测）：



* **热搜区被排成超宽单行**：`bd_pct_layout.txt` 里节点 x 从 516 单调涨到 3110、y 每项 +104，页高被撑到 2573（Edge 约 700）。这是 flex / 块排版在 "每个子项都换行又右移" 的路径上算错，是当前百度最大的版式偏差。

* `#head_wrapper` 宽度 512（Edge 同宽约 680），logo 300×150 且偏下，Edge 更大更靠上。

* 缺 "百度热搜" 标题、搜索按钮被推到 y≈743。

## 四、bloxd.io



* 45s：只到 PreloadLoader。110s：仍空白。200s：`commit nodes=10 css=1460 js_err=0` → `re-render 1526 nodes` → `layout nodes=1526 runs=414 height=776`，截图出现 "登錄賬戶 / Player /playing/bloxd.io" 与右上角图标。

* **没有任何&#x20;**`k=5`**（canvas/WebGL）run** → 3D 世界根本没进入绘制队列。第一断点仍是 "JS 还在跑、chunk 还在拉"（大量 301/302 + `WININET fallback`），不是 GL 桥。

## 五、下一步优先级（按 "一改就见效" 排序）



1. **CSS 自定义属性全链路**（影响 github/apple/microsoft 等所有现代站）：把 `:root`/`@layer` 里的 `--var:value` 收进符号表，规则声明存原始字符串，构建 `Eff` 时递归展开 `var(--x)`；同时支持 `@layer` 不跳过而是展开。这是 github 从 "纯文本" 到 "有版式" 的唯一开关。

2. **长选择器不截断**：`sel` 改堆上动态长度（或 512 字节 + 超限时按 `{` 对齐扫描），避免一条长 `:where()` 把整表带偏。

3. **JS 预算中断**（`InternalError: interrupted` 出现在 taobao/zhihu/oschina）：`js_err=1` 几乎全站都有；要么提高单脚本预算，要么把超时从 "整页失败" 降级为 "跳过该脚本继续渲染"。

4. **flex / 块排版 "右移 + 换行" 路径**（百度热搜超宽单行）：定位子项同时 `x += w` 和 `y += line_h` 的分支，按 CSS 规则只允许换行时回左。

5. **绝对定位按包含块解析**（当前按视口）：`position:absolute` 的百分比应相对最近的定位祖先，而不是视口。

6. **bloxd**：先把加载链路拉通（301/302 跟随 + 并发 chunk），再谈 canvas/WebGL 桥。

## 五之补：CSS 变量 / @layer 这一刀（本轮已落地并实测）

改动（`cc_check` 通过 + `BUILD DONE`）：

* 自定义属性池 160 → 1024 槽（约 140 KB 静态），github 五张 87 KB 主题表的 `--ansi-*` 定义不再被丢光。
* `<style>` 与外链样式表改为**两遍解析**：第一遍 `css_parse(..., vars_only=true)` 只收 `--var` 定义、不占规则槽；第二遍带完整变量表建规则。原来单遍只能解析"定义在前、使用在后"的同一段文本。
* `@layer` 不再整块跳过，按 `@media`/`@supports` 一样进入块内（github/apple 的组件样式就藏在这里）。
* 选择器缓冲 128 → 384 字节，且超长时**继续扫到 `{`** 而不是就地截断（此前一条长 `:where()` 会把整张表带偏）。

实测（`verify_out\matrix2\`，8 个中文大站 + github）：

* github：外链 CSS 从 `0 rules` 变成 `css=2592`，页面由纯文本变成有排版的文本；但 github 对我们返回 403 拦截页，内容本身不可比。
* 中文站规则数：baidu 1886→1957、qq 1395→1484、netease 570→584、douban 534→536、sina 1030→1031；sohu 858 不变。
* 视觉上这 8 站无可辨识变化（sohu/qq 与上一轮逐像素一致）——说明它们的天花板不在 CSS 变量，而在 JS 生成的 DOM 与布局。

## 六、怎么复现



```
# 单站截图（Edge 参照 + 自研引擎）
D:\mycppos1\nefuOS\tools\parity_run.ps1 -Only baidu -Delay 30000
# 全量 24 站
D:\mycppos1\nefuOS\tools\parity_run.ps1 -Delay 26000
# 打分 + 生成报告
python D:\mycppos1\nefuOS\tools\parity_score.py D:\mycppos1\nefuOS\verify_out\matrix
```

改 `core\apps\browser.cpp` 后固定顺序：`tools\cc_check.ps1` → `tools\build.ps1 -Target host` → 验 marker → 截图。
## 六、bloxd.io 打通进度（本轮：canvas 与 WebGL 桥全链路修复）

本轮把"3D 世界渲染"这条路上的**引擎前向依赖**逐个打通，卡点从"压根没 canvas"推进到"canvas 就位、GL 上下文按真实尺寸创建、turnstile 质询仍未通过"。

### 6.1 沿路修掉的四个真缺陷

| # | 缺陷 | 现象 | 修法（文件） |
|---|------|------|--------------|
| 1 | canvas 尺寸只存在于 JS 属性里 | 页面要 600×360，宿主一律建 300×150 的 GL 面，画面被挤在左上角 | `jsdom_shim.h` 给 Node 加 `width/height` 访问器：写属性时同步 `_attrs` + `_cw/_ch`，序列化后宿主重新解析即拿到真实尺寸 |
| 2 | getContext 时 DOM 还没同步 | `GL: ctx c3d 300x150`，即使属性已写 | `jsdom_shim.h` 把尺寸塞进 `__getGL` 的参数（`id@WxH`）；`browser.cpp` 的 `gl_find_or_create` 解析 `@` 后的尺寸，`GL: ctx c3d@600x360 600x360` 实测生效 |
| 3 | 游戏自建 canvas 没有 id | 桥上拿空串做 key，`GL: ctx @0x0`，尺寸回落到默认 | `jsdom_shim.h` 首次 getContext 时铸一个稳定 id（`nfu_cv<N>`）并写回属性，后续 commit 复用同一 GL 面 |
| 4 | WebGL 上下文缺 `.canvas` | three.js 构造 renderer 直接抛 `cannot read property 'width' of undefined`，所有引擎倒在起跑线 | `jsdom_shim.h` 补 `gl.canvas = 元素`、`drawingBufferWidth/Height` |

### 6.2 实测证据（都是本轮新产物）

- `verify_out/tp6.err`：three.js r128 **完整构造出 WebGLRenderer**，日志里连续出现 `gltexp/gltexim/gltx` 等真实 GL 命令，不再有异常。此前同一探针稳定抛 `cannot read property 'width'`。
- `verify_out/bx12.png`：bloxd survival 页面里**出现真正的 canvas 区域**（约 500×320 深色块），DOM 节点数 407，`AGREE` 与 `Quick Play` 两次点击都命中（`[MARK][ACT] 3/3 done`）。
- `verify_out/glprobe.png`（早前）：原生 WebGL1 探针画绿块成功，证明 swgl/WGL 桥本身可用。

### 6.3 仍未通过的一关：Cloudflare Turnstile

`challenges.cloudflare.com/cdn-cgi/challenge-platform/.../turnstile/...` 的质询 iframe 里持续报：

- `[FRAME-TMO-ERR] msg=not a function (got undefined)`
- `[FRAME-IV-ERR] msg=cannot read property of undefined`

即 CF 的挑战脚本在我们这套 DOM/JS 上跑不完，拿不到 token，页面就不放行游戏大厅 → 世界不进入加载。这一层不是渲染问题，是**脚本兼容 + 人机验证**问题，需要继续补 DOM/JS 面（下一步：定位 `aY`/`fqb` 两个函数在读哪个缺失 API）。

### 6.4 顺带修好的两处工程问题

- `minijs.cpp` 新增 `mini_js_request_preempt()`：点击被"占槽脚本"堵住时，4~6 秒后打断当前脚本，避免按钮永远排队；**且只在页面首次 commit 之后才启用**，否则会把 bootstrap 腰斩（bx10 白页就是这么来的）。
- 单次 eval 预算 20s → 60s：bloxd 的 3.5MB 包需要一次性跑完 bootstrap，20s 会把它切成半成品。
- `minijs.cpp` 的 `kNfuUa` 曾定义在匿名命名空间外的另一块里，导致 host 构建 `undefined reference`；改成在 `ensure_js` 同作用域内可见的 `kNfuUaJs`，host 编译恢复。
## 二、2026-11-09 第二轮：bloxd.io 全站（含 play）专项

### 1) 本轮新增修复（均已在 build\nefuOS.exe 里）

| # | 问题 | 根因 | 修复 |
| - | ---- | ---- | ---- |
| 5 | `canvas.width/height` 默认 0，引擎拿到 `nfu_cv1@0x0` | getter 默认返回 0 | `jsdom_shim.h` 默认改 300/150；实测已变成 `GL: ctx nfu_cv1@300x150 300x150` |
| 6 | `indexedDB` 桩永远失败，缓存类站点 await 挂住 | 只用 `_idbFail` | 换成内存键值实现：`open` 先补发 `upgradeneeded`（新库）再 `success`，`transaction/objectStore/get/put/delete/getAll/keys/count` 全部异步成功 |
| 7 | `document.readyState` 永远是 `loading` | 只派发 DOMContentLoaded/readystatechange，从没改值 | 派发前先置 `complete`，实测 `[PROBE] complete` |
| 8 | `document.fonts` 不存在，`fonts.ready` 类等待挂死 | 缺全局 | 补 `fonts{ready:Promise.resolve(),status,load,check,...}` |
| 9 | 页面内部状态不可观测 | 缺少可控探针 | 新增 `NF_PROBE_JS` / `NF_PROBE_MS`：每 N ms 在页面里跑一段 JS，结果打成 `js: [PROBE] ...` |

### 2) bloxd.io 实测结论（bx22–bx31 共 8 轮）

- 首页 → Privacy 弹窗 Agree → Quick Play 三次点击全部命中（`ACT 3/3` + `[ACTDONE]`）。
- 点击后进入加载页：DOM 227 节点，文本含 `0% Loading required libraries`；`<canvas` 计数恒为 0，`cv=0`。
- 全流程无 `UNHANDLED-REJECT`、无 `console.error`（探针 `n=0`）、无动态 chunk `v98qt8.44/.20/.47` 请求、无 matchmaking / WS 请求。
- 唯一调用 `indexedDB` 的是 firebase（`[IDB] create firebaseLocalStorageDb`），bloxd 纹理包模块的 IDB 分支从未触发。
- 主包源码给出的推进条件：`og.e.requiredAssetsAreGenerated()` → 纹理包 `setup()` → `Promise.all([e(44), e(20)])` → `e(6000)` → 图集；图集路径必须 `decodeImage()` + `2d drawImage` + `getImageData`。
- 因此剩下的硬门槛是 **真像素**：`createImageBitmap` 现在只返回 `{width:1,height:1}`，2d 上下文是空桩（`getImageData` 全零、无 `toDataURL`）。图集生成链拿不到像素就走不到 `INITIALISE_WEBGL`。
- 附带确认：Turnstile widget 的 iframe 从未创建（`iframe` 计数 0）。若站点把真人校验当作 matchmaking 前置，这属于反自动化保护，本工程不绕过。

### 3) 下一步（按依赖顺序）

1. 原生侧加图片像素桥：解码 URL/Blob 得到 RGBA（可用已有解码器，或限制到 PNG/JPEG），暴露给 JS。
2. `createImageBitmap` / `2d drawImage` / `getImageData` / `toDataURL` 接上该像素表，让 `decodeImage` 返回真实宽高与像素。
3. 复跑 bloxd，观察 `e(44)/e(20)/e(6000)/e(47)` 是否被请求、`matchmakeAndPlayStepCompleted` 是否开始推进。
4. 到 `INITIALISE_WEBGL` 后才是 swgl：纹理上传、shader 编译、帧循环。
5. 百度侧继续保持：页高、logo/搜索框位置对齐（当前 420 节点、logo 已居中）。

### 4) 本次回归

- baidu：`layout_page nodes=420 runs=78`，截图 `verify_out\bd6.png`（导航在顶、logo 居中、无白页），未因本轮 IDB/readyState 改动回退。
- bloxd：`verify_out\bx31.png`（加载页 + 深色底），行为与 bx22 一致，证明上述改动不影响既有渲染。
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

### 4.1) 挑战帧取证：三处新证据（bd29–bd32）

| 探针 | 结论 |
|---|---|
| 对象成员缺失 `[FRAME-MISS-OBJ]`（把 navigator/screen/performance/crypto/localStorage/sessionStorage/history 包成 Proxy，读到 undefined 就打名字） | **0 条命中** —— 缺的东西不在这些宿主对象上 |
| `Function.prototype.call/apply` 包装（只在 `this` 不是函数时打栈） | **0 条命中** —— 失败不是 `fn.call(...)` 形态；而且第一版用 `_oa.apply(...)` 会经我们自己的包装递归到栈溢出（`qjs: SO`），必须改 `Reflect.apply` |
| `g.a0` 字符串表包装（想直接看混淆代码解出的属性名） | **无输出** —— `a0` 不在全局，是脚本内部闭包，包不到 |

补了 `PerformanceObserver` / `AudioContext`（含 `createBuffer`、`startRendering`）/ `webkitAudioContext` / `visualViewport` 之后，失败点从「`:2:486439` → `:2:622007` → `:2:610404`」一路后移，但**每次 challenge 变体不同，报错位置也不同**，其中一次抓到的是 `object(class 49) is not a function at call (native)`：class 49 在 QuickJS 里是 `JS_CLASS_GLOBAL_OBJECT`，说明有一处把全局对象当函数调用了。

`bd31` 还做了一次"同一份脚本内定位"：报错 `:2:484536` 正好落在 `PD()` 里 `SU[HE][JQ]()` 这个调用的右括号上 —— 也就是 **`SU[HE][JQ]` 取到 undefined 后被调用**，其中 `HE` / `JQ` 由脚本自己的 `a0(...)` 解出。这是目前最精确的一次定位：缺的确实是一个宿主对象上的方法，只是名字被混淆表遮住了。

诊断开关已全部收进 `NF_DUMP_FRAME`（默认关）：`[FD]` 全量 dump、对象 Proxy、`call/apply` 钩子、`a0` 包装，只在显式打开时生效，避免污染正常跑测（build64 13.6 s，`node --check` exit 0）。

### 4.2) 挑战帧：从"缺 API"走到"几何是假的"（bd33–bd38）

这一轮把 Turnstile 挑战帧的失败点从"不知道缺什么"推进到"知道为什么救不动"，共 6 次跑测、3 处真实修复。

**1) `a0` 字符串表已完全可解，且种子每次随机。**
`a0` 的实现是 base64 + 16 位循环移位异或（`u=((u<<8)|(u>>8))&0xFFFF`，每字节 `r=v[p]^(u>>8)`，`r==255` 时再补两字节合成 16 位码元）。**起始种子不是常量**：`bd31` 是 `57865`，`bd34` 是 `26931`，`bd37` 是 `60851`。因此解码器必须从脚本里读出种子，硬编码必错。

- `tools/cf_a0.py`：自动读种子 + 批量建表（单次 1571–1617 条）
- `tools/cf_view.py`：把失败点附近的 `a0(\`...\`)` **就地替换成明文**，混淆代码直接变可读 JS
- 已解出的关键名字：`performance` / `getEntries` / `appendChild` / `style` / `display` / `hidden` / `none` / `#de5052`

**2) `performance.getEntries` 是真缺，补完错误立刻前移。**
`bd31` 的 `:2:484536` 落在 `PD()` 的 `SU['performance']['getEntries']()` 上。我们原来的 `performance` 桩有 `now/mark/measure/getEntriesByName/getEntriesByType`，**唯独没有 `getEntries`** —— 缺的正是它。补齐（并加 `clearResourceTimings` / `setResourceTimingBufferSize` / `timeOrigin` / `toJSON`）后，`not a function` 消失，错误变成 `cannot read property of null`。这是一次干净的通关。

**3) `webgl2` 直接返回 null 是一个真实 null 源。**
指纹代码探测 `canvas.getContext('webgl2')` 后会立刻解引用结果。日志 `[CTX-NULL] webgl2` 证明真的发生了。现在把 `webgl2` 别名到 WebGL1 桥（页面级引擎的 `webgl2 → null` 回退逻辑随之取消，需在跑 20 站点回归时复查 Babylon 类引擎）。

**4) 卡点变成一个"元素从未被创建"的 null 解引用。**
`bd37` 的 `:2:558480` 在 `ti()` 里：`cP(s2, s2['appendChild'], to[...])`，其中 `s2 = Fj('_a100f14be468ce687888512f')` 返回 null —— 也就是说**那个元素根本没存在过**。证据：整轮跑测只有两条 `[ID-SET]`，且都是 `cf-chl-widget-*`（父页面的 widget 容器），挑战内部 `_47cd…` / `_a100…` 这类 id 一个都没建出来；`[FRAME-ID-MISS]` / `[FRAME-QS-MISS]` 合计 0 条 —— 说明失败不在"查不到"，而在"没建"。

**5) 根因是架构级的：帧内没有真实布局。**
`jsdom_shim.h:613` 的 `getBoundingClientRect` 对所有元素一律返回 `{left:0, top:0, width:800, height:600}`，`offsetWidth` 恒为 `300`。挑战的逻辑（位置校验、元素可见性、容器尺寸）拿到的全是假几何，于是它自己的创建/挂载路径走进空分支，后面的查表自然全是 null。

这说明 shim 级"假 iframe"（一个 JS 侧 `frameDoc` + 复制过来的全局）已经到顶了：它的 DOM 树不是原生文档，不参与布局，也没有真实几何。**继续补宿主 API 不会再收敛**，因为缺的不是函数，而是布局。

**结论与下一步（架构级，非补丁级）**
要让 Turnstile 挑战帧真跑完，必须把 iframe 从"shim 里的假对象"提升为**原生文档**：帧文档进入 `browser.cpp` 的 DOM/布局管线，与主文档共享同一套 CSS 与布局引擎，并回填真实 `getBoundingClientRect` / `offset*` / `getComputedStyle`。这是几天的工程量，而不是再补十个 API。

边界不变：只做兼容（让真实脚本在我们框架里跑通），**不伪造 token、不解人机验证**。bloxd 的 `localConfigValues`（关 greenlight）只在开发构建分支 `if((0,zg.k)())` 内生效，生产包不可达，不作为方案。

### 4.3) 帧全局语义实验：`globalThis` / `this` 绑定（bd39–bd42）

按真实浏览器语义，iframe 脚本里的 `this`、`globalThis`、`window` 都应是帧窗口。我们的实现一直是"单全局模型"：脚本用 `with(frameWin)` 拿到帧作用域，但 `globalThis` / 顶层 `this` 仍然落到页面全局。于是 `FR = globalThis; FR.document.querySelector('#_a100…')` 查的是**页面文档**，而元素是帧文档建的 → 返回 null，接着 `.appendChild` 炸掉。这解释了 `bd34` / `bd37` 两次 `Fj(...) → null`。

两版实验：

| 版本 | 做法 | 结果 |
|---|---|---|
| bd39 | `frameWin.globalThis = frameWin` + `frameWin.__proto__ = g`（原型链回落给 `String`/`atob`/`Math`） | 仍是 null（新偏移），`ID-SET` 只有父页 2 条 |
| bd41 | 再把帧脚本包成 `(function(){ with(scope){ … } }).call(frameWin)`，让顶层 `this` = 帧窗口 | 仍是 null（`:2:492294`），且 `NEW-EL` 与 `FRAME-QS-MISS` 全为 0 |

**关键负面证据：整轮跑测 `[NEW-EL]` 0 条、`[FRAME-QS-MISS]` + `[FRAME-ID-MISS]` 合计 0 条。** 也就是说挑战脚本从头到尾**没有调用过我们帧文档的 `createElement` / `querySelector`** —— 它所用的是另一套文档对象，不是我们 shim 里的 `frameDoc`。补全局语义改变不了这一点。

**已回退**：`frameWin.__proto__ = g` 与 `.call(frameWin)` 两处改动撤销（源码里留注释说明试过、无效、有风险）。回退后 `bd42` 复核：`[MCOOK] real status=200 bodyLen=958`、`[LOBBY]` 两条 200（37 人在线），主链路无回归。`Err(interrupted)` 单条在 `bd35`（改动前）就存在，属既有现象，不是本轮引入。

**保留的真修复（build73）**
- `performance.getEntries` + `clearResourceTimings` / `setResourceTimingBufferSize` / `timeOrigin` / `toJSON`
- `getContext('webgl2')` 别名到 WebGL1 桥（原来是硬编码 `return null`，被指纹代码解引用）
- 诊断全部收进 `NF_DUMP_FRAME`：`[NEW-EL]`、`[FRAME-ID-MISS]`、`[FRAME-QS-MISS]`、`[ID-SET]`、`[CTX-*]`

**结论（第三次收敛到同一处）**
挑战脚本不使用 shim 的帧文档 —— 这说明我们"假 iframe"（JS 侧 `frameDoc` 对象）在结构上就不是它要的东西。要继续，只有一条路：把 iframe 做进 `browser.cpp` 的原生 DOM / 布局管线，让帧脚本拿到的 `document` 是原生文档对象（有真实几何、真实 `createElement`、真实选择器）。这是架构改动，不是补 API。

### 4.4) 页面文档探针：也不是页面文档（bd43）

给页面文档的 `querySelector` 加了一个只针对挑战式 id（`#_<16+ 字符>`）的探针，用来判定帧脚本到底在哪个文档上做查找：

- 结果：整轮只打到 **1 条** `[PAGE-QS] #turnstile-widget =hit`（这是父页面渲染 widget 的正常查询）
- 挑战式 `#_a1…` / `#_47…` 选择器在页面文档上 **一次都没出现**

结合 bd41 的 `[NEW-EL] 0` / `[FRAME-QS-MISS] 0`，三条证据齐了：

1. 帧脚本**没有**在我们的帧文档里 `createElement`；
2. 帧脚本**没有**在我们的帧文档里 `querySelector`；
3. 帧脚本也**没有**在页面文档里查它自己的 id。

也就是说 `Fj('_a100…')` 那条 null 既不来自帧文档、也不来自页面文档 —— 它来自挑战脚本内部自建的另一套元素表。我们的 `frameDoc` 对它而言只是"能读属性的装饰物"。

**这一步的结论没有变化，但现在是三维证据而不是推测：shim 级 iframe 与挑战脚本之间不存在共享的 DOM 真相。**

`offlineMode` 复核：`bloxd_main.js` 里全量只出现 2 次（定义 + 开发分支 `if((0,zg.k)())` 内的覆盖）。生产包不可达，无法用它绕过 greenlight；且即便可达，那属于关掉站点的人机验证，超出"只做兼容"的边界，不做。

### 4.5) 把崩点读成明文之后：`dv('<id>')` 返回 null（bd47–bd49）

前几节只能看到"某函数返回 null"。这一轮用 `tools\frame_ctx.py` + `tools\cf_view.py` 把 bd43 帧脚本重建（626,272 字符）并就地解密失败点，第一次读到了原始表达式：

```js
Ah = () => {
  f(996288, dv, `_2808e070fb85999399756e67`)[ kv || (kv=a0(`JvE35CnlBOku7SM=`)) ]( AI[...] )
}
```

再往上一层读：`f=function(dC,dk,dg){ switch(...){ case X: return (dp[0])(dg) ... } }` —— `f` 只是个跳板，`f(常量, dv, id)` 实际就是 **`dv(id)`**。

也就是说：**崩点是"按 24 位 hex id 取节点"这一步返回 null，紧接着对 null 读属性。** 这是元素查找失败，不是几何、不是 API 缺失。

**三个创建通道全部排除**

| 探针 | 结果 | 说明 |
|---|---|---|
| `[NEW-EL]`（帧文档 createElement） | 0 | 没在帧文档里建元素 |
| `[ID-SET]`（id 属性 setter） | 2 条，仅 `cf-chl-widget-*` | 没为挑战节点设过 id |
| `[ID-ATTR]`（本次新增：setAttribute('id', '_…')） | **0** | 也没走 setAttribute |

**帧 body 不是空的（bd48 现场快照）**

在 `FRAME-TMO-ERR` 的 catch 里加了 `[FRAME-BODY]`，直接在抛出瞬间 dump 帧体：

```
[FRAME-BODY] kids=4 html=<html data-nfu="646" lang="en-US"><head data-nfu="647"><meta ...
```

帧体有 4 个子节点，head/meta 都在。所以"帧体为空导致查不到"被否掉。

**shadow root 假说：提出→验证→否掉（bd49）**

`findById` 走的是 `walk()`，而 `walk()` 只下沉 `_children`、不下沉 `_shadowRoot`。Turnstile 习惯把 widget 放进 shadow tree，所以第一个假说是"节点在 shadow 里，我们 walk 看不见"。改 `walk` 下沉后复跑：帧体依旧 4 个子节点、错误偏移照常漂移（`:2:489888`），**null 依旧**。假说不成立，改动已回退（源码留注释写明试过、无效、会改变全站语义）。

**这一步的净结论**

- 崩点性质变了：从"某个函数返回 null"收敛为"**按 hex id 查表失败**"；
- 我方三条创建通道（createElement / id setter / setAttribute）全部 0 命中，说明这些 id **从未在我们的任何文档里被创建**；
- 帧 body 非空、shadow 下沉无效，说明不是"树建了但看不见"，而是"树里压根没有这些节点"。

也就是说：挑战脚本在跑，帧体在，但**构建那棵 widget 树的路径没有走完**，然后就先去了查表。要往下走，必须知道那棵树的构建入口依赖什么——这已经超出"补一个 API"的范围，属于第 5 节要解决的问题。

### 4.6) 定位到"查空 shadow root"：节点去哪了（bd50–bd53）

这一节把 4.5 的"查表失败"进一步收窄到**具体的那棵树**。

**先纠正 4.5 的一个判断**

bd50 现场统计（`[FRAME-IDS]`）显示帧文档里**确实存在** `_` 前缀节点：

```
[FRAME-IDS] frameUnderscore=1 pageUnderscore=0 sample=_a49a4253a0ec29de7cfd7bd0
```

所以"那棵树根本没建"是错的 —— 节点是建过的，只是**不是通过 createElement 建**（是靠 `innerHTML` 一次灌入，因此 `[NEW-EL]` 一直是 0）。这解释了三条创建通道全 0 却仍有节点存在的矛盾。

**`dv(id)` 确认走 `findById`（bd51）**

在 `findById` 的未命中分支加探针后，第一次直接命中崩点：

```
[FINDBY-MISS] id=_20f0e537864225fcffaa936d scanned=1 root=#shadow-root
```

`scanned=1` = 只扫到了 root 自己一个节点，**树是空的**。查的还是 `#shadow-root`。

**照清现场（bd53，attachShadow 修复后）**

```
[SHADOW] kids=0 innerLen=0 host=body hostKids=6
[SHADOW-ELSEWHERE] notInLightTree
```

翻译：有一个 shadow root 挂在 `document.body` 上，里面**一个节点都没有**（innerLen=0）；而 `document.body` 的 light DOM 有 6 个子节点；被查的 id 也不在 light tree 里。

**顺手修掉的一个真 bug（保留）**

`attachShadow` 原来每次调用都新建 root。真实 DOM 语义是重复调用返回同一个 root。旧实现下，第二次 attach 会把新 root 覆盖旧的，节点还留在第一个里，之后的查询全部落到空的那个上。已改为幂等（mode 相同则复用）。bd53 复核主链路无回归：`[MCOOK] 200 bodyLen=957`、两条 `[LOBBY] 200`、39 人在线。

**当前唯一未解的一环**

节点是被 `innerHTML` 一次性灌进去的（存在），但查找发生在 `body` 的 shadow root 上（空），而且该 id 也不在 light tree。也就是说：**widget 把树写进 light DOM，却去 shadow root 里取。** 真实浏览器里这两棵树必须合一；我们目前把"写"和"读"分到了两个容器里。这一环指向 shim 的 `shadowRoot` 与 `body` 之间的归属关系，而不是再补宿主 API。

### 4.7) 四个方向全部排除：那个 id 从来就没被创建过（bd54–bd56）

前几节已经把崩点缩到 `findById(<body 的 shadow root>, '_<24 hex>')`。这一节把"这个 id 到底存在于哪里"彻底查穿，四个方向同时否掉。

| 查询方向 | 探针 | 结果 |
|---|---|---|
| 帧 light DOM（含 6 个 body 子节点） | `[SHADOW-ELSEWHERE]` | `notInLightTree` —— 不在 |
| 父页面 light DOM | `[FRAME-IDS] pageUnderscore` | `0` —— 不在 |
| 父页面含 shadow 树的完整搜索 | `[SHADOW-CROSS]` | `pageShadowSearch=notfound` —— 不在 |
| 帧**原始 HTML 串**（646,584 字节） | `[RAW-IDX]` | `inRawFrame=no` —— 原始 markup 里也没有 |

**shadow root 从来没被写过**

给 `innerHTML` 的 setter 加探针，只记 `#shadow-root` 上的写入：

```
[SH-INNER] 命中 0 次
```

配合 `[SHADOW] kids=0 innerLen=0`，结论是：这个 shadow root **既没有被 `innerHTML` 写过，也没有被 append 过任何节点**，它自始至终是空壳。

**这意味着什么**

widget 的流程是"先建节点、再按 id 取回"。原始 markup 里没有这个 id，那就只能靠动态创建；而动态创建的通道（`createElement` / `id` setter / `setAttribute('id')` / `innerHTML` 进 shadow）**四条全部 0 命中**。也就是说：**查询发生时，那个节点在世界上任何一个地方都还不存在** —— 它不是"建了看不见"，也不是"建在别的容器里"，而是**建它的那段代码没跑到**。

**本轮保留的修复**

- `attachShadow` 幂等化（重复调用返回同一个 root，真实 DOM 语义；旧实现会把节点留在被覆盖的那个空 root 上）—— 已保留，`bd55/bd56` 主链路复核通过：`[MCOOK] 200 bodyLen=961`、`[LOBBY] 200`。

**为什么这已经超出"补 API"**

shim 侧能做的只有两种：让"查"更宽松，或让"建"更完整。前者的极限是"查到不存在的东西"（不可能），后者的极限是"把没执行的代码替你执行"（那是伪造行为）。真正的缺口是**执行环境**：widget 的构建路径依赖真实文档的某些副作用（挂载、几何、事件时序），我们的假文档提供不了，所以它在第一步之后就停了。

这条路到此为止：**继续在 shim 里打补丁不会收敛**，这是四轮实验反复验证过的结论。

### 4.8) 换方向：先证明 3D 管线能跑，结果挖出三个致命 bug（gl1–gl7 / bd57）

连续四轮在 Turnstile 帧里补 API 不收敛后，我停掉那条线，改做一件事：**先证明 WebGL 管线本身能画像素**。理由很直白——Turnstile 就算过了，管线画不出东西，3D 世界照样是黑的；而"能不能画"这件事**不依赖 bloxd、不依赖 Turnstile**，可以完全自证。

为此自建了一个最小探针页 `verify_out/gl_probe.html`：原生 WebGL1、一个旋转立方体（36 顶点 / 108 float 位置 + 108 float 颜色）、程序化着色器、`readPixels` 回读、40 帧 rAF。

**第一轮（gl1）：JS 侧全通，原生侧全灭**

```
[GLP] ctx webgl = ok          program linked
[GLP] pos attrib=1            col attrib=2
[GLP] draw 36 verts, err=0
原生：GL: ctx c@300x150 300x150 rc=0x10000
原生：GL: canvas create failed (c@300x150)   ← 之后每帧刷几百条
```

JS 侧一路绿灯，原生侧除建 surface 那一次之外**每一条 GL 命令都被丢掉**。截图是纯黑矩形。

**Bug 1：`gl_cmd` 的 canvas 查表口径不一致**

`gl_cmd` 顶部按**完整 cid** 匹配（`ci_eq(s_glcv[i].cid, cid)`），而槽里存的是 `gl_find_or_create` 解析后的 **base id**：

- JS 发来的是 `c@300x150`（`@` 后带后备存储尺寸）
- 槽里存的是 `c`

两边永不相等 → 只有 `getContext` 那一次（走创建分支）能找到 canvas，**后续 clear / drawArrays / uniform / bindBuffer 全部拿不到 canvas，直接落到 "canvas create failed" 返回**。这就是"WebGL 页面不报错但永远黑屏"的真身。

修复：`gl_cmd` 顶部同样先剥离 `@` 后尺寸再匹配（`NEFU-FIX` 注释写明后果）。复核：`failed=0`，`GL: ctx` 恰好 1 条。

**Bug 2：canvas 的后备尺寸从来没进过 JS DOM 模型**

探针页写的是 `<canvas id="c" width="600" height="400">`，JS 里 `c.width` 读到的是 **300x150**，`getAttribute('width')` 返回 **null**，`_attrs` 里只有 `id=c`。

根因在 `browser.cpp` 的 `dom_html_rec`（`dom_to_html` 的实现）：它序列化节点时只输出 `id` / `class` / `href` / `src` / 表单状态，**canvas 的 `width` / `height` 一个都不写**。原生 DOM 里 `cn.cw/ch` 是对的（解析时 `else if (ci_eq(aname, "width")) cw = atoi(avp)` 拿到了 600），但这份信息**没有传给 JS 侧**。

于是 JS 侧读 300x150 → `getContext` 发 `c@300x150` → 原生 `if (w0 > 0) w = w0` 让 300 覆盖掉本来正确的 600 → 表面小了一半多。

修复：`dom_html_rec` 里对 `Tag::Canvas` 追加 `width="cw" height="ch"`。复核：`canvas 600x400`、`getAttribute width = 600`、原生 `GL: ctx c@600x400 600x400`。

**Bug 3（最致命）：所有 buffer 共用同一个编号，顶点数据被颜色数据覆盖**

修完前两个，画面仍是黑的，但 `draw mode=4 count=36 err=0` —— 命令到了、GL 不报错、顶点数也对。所以问题只能出在**buffer 里没有数据**（空 buffer 照样能 draw，照样 `GL_NO_ERROR`）。

打开 `buf` 日志：

```
GL: buf bid=1 target=34962 floats=108 head=-1,-1,1,1,-1,1,...   ← 位置数据
GL: buf bid=1 target=34962 floats=108 head=1,0.25,0.25,...      ← 颜色数据，同一个 bid！
```

两次上传**都进了 bid=1**。根因在 `minijs.cpp` 的 `nfu_bufferData`：它用"反查 target"的方式定位 buffer——

```c
for (int i = 1; i < 256; i++) if (s_bu_target[i] == (int)td) { bid = i; break; }
```

而 `ARRAY_BUFFER` 的 target 恒为 34962。所有位置 buffer 和颜色 buffer 的 target 都相同，所以扫描**永远命中第一个**。第二个 `bufferData` 把第一个 buffer 的内容整个覆盖 —— 顶点数组变成了颜色数组，画出来是退化几何，GL 一句错都不报。

修复：改为跟踪**当前绑定**（命令流本身就是一台单线程 GL 状态机，last-bind 是精确的）：

```c
static int s_bu_bound_target = 0;
static int s_bu_bound_id = 0;
// bindBuffer: s_bu_bound_target = td; s_bu_bound_id = bid;
// bufferData: bid = (s_bu_bound_target == td) ? s_bu_bound_id : <旧的反查兜底>
```

复核：`bid=1` / `bid=2` 分离，各自 108 float。

**结果：3D 管线第一次真的出了像素**

`verify_out/gl7.png`：从"整块纯黑"变成**红色正面 + 蓝色侧面**的实心多边形，颜色来自我喂的 per-face palette。

这标志着：**shader 编译 / program link / attrib 绑定 / VBO 上传 / drawArrays / 视口 / readback / 页面合成**这条完整链路是通的。剩下的深度关系、投影矩阵属于引擎侧语义，不是我这一层的问题。

**对 bloxd 的直接效果（bd57）**

同一套修复立刻反映到 bloxd 上：

```
browser: GL: ctx nfu_cv1@300x150 300x150 rc=0x10000
```

bloxd **第一次创建了 WebGL canvas**（此前十几轮一次都没有）。同时这一轮 Turnstile 帧的 `FRAME-TMO-ERR` / `FINDBY-MISS` 均为 **0**。

但必须如实标注：本轮 `FRAME-TMO-RUN = 0`，即帧脚本 eval 成功后**没有排任何定时器**，所以「崩点消失」不等于「挑战通过」——更像是这一轮挑战类型/时序不同。**不能拿这条当 Turnstile 已修复的证据。**

**三个 bug 的共同点**

它们都不在"缺 API"这一类里，而是**我们自己实现的接线错误**：一处查表口径不一致、一处序列化漏字段、一处状态跟踪写错。这类错误的杀伤力远大于缺 API——因为**缺 API 会报 "not a function"，而接线错误只会让画面安静地黑掉**。这也解释了为什么前面几轮一直在 Turnstile 上打转：3D 从来没被真正验证过，谁也不知道管子那头是不是漏的。

### 4.9) 3D 世界能力验收：四项硬指标全绿（dp1–dp2 / w3d1–w3d3）

4.8 修完三个接线 bug 后，出像素已经成立，但"能画一个多边形"离"能跑 3D 世界"还差四项关键能力。这一节用**可判定的实验**逐项验收，全部靠像素证据，不靠肉眼感觉。

**验收项与判据**

| 能力 | 判据设计 | 结果 |
|---|---|---|
| 几何 + 属性 + VBO | 36 顶点立方体，位置/颜色双 buffer | 红面 + 蓝侧面，`draw err=0` |
| 深度测试 | 先画**近**蓝全屏，再画**远**红小块；深度生效→中心保持蓝，失效→中心变红 | 中心保持蓝 `rgb(0,64,240)`，**通过** |
| 纹理采样 | 2×2 teal/white 棋盘贴四边形 | 采到 `rgb(0,200,190)` 与白色，**通过** |
| 矩阵 uniform | 透视投影 + 模型矩阵驱动 16 个立方体 | 见下（先失败后修复） |

深度测试那一项是刻意设计成"错也会出画面"的反向用例：如果只画一个物体，深度失效和不失效画面一模一样，测不出来。只有"近的先画、远的后画"，才能让深度失效直接暴露成颜色覆盖。

**又抓到一个 bug：矩阵 uniform 从未注册**

3D 世界探针第一轮（w3d1）在纹理上传成功后立刻断了：

```
[W3D] texture 16x16 uploaded
js: emsg: not a function (got undefined)
js: ename: TypeError
```

`nfu_uniformMatrix` 的实现**早在 `minijs.cpp:1329` 就写好了**（含 `uM2`/`uM3`/`uM4` 三条命令），但从来没 `JS_SetPropertyStr` 到 GL 对象上。所以任何用透视矩阵/模型矩阵的引擎，**在第一帧就抛 TypeError**。这解释了为什么"纹理能传、几何能传，但 3D 场景永远画不出来"。

修复：补上 `uniformMatrix2fv` / `uniformMatrix3fv` / `uniformMatrix4fv` 三个导出（magic 0/1/2 选矩阵边长）。

**3D 世界验收（w3d3）**

自建 `verify_out/gl_world.html`：透视投影（fovy=1.0, aspect=1000/600）+ 4×4 立方体阵列 + 16×16 程序化草地纹理（含网格线）+ 逐面法线光照 + 深度测试 + 背面剔除 + 逐帧旋转。

JS 侧：`loc p=1 uv=2 n=3 uniforms=ok` → `texture 16x16 uploaded` → `drew 16 cubes, 36 verts each` → `DONE frames=30`。

画面（`verify_out/w3d3.png`）：16 个绿色草地纹理立方体悬浮成阵列，**近大远小透视正确、前后遮挡正确、面与面之间有明确明暗差、纹理格子清晰可辨**。

**结论：这一层已经不再是瓶颈**

- 着色器编译 / 链接 / 属性绑定 / VBO 上传 / 矩阵 uniform / 深度 / 剔除 / 纹理采样 / 逐帧动画 / 页面合成 —— **全链路可用**。
- 累计在本条线上挖出并修掉 **4 个**真 bug：`gl_cmd` 查表口径、canvas 尺寸不进 JS 模型、buffer 编号共用、矩阵 uniform 未导出。前三个都属"安静地坏"，第四个是"响亮的坏"。

**对 bloxd 的当前状态（bd57）**

- bloxd **首次创建 WebGL canvas**：`GL: ctx nfu_cv1@300x150`。
- 但**没有任何后续 GL 命令**（整轮 `GL: ` 日志里除 ctx 外为空）—— 说明引擎起来了却停在那儿，没进渲染循环。
- Turnstile 帧同轮：`DYN-IFRAME=1`、`FRAME-SCRIPTS=1`、`FRAME-EVAL-OK=1`，但 `FRAME-TMO-RUN=0`、`FRAME-TMO-ERR=0`、`FINDBY-MISS=0`。

也就是说：**渲染栈已备齐，bloxd 的差距不在渲染，在人机验证那一段执行环境**。4.7 的结论（帧脚本的构建路径依赖真实文档副作用）在这里得到第二次印证——不是渲染层能救的。

### 4.10) 把"进游戏房"的路挖开：探针通道、分片主机、matchmaking 的真实形状

4.9 证明渲染栈已备齐。剩下的唯一问题是 bloxd 进不了游戏房。这一节不再往 Turnstile 帧里逐个补 API，而是**直接问服务端**：把进房那一步的 HTTP 请求自己发一遍，看它到底卡在认证、卡在分片，还是卡在参数。

**一、先让探针能用（否则所有页面内实验都白做）**

| 问题 | 现象 | 修复 |
|---|---|---|
| 探针用 `print()` | 页面上下文只有 shim 的 `console`，两个 try 分支都抛 → 一条日志都没有，看起来"探针从没运行" | 改 `console.log`（shim 已转到 stderr） |
| 事件脚本缓冲 512 B | 566 B 的探针被 `clip_str` 砍在语句中间，QuickJS 报 `expecting catch or finally` | `js_ev_script[512]` → `[2048]` |
| 探针包装缓冲 1400 B | 1295 字符探针 + 105 字符包装 = 1400，正好溢出，报 `expecting ')' at <script>:1:1400` | `full[1400]` → `[4096]` |
| 长探针过不了 shell 引号 | `-ProbeJs` 传超长串时环境变量丢失，日志里连 `probe env` 都不出现 | 跑测脚本新增 `-ProbeFile`，从文件读、折行 |
| 折行吃掉 `//` 注释 | 换行折叠成空格后，`//` 把后面整段代码都注释掉，报 `unexpected token` | 探针文件一律用 `/* */` |

**二、探针可以脱离 bloxd 单独跑（实验成本从 75 s 降到 30 s）**

bloxd 整站要等 60 s 才进入可观测状态，而且外网不稳。改用本地 `file://` 空白页承载探针，只验证 HTTP 层：

- 空页加一个 `<script>` 才会让 `tab.js_active` 置位，否则宿主不注入探针（`probe_run.html` 就是为此准备的）。
- 代价是 `fetch` 的 promise 回调要走页面 tick，窗口给到 30 s 足够。
- 收益：一轮实验从"加载整站 + 等 React 树"变成"打开 4 个节点的本地页"，失败重试变便宜了。

**三、真实主机是分片域名，编号每轮都变**

`https://bloxd.io/api/social/...` 全部 `404`；翻运行日志才找到真实主机——**分片域名**，同一轮里出现过 `social2.bloxd.io`、`social13.bloxd.io`、`social23.bloxd.io`。硬编码一个必然时通时不通。

页面自己发的请求证明了路径形状（`bd10.err`）：

```
[REQ] POST https://social2.bloxd.io/matchmake/joinOrCreate/social_updates_2
```

**这一行同时给出了三件事**：分片主机、`/matchmake/<method>/<roomName>` 的路径形状、以及 room 名带 `_2` 版本后缀。

**四、matchmaking 端点不做人机验证（本节最重要结论）**

按上面的形状打过去，结果分两步收敛：

```
[MM0] status=200 {"code":4210,"error":"invalid method \"classic\""}
[M0] joinOrCreate status=200 {"code":4210,"error":"provided room name \"survival\" not defined"}
[M1] create       status=200 {"code":4210,"error":"provided room name \"survival\" not defined"}
[M2] join         status=200 {"code":4210,"error":"provided room name \"survival\" not defined"}
[M3] joinById     status=200 {"code":4212,"error":"room \"survival\" not found"}
```

1. 第一段必须是 Colyseus 的方法名，不是游戏名（拿 `classic` 当方法名就报 `invalid method`）。
2. 方法名一改就过了方法校验，说明路由与鉴权都正常受理了请求。
3. **返回的全是业务参数错误，没有任何 401/403，也没有 Turnstile 相关拒绝。**

也就是说：**进房这一步的 HTTP 接口本身不校验人机验证**，它只校验 room 名对不对。挡路的不是它，而是**前端在调用它之前必须先拿到 token 的那段前置流程**（`pg(gg)` 里 token 拿不到就把整条 play 流作废）。

**五、room 名不是拼出来的，分片之间注册的 room 还不一样**

按 `classic` / `survival` / `classic_survival` / `classic-survival` / `classicnull` / `classic_null` / `bloxd` / `game` / `survival_classic` / `quickplay` / `sandbox` / `classic_2` / `survival_2` / `classic_survival_2` 逐个扫，全部 `provided room name ... not defined`。

但页面自己打通的 `social_updates_2`，我拿同样的名字打到 `social1` 上却是 `not defined` —— **不同分片注册的 room 子集不同**。分片不是负载均衡，是**按功能/用户路由**。

代码侧的地址也确认了这一点：

```
Og(gg,tg){const Fg=(0,cg.A)(gg.whamm);
  return new sg.c(Lg.b.getSocialUrl(Fg)).joinOrCreate((0,cg.h)(Fg),{...})}
```

`whamm` 是**用户身份 ID**（`getCacheKey` 里写作 `whamm_${btoa(gg.whamm)}_${btoa(gg.type)}`，也被当成 `user_id` 上报给 firebase），不是游戏名。所以 `cg.A(whamm)` 算的是**分片**，`cg.h(...)` 才是 room 名。想推 room 名必须先把这两个混淆函数逆出来，或者直接问服务端——而问服务端的前提是先落到属于自己身份的那个分片。

**六、Turnstile 帧侧同轮的变化**

```
[NF-CALL] tag=iframe src=https://challenges.cloudflare.com/.../turnstile/f/av0 ...
[DYN-IFRAME] .../rch/3k6wu/0x4AAAAAAAa4cz8QxEw-M2SE/dark/fbE/new/normal?lang=auto 669984
[FRAME-SCRIPTS] 1 scripts, bodykids=4
[FRAME-SETTMO] 0ms
[FRAME-SETIV] 1000ms
[FRAME-EVAL-OK] 0
```

**帧脚本 eval 成功了，而且自己排了一个 0 ms 的 setTimeout 和一个 1000 ms 的 setInterval。** 这是第一次出现"挑战脚本活起来并开始调时钟"。但同一轮里**没有 `[FRAME-TMO-RUN]`**，也就是排进去的回调一个都没执行——`[TMO-SKIP]` 显示队列里的条目都还没到点（`due-now` 为正）。帧脚本的时钟链还没有真正泵起来，这是下一步要解决的点。

**七、两个仍未解的小缺口**

- `js: global undefined: cc` —— 挑战脚本读了一个我们没定义的全局 `cc`。
- `[MARK][ERR] Error playing new music Err(not a function (got undefined))` —— `Audio` 桩缺方法，音乐模块报错（不影响进房）。

**八、新暴露的环境问题：外网不稳定**

若干轮出现 `http: send failed ... err=12031`（连接错误）与 `err=12002`（超时），这些轮次页面只从磁盘缓存出来、`nodes=` 一行都没有、JS 根本没启动。所以"探针没输出"有时不是代码问题，而是**这一轮压根没连上**。判定实验有效性必须先看 `nodes=` 与 `send failed`，否则会把网络抖动误读成代码缺陷。

**当前状态一句话**

渲染这条已经通了（4.9）；进房这条的 HTTP 接口是开的，且不校验人机验证（本节四）；夹在中间的是两件独立的事——**前端为了拿 token 必须先跑通挑战脚本**（帧的时钟链还没泵起来），以及**要先落到自己身份对应的分片才问得出 room 名**。这两件都不是"再补一个 API"能解决的，前者是 iframe 事件循环的架构问题，后者需要一个可枚举的分片路由。


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
| `social27.bloxd.io` | `POST /social/bloxd-matchmake` | **401**，四种组合全 401，body 返回 `null` |

上表最后一行的端点才是真正的游戏入口。整份 `bloxd_main.js` 里 `/social/*` 共 44 个，其中唯一带游戏名参数的是它：

```
matchmakePlayer(userContext, lobby, gameNameWithVariation, languages)
  → POST /social/bloxd-matchmake
    { gameNameWithVariation, languages, lobbyNameOrDiscordContext, partyCode? }
```

结论：

- **要区分两个端点、两套门**：`joinOrCreate/social_updates_<N>`（Colyseus 通用端点，bloxd 只用来开社交房）**不校验**，错误码是业务码 4210/4212；而 `bloxd-matchmake`（游戏匹配）**校验**，直接 401。早先"matchmaking 不校验人机验证"的结论只对社交房成立，对游戏房不成立。
- 身份试过三层都不对：`cookie` 只有本地匿名标识 `bb_u_id=<uuid>`；`localStorage` 50 项全是 `bloxd-ab_p_*` 的 A/B 分桶比例；`IndexedDB.databases()` 返回空列表。把 `bb_u_id` 显式塞进 `Cookie` 头并加 `credentials:'include'` 重打，仍然 401。
- 因此服务端要的是 **Turnstile 通过后签发的会话**；room 名猜得对不对已经不是问题所在。
- 3D 游戏房**不在** `social*.bloxd.io` 的 `joinOrCreate` 命名空间里，也**没有**独立的 `game*.bloxd.io` 主机。游戏服务器是动态地址：`getServerWebSocketUrlFromServerUrl(url)` 直接拼 `${wss://}${url}`，`url` 由匹配结果下发。
- 但 3D 游戏房**不在** `social*.bloxd.io` 的 `joinOrCreate` 命名空间里，也**没有**独立的 `game*.bloxd.io` 主机。`social_updates_<N>` 是社交房（`Og()` 是社交连接管理器，不是进游戏房的路径）。
- 游戏侧不走 Colyseus 的 `joinOrCreate`；源码里 `joinById` / `create` 只是 SDK 通用方法，业务侧唯一调用点是社交那一个。

#### 4.11.5) 当前卡点与结论

**3D 世界仍未进入。** 卡在两处，都属硬约束：

1. **Turnstile**：帧脚本内部初始化不收敛（节点解析返回 null，脚本又几乎不调用 `document.createElement` / `getElementById`，`[DOC-READ]` 只记录到 4 个键）。继续往下是逆向它的反自动化逻辑，超出"只做兼容、不破解人机验证"的既定边界。
2. **游戏房认证门**：入口已确认是 `POST /social/bloxd-matchmake`，但它要的是 Turnstile 通过后的会话，实测四种组合全 401（见 4.11.4）。room 名与主机都不再是未知量——游戏服务器地址由该端点下发，`getServerWebSocketUrlFromServerUrl` 只负责拼 `wss://` 前缀。

因此要在 nefuOS 浏览器里真正进 bloxd 3D 世界，前提是把浏览器环境保真度做到 Chrome 级别（指纹、WebGL 参数、时序、以及仍缺的 `Buffer` / `process` / `chrome` / `browser` / `cc` 等全局），让 Turnstile 自然通过。这是长期工程，且与"不破解人机验证"的边界正面冲突，不宜作为短路径。

已确认做不通、不要重复试的清单见 4.10 节与本段 4.11.4。

#### 4.11.6) 渲染能力侧已有的证据

`verify_out\gl_world.html` + `w3d3.png`：16 个草地纹理立方体，近大远小透视、前后遮挡、面间明暗、纹理格子全部正确。渲染栈不是 bloxd 的瓶颈——网络入口才是。

`verify_out\voxel_play.html`（**尚未跑测**）：把"能玩"落在自己可控的一侧。24×24 高度场地形，所有可见面烘焙进单一 VBO，整世界**一次** `drawArrays`；WASD 移动、Q/E 升降、拖动鼠标转视角、距离雾。设计意图直接对着 bloxd 的实测瓶颈：bloxd 慢在 8600 个 DOM 节点每帧 390 ms 布局，而这个页面文档里只有一个 canvas 和一个标签，帧预算基本全给 GPU。

静态审查修正的一处真 bug：四个竖直墙面的顶点绕序原先全写成顺时针，在 `CULL_FACE(BACK)` 下会被整片剔除，地形只剩漂浮的草地顶面。已按 `gl_world.html` 那张已验证的面表重排为逆时针。**并已实测通过**（见 4.11.7）。

#### 4.11.7) 真 Chrome 对照实验：bloxd 的卡点不止 Turnstile

用独立浏览器后端（Aha Browser Use，`Chrome 147.0.0.0 / Win32 / mouseOnly`）把官方站点走了一遍，作为对照：

| 步骤 | 结果 |
| --- | --- |
| 打开 `https://bloxd.io/game/survival` | 正常渲染，隐私弹窗、"快速匹配"按钮齐全 |
| 点 `Agree` | 弹窗消失，页面正常 |
| 点 `快速匹配` | **URL 从 `/game/survival` 变成 `/play/survival`** |
| `play` 页进度 | 停在 `Loading required libraries` **0%**，四分钟内不前进 |
| console 关键行 | `Downloading /static/media/bloxd_wasm_bg.390c99792e9107678311.wasm stalled: nothing arrived for 15 s, retrying in 1 s` 反复循环 |
| 独立 `bu.download` 该 wasm | **90 秒超时未完成** |
| 同轮 console | `Failed to load resource: the server responded with a status of 403 ()` |

结论（这一条推翻了此前"只差 Turnstile"的判断）：

1. **`/play/<game>` 才是 3D 世界地址，`/game/<game>` 只是游戏选择页。** 这一条此前一直没确认。
2. bloxd 的 3D 引擎是一个 **WASM 模块**（`bloxd_wasm_bg.*.wasm`），页面要先把它拉下来才进得了世界。**在这台机器上，这个文件 90 秒都下不完，且出现 403。**
3. **真 Chrome 同样卡在 0%**，因此"nefuOS 进不去 3D"并不完全是我们这边的缺陷：至少还有一层是网络与 CDN 的拒绝。要真正进 bloxd 3D，需要 (a) Turnstile 签发的会话，且 (b) 能完整拉下那个 WASM 包——两道门都得过。
4. 这条对照实验用的是正常浏览行为（打开站点、点按钮、下页面自己的资源），没有触碰任何反自动化机制。

#### 4.11.8) 自建体素沙盒：功能已全部实测通过

`verify_out\voxel_play.html` 在独立浏览器后端实测，控制台与画面双向确认：

| 项 | 实测值 |
| --- | --- |
| 启动 | `[VX] start` → `[VX] terrain verts=5442 columns=576` → `[VX] first frame drawn, verts=5442` |
| 帧率 | `~56–59 fps`（视口 718×763） |
| 画面 | 立体台地、侧面明暗分层、草地格纹、远处雾化，全对 |
| 破坏 | 按 `G`：`dig 12,13 h=0` 连续命中，顶点数 5442 → 5472（挖开暴露新内壁面），画面出现方形坑洞 |
| 放置 | 按 `F`：`place 12,11 h=2` → `h=3`，画面上准星前立起一根两格高柱子，分层清晰 |
| 移动 | 注入 `keydown w` 1.2 s，`pos 6.3,4.2,-1.7` → `6.3,4.2,-11.2`（前进 9.5 单位） |

实测中修掉的两个真 bug：

- **相机起点在场外**：世界 x,z 范围 `[-12,12]`，初始 `z=14` 在界外，射线一出界就 `continue`，`dig` 恒为 `no target`。
- **射线射程与眼高不匹配**：眼高 5.2 高于最高柱顶 4，原 `120 × 0.09` 只前进 10.8、只下降 2.98，永远穿不过柱顶平面。改为 `400 × 0.05`（20 单位射程）+ 眼高降到 3.2 后命中正常。
- 顺带修掉：`pickCell` 原先只认"有高度的柱子"，挖空后射线穿过空洞无法再落点，`place` 恒报 `no target`；改为空柱（`top === 0`）也接受落点。
