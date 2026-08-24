@echo off
rem ============================================================
rem  APP 一键构建：编译 + 生成 .bin（供 Bootloader UART/SD 升级）
rem  产物: MDK-ARM\ender3 v2\ender3 v2.bin
rem ============================================================
setlocal
cd /d "%~dp0MDK-ARM"

"C:\Keil_v5\UV4\UV4.exe" -b "ender3 v2.uvprojx"
if errorlevel 2 goto :fail

"C:\Keil_v5\ARM\ARMCC\Bin\fromelf.exe" --bin --output "ender3 v2\ender3 v2.bin" "ender3 v2\ender3 v2.axf"
if errorlevel 1 goto :fail

echo.
echo ============================================
echo  Build OK: MDK-ARM\ender3 v2\ender3 v2.bin
echo ============================================
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
