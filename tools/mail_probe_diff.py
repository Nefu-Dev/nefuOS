from PIL import Image

def countdiff(a, b, name):
    ia = Image.open(a).convert("RGB"); ib = Image.open(b).convert("RGB")
    pa = ia.load(); pb = ib.load()
    n = 0
    for y in range(0, 620, 2):
        for x in range(0, 806, 2):
            if pa[x, y] != pb[x, y]:
                n += 1
    print(f"{name}: diff~{n}")

base = r"D:\mycppos1\nefuOS\shots"
countdiff(base + r"\p0_list.png", base + r"\p1_inbox.png", "p0 vs p1")
countdiff(base + r"\p1_inbox.png", base + r"\p2_sent.png", "p1 vs p2")
countdiff(base + r"\p2_sent.png", base + r"\p3_account.png", "p2 vs p3")
countdiff(base + r"\p3_account.png", base + r"\p4_drafts.png", "p3 vs p4")
