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
