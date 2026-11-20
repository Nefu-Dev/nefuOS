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
