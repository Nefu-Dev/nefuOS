### 4.1) 挑战帧取证：三处新证据（bd29–bd32）

| 探针 | 结论 |
|---|---|
| 对象成员缺失 `[FRAME-MISS-OBJ]`（把 navigator/screen/performance/crypto/localStorage/sessionStorage/history 包成 Proxy，读到 undefined 就打名字） | **0 条命中** —— 缺的东西不在这些宿主对象上 |
| `Function.prototype.call/apply` 包装（只在 `this` 不是函数时打栈） | **0 条命中** —— 失败不是 `fn.call(...)` 形态；而且第一版用 `_oa.apply(...)` 会经我们自己的包装递归到栈溢出（`qjs: SO`），必须改 `Reflect.apply` |
| `g.a0` 字符串表包装（想直接看混淆代码解出的属性名） | **无输出** —— `a0` 不在全局，是脚本内部闭包，包不到 |

补了 `PerformanceObserver` / `AudioContext`（含 `createBuffer`、`startRendering`）/ `webkitAudioContext` / `visualViewport` 之后，失败点从「`:2:486439` → `:2:622007` → `:2:610404`」一路后移，但**每次 challenge 变体不同，报错位置也不同**，其中一次抓到的是 `object(class 49) is not a function at call (native)`：class 49 在 QuickJS 里是 `JS_CLASS_GLOBAL_OBJECT`，说明有一处把全局对象当函数调用了。

`bd31` 还做了一次"同一份脚本内定位"：报错 `:2:484536` 正好落在 `PD()` 里 `SU[HE][JQ]()` 这个调用的右括号上 —— 也就是 **`SU[HE][JQ]` 取到 undefined 后被调用**，其中 `HE` / `JQ` 由脚本自己的 `a0(...)` 解出。这是目前最精确的一次定位：缺的确实是一个宿主对象上的方法，只是名字被混淆表遮住了。

诊断开关已全部收进 `NF_DUMP_FRAME`（默认关）：`[FD]` 全量 dump、对象 Proxy、`call/apply` 钩子、`a0` 包装，只在显式打开时生效，避免污染正常跑测（build64 13.6 s，`node --check` exit 0）。
