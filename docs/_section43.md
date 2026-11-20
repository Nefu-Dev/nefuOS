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
