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
