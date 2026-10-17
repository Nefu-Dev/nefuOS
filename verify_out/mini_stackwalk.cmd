set pagination off
set confirm off
set auto-solib-add 0
set print thread-events off
file C:/Users/huawei/AppData/Local/Temp/nefu_dist/nefuOS_dbg.exe
run --app 17 --url http://127.0.0.1:8123/mini600.html
printf "\n===== STOPPED (crash) =====\n"
info registers rip rsp rbp
printf "\n===== STACK WORDS (first 60) =====\n"
x/120a $rsp
printf "\n===== top frame =====\n"
bt 8
