@echo off
REM nefuOS QEMU Launch Script (with virtual hard disk)
"D:\qemu\qemu-system-x86_64.exe" ^
    -cdrom "%~dp0dist\nefuOS_v2.iso" ^
    -hda fat:rw:%~dp0dist\ ^
    -m 256M ^
    -vga std ^
    -device sb16 ^
    -boot d ^
    -name "nefuOS (HD)"
pause
