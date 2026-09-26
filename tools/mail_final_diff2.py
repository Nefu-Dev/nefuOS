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

# read already-read msg0 then back -> Inbox list must be unchanged
countdiff("f0_list_inbox.png", "f5_list_after_read.png", "f0 vs f5 (read->list)")

# after reading msg1: list vs after compose->draft->Inbox; only status bar may differ (y>=600)
countdiff("f7_list_after_msg1.png", "f9_list_after_compose.png", "f7 vs f9 (compose->list)", box=(0, 0, 806, 600))
countdiff("f7_list_after_msg1.png", "f9_list_after_compose.png", "f7 vs f9 FULL (incl status)")

# account round trip: must be identical
countdiff("f9_list_after_compose.png", "f11_list_after_account.png", "f9 vs f11 (account->list)")

# source vs plain read views: the header area should differ only in the header lines
countdiff("f1_read.png", "f3_read_source.png", "f1 vs f3 (plain->source)")
countdiff("f1_read.png", "f4_read_plain.png", "f1 vs f4 (plain after source->plain)")
