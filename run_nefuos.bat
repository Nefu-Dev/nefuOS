@echo off
REM nefuOS QEMU Launch Script
"D:\qemu\qemu-system-x86_64.exe" ^
    -cdrom "%~dp0dist\nefuOS_v2.iso" ^
    -m 256M ^
    -vga std ^
    -device sb16 ^
    -boot d ^
    -name "nefuOS"
pause
