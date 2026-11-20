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
