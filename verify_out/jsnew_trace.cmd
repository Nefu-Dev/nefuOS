set pagination off
set confirm off
set auto-solib-add 0
set print thread-events off
set $logfile = "D:/mycppos1/nefuOS/verify_out/jsnew_trace.txt"
file C:/Users/huawei/AppData/Local/Temp/nefu_dist/nefuOS_dbg.exe
# trace JS_NewStringLen calls, log buf_len
break quickjs.c:4736
commands
  silent
  set $dummy = (int)0
  printf "JSNEW buf_len=%llu buf=%p\n", buf_len, buf
  continue
end
run --app 17 --url http://127.0.0.1:8123/nfu_noread.html
printf "\n===== STOPPED =====\n"
bt 15
