import io

p = r"D:\mycppos1\nefuOS\docs\browser-parity-status.md"
add = u"""

## \u4e09\u30012026-11-09 \u7b2c\u4e09\u8f6e\uff1ashim \u8bed\u6cd5\u590d\u6d3b\u540e\u7684\u771f\u75c5\u56e0\uff08Worker \u4e0e IDB get \u8bed\u4e49\uff09

### 1) \u4e00\u53e5\u8bdd\u7ed3\u8bba

`0% Loading required libraries` \u4e0d\u662f\u7eb9\u7406\u3001\u4e0d\u662f WebGL\u3001\u4e5f\u4e0d\u662f\u7f51\u7edc\u3002\u5b83\u662f
`Promise.all([preload, turnstileLoadedPromise, waitUntilAllSdksReady()])` \u91cc firebase \u7684
IndexedDB \u8bfb\u53d6\u629b\u4e86 TypeError\uff0c\u6574\u6761 Promise \u9759\u9ed8 reject\uff0c`.then()`
\u6c38\u8fdc\u4e0d\u6267\u884c\uff0c\u6240\u4ee5\u8fdb\u5ea6\u6761\u505c\u5728 0%\uff0c\u4e14\u4e00\u6761\u9519\u8bef\u90fd\u4e0d\u544a\u8bc9\u4f60\u3002

### 2) \u672c\u8f6e\u4e09\u5904\u4fee\u590d\uff08\u5747\u5df2\u8fdb `build\\nefuOS.exe`\uff0cbuild46/build47\uff09

| # | \u95ee\u9898 | \u6839\u56e0 | \u4fee\u590d |
| - | ---- | ---- | ---- |
| 10 | \u4e16\u754c\u751f\u6210 worker \u62a5 `error from worldgen worker`\uff0c\u52a0\u8f7d\u5361\u6b7b | shim \u91cc `g.Worker` \u53ea\u662f"\u62a5\u4e00\u6b21\u9519\u5c31\u4f5c\u5e9f"\u7684\u6869 | `jsdom_shim.h` \u6362\u6210\u771f\u5b9e\u73b0\uff1aBlob \u7ecf `createObjectURL` \u5df2\u662f base64 `data:` URL\uff0c\u76f4\u63a5\u89e3\u51fa\u6e90\u7801\uff1b`new Function(...)` \u5728\u540c\u4e00\u4e0a\u4e0b\u6587\u91cc\u4ee5\u5b9a\u65f6\u5668\u961f\u5217\u4ea4\u9519\u6267\u884c\uff08\u65e0 OS \u7ebf\u7a0b\u3001\u65e0\u7b2c\u4e8c\u4e2a QuickJS \u4e0a\u4e0b\u6587\uff09\uff1b`postMessage` \u6df1\u5ea6\u514b\u9686\uff0c\u4e24\u4fa7\u4e0d\u5171\u4eab\u5f15\u7528 |
| 11 | `.err` \u91cc\u5bc6\u5bc6\u9ebb\u9ebb\u7684 `log#N:` \u5168\u662f\u7a7a\u884c\uff0c\u770b\u4e0d\u5230\u6e38\u620f\u81ea\u5df1\u7684\u72b6\u6001 | console \u7528 `Array.prototype.join(' ')`\uff0c\u5bf9\u8c61\u4e00\u5f8b\u53d8\u7a7a\u4e32 | `minijs.cpp` \u52a0 `__ncFmt` \u6df1\u5ea6 2 \u7684 JSON \u5c55\u5f00\uff08Array 10 \u9879\u3001\u5bf9\u8c61 14 \u952e\u3001\u6700\u957f 420 \u5b57\u7b26\uff09 |
| 12 | `Loading required libraries` \u7684 `Promise.all` \u9759\u9ed8 reject | `IDBObjectStore.get()` \u672a\u547d\u4e2d\u8fd4\u56de `null`\uff0c\u800c\u89c4\u8303\u8981\u6c42 `undefined`\uff1bfirebase \u5199\u7684\u662f `void 0===Vi?null:Vi.value`\uff0c`null` \u80fd\u8fc7\u5224\u7a7a\u3001\u4e0b\u4e00\u53e5\u5c31\u7206 `Cannot read property 'value' of null` | `jsdom_shim.h` \u6539\u4e3a `hit ? m[k] : void 0` |

### 3) \u5b9e\u6d4b\u8bc1\u636e\uff08bd10 / bd11\uff0c`https://bloxd.io/game/survival`\uff09

- `[WORKER-OK] data:application/octet-stream;base64,LyohIEZvciBsaWNlbnNlIGl 1014329`
  \u2014\u2014 1,014,329 \u5b57\u8282\u7684 worldgen \u811a\u672c\u771f\u6b63\u8f7d\u5165\u5e76\u6267\u884c\uff0c`error from worldgen worker` \u5f7b\u5e95\u6d88\u5931\uff0c
  \u65e0 `WORKER-ERR` / `WORKER-MSGERR` / `WORKER-LOADERR`\u3002
- `log#112 [2026-11-09T06:15:36.796Z] All required libraries loaded`
  \u2014\u2014 \u9996\u6b21\u51fa\u73b0\uff0c\u8bf4\u660e `LOAD_REQUIRED_LIBS` \u8fd9\u4e00\u6b65\u771f\u7684\u8dd1\u5b8c\u4e86\u3002
- \u771f\u7f51\u7edc\u5f00\u59cb\u5de5\u4f5c\uff08\u6b64\u524d\u5168\u7a0b 0 \u6761 POST\uff09\uff1a
  `POST https://social2.bloxd.io/matchmake/joinOrCreate/social_updates_2`\u3001
  `social/get-game-ccus`\u3001`social/get-favourite-lobbies`\u3001`social/get-open-custom-lobbies`\u3001
  `social/get-social-information`\u3001`social/analytics/report-events`\u3001
  `https://bloxd.io/index/payment/history`\u3001`https://bloxd.io/index/xsolla/catalogue`\u3002
- \u4e0a\u8ff0\u8bf7\u6c42\u5168\u90e8 `401`\uff08`Failed to fetch game CCUs / refresh social state / Super Rank catalogue / payment history`\uff09
  \u2014\u2014 \u672a\u767b\u5f55\u8bbf\u5ba2\u65e0 greenlight token\u3002
- `dom_bd11.html`\uff0845,589 B\uff09\uff1a`<canvas` \u8ba1\u6570 = 0\u3001`nfu_cv` \u8ba1\u6570 = 0\u3002
  \u4ecd\u80fd\u770b\u5230 `0%` + `Loading required libraries` \u7684\u9690\u85cf overlay\uff08`visibility:hidden;opacity:0`\uff09\u3002
- `GL: ctx nfu_cv1@300x150 300x150 rc=0x10000` \u662f\u672c\u5de5\u7a0b\u81ea\u5efa\u6865\u7684\u4e0a\u4e0b\u6587\uff0c\u4e0d\u662f bloxd \u521b\u7684\u3002

### 4) \u73b0\u5728\u5361\u5728\u54ea\u4e00\u9053\u5899

`bloxd_main.js` \u504f\u79fb ~438,717 \u5904\u5199\u5f97\u5f88\u6e05\u695a\uff1a

```
const kt = useTurnstile ? { turnstileLoadedPromise: new Promise(...window.onTurnstileLoaded...),
                            getTurnstileToken: async go => new Promise((res, rej) => Lg.enqueue(new cg(res, rej, go))),
                            tryShowWidgetPopup: () => Pg && Wg(true) } : { ... }
... window.turnstile.render('#turnstile-widget', { sitekey: '0x4AAAAAAAa4cz8QxEw-M2SE', action: 'Greenlight', ... })
```

\u5373\uff1a\u767b\u5f55/\u914d\u5bf9\u9700\u8981 Cloudflare Turnstile \u7684 token\uff0c\u800c widget \u662f\u5728 **iframe** \u91cc\u8dd1\u7684\u3002
\u672c\u5de5\u7a0b\u7684 iframe \u8ba1\u6570\u4e00\u76f4\u662f 0\uff0c\u6240\u4ee5 token \u62ff\u4e0d\u5230 \u2192 \u670d\u52a1\u7aef 401 \u2192
matchmaking \u6ca1 seat \u2192 \u4e0d\u4f1a\u521b canvas \u2192 3D \u4e16\u754c\u6c38\u8fdc\u4e0d\u51fa\u73b0\u3002

\u8fb9\u754c\u4e0d\u53d8\uff1a\u672c\u5de5\u7a0b\u53ea\u505a"è®\u8ba9\u771f\u5b9e\u7684 Turnstile \u811a\u672c\u80fd\u5728\u6211\u4eec\u7684 iframe \u91cc\u8dd1\u5b8c"\uff0c
\u4e0d\u4f1a\u9020\u5047 token\u3001\u4e0d\u4f1a\u7834\u89e3\u4eba\u673a\u9a8c\u8bc1\u3002

### 5) \u4e0b\u4e00\u6b65\uff08\u6309\u4f9d\u8d56\u987a\u5e8f\uff09

1. shim \u8865\u771f iframe\uff1a\u5b50\u6587\u6863 + \u72ec\u7acb document/window + \u8de8\u6587\u6863 postMessage\uff0c\u8ba9 turnstile api.js \u81ea\u5df1\u8dd1\u5b8c\uff1b
2. \u62ff\u5230 seat \u540e\u63a5 Colyseus \u957f\u8fde\u63a5\uff08`__nfuWsOpen` \u94a9\u5b50\u5df2\u6709\uff0c\u9700\u6838\u5bf9\u63e1\u624b/\u5e27\u534f\u8bae\uff09\uff1b
3. \u7eb9\u7406\u56fe\u96c6\uff1a`createImageBitmap` / `2d drawImage` / `getImageData` \u5fc5\u987b\u7ed9\u771f\u50cf\u7d20\uff1b
4. \u6700\u540e\u624d\u662f swgl \u6e32\u67d3\u4e0e\u5e27\u5faa\u73af\u3002

### 6) \u56de\u5f52

- `bd11`\uff08build47\uff09\uff1a\u65e0201\uff0cDOM 396\u2013400 \u8282\u70b9\uff0c`layout_page runs=84`\uff0c\u4e0d\u5d29\u3002
- `x1.html` \u63a2\u9488\uff08shim \u8bed\u6cd5\u56de\u5f52\u4ef6\uff09\u4ecd\u7136\u4e0d\u5d29\uff1b`node --check verify_out\\shim.js` exit 0\u3002
"""
with io.open(p, "a", encoding="utf-8") as f:
    f.write(add)
print("appended", len(add))

