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
