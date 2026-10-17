import re
src = open(r"D:\mycppos1\nefuOS\core\apps\apps.h", encoding="utf-8", errors="replace").read()
body = src.split("enum AppId {")[1].split("};")[0]
idx = 0
names = {}
for line in body.split("\n"):
    line = line.strip()
    if not line or line.startswith("//"):
        continue
    m = re.match(r"APP_(\w+)\s*(=\s*(\d+))?", line)
    if not m:
        continue
    if m.group(2):
        idx = int(m.group(3))
    names[m.group(1)] = idx
    idx += 1
want = ["NETCFG","FONTVIEW","EDITOR","CALENDAR","DISKUSAGE","PROCESSLIST","DBMANAGER","MLLAB","SYSMON","FXGRAPH","DATECALC","SCRATCH","CHATBOT","MATHTOOL","GFXLAB","SIMLAB","TEXTTOOL","COMPILERLAB","NETLAB","FSVIEW","REPL","BIGINT","FPSCI","FXSCI","GEDEMO","DEEPVIZ","RAYVIEW","WIKI","SETTINGS","STORE","HELP","EMAIL","TASKMGR","TERMINAL","FILEMGR","BROWSER","TEXTVIEW","JSONVIEW","IMAGEVIEWER","MUSIC","VIDEOPLAYER","MONITOR","WEATHER","GAMECENTER","WIDGETS","CONVERTER","NOTEPAD","PAINT","SNAKE","MINER","CLOCK"]
for k in want:
    if k in names:
        print(f"{k} = {names[k]}")
