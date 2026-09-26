from PIL import Image

base = r"D:\mycppos1\nefuOS\shots"

def countdiff(a, b, name, box=None):
    ia = Image.open(base + "\\" + a).convert("RGB")
    ib = Image.open(base + "\\" + b).convert("RGB")
    pa = ia.load(); pb = ib.load()
    x0, y0, x1, y1 = box if box else (0, 0, 806, 629)
    n = 0
    rows = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            if pa[x, y] != pb[x, y]:
                n += 1
                rows[y] = rows.get(y, 0) + 1
    extra = ""
    if rows:
        top = sorted(rows.items(), key=lambda kv: -kv[1])[:6]
        extra = "  top rows: " + str(top)
    print(f"{name}: diff={n}{extra}")

# v5 vs v0: both Inbox list; msg0 was already read in both (persisted), so should be identical.
countdiff("v0_list_inbox.png", "v5_list_after_read.png", "v0 vs v5 (read->list)")
# v7 vs v0: Inbox list again; only the status text should differ (Draft saved vs Ready)
countdiff("v0_list_inbox.png", "v7_list_after_compose.png", "v0 vs v7 (compose->list)")
# v9 vs v7: both Inbox list after account round-trip; status text identical ("Draft saved")
countdiff("v7_list_after_compose.png", "v9_list_after_account.png", "v7 vs v9 (account->list)")
