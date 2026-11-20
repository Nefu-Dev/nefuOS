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
