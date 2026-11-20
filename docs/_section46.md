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
