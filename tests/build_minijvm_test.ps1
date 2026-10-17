# nefuOS: headless mini JVM test build.
# 1. compiles tests/java/*.java with the host JDK (javac)
# 2. embeds the .class bytes via tools/gen_java_embed.py
# 3. builds + runs minijvm_test.exe (klib + stubs, no GUI)
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$python3 = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python3)) { $python3 = "python" }
Set-Location $root

# ---- 1. javac ----
$javac = Get-Command javac -ErrorAction SilentlyContinue
if (-not $javac) { throw "javac not found - a JDK is required for the minijvm test" }
$javaOut = "tests\java\out"
New-Item -ItemType Directory -Force -Path $javaOut | Out-Null
Get-ChildItem "$javaOut\*.class" -ErrorAction SilentlyContinue | Remove-Item -Force
$srcs = Get-ChildItem "tests\java\*.java" | ForEach-Object { $_.FullName }
# -XDstringConcat=inline: compiles "a"+b into StringBuilder.append chains
# (invokedynamic is outside the minijvm subset; see jdk-integration-plan.md)
& $javac.Source -XDstringConcat=inline -d $javaOut $srcs
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

# ---- 2. embed ----
& $python3 "tools\gen_java_embed.py"
if ($LASTEXITCODE -ne 0) { throw "gen_java_embed failed" }

# ---- 3. build + run ----
$out = Join-Path $env:TEMP "nefu_build\minijvm_test\minijvm_test.exe"
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null

& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Wno-sized-deallocation -I core `
  "tests\minijvm_test.cpp" "tests\nvfs_stubs.cpp" "core\apps\minijvm.cpp" `
  "core\apps\minijvm_core.cpp" "core\apps\minijvm_builtin.cpp" `
  "core\klib\memory.cpp" "core\klib\string.cpp" "core\klib\printf.cpp" `
  -o $out
if ($LASTEXITCODE -ne 0) { throw "minijvm test build failed" }

Write-Output "built: $out"
& $out
exit $LASTEXITCODE
