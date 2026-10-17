set pagination off
set confirm off
set auto-solib-add 0
set print pretty on
file C:/Users/huawei/AppData/Local/Temp/nefu_dist/nefuOS_dbg.exe
run --app 17 --url http://127.0.0.1:8123/mini600.html
bt 30
info registers rip rsp rax rbx rcx rdx rsi rdi
quit
