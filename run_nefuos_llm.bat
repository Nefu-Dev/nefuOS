@echo off
REM nefuOS QEMU Launch Script (with real nefuLLM model disk)
"D:\qemu\qemu-system-x86_64.exe" ^
    -cdrom "%~dp0dist\nefuOS.iso" ^
    -m 256M ^
    -vga std ^
    -device sb16 ^
    -hda "%~dp0dist\llm_model.img" ^
    -boot d ^
    -name "nefuOS (LLM)"
pause
