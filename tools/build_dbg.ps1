# Debug build: recompile with -g to get crash symbols
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
Push-Location $root
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$gcc = "D:\CLion\bin\mingw\bin\gcc.exe"
$bareOut = Join-Path $env:TEMP "nefu_build\bare_dbg"
$distOut = Join-Path $env:TEMP "nefu_fix_test"
New-Item -ItemType Directory -Force -Path $bareOut | Out-Null
$hostExe = Join-Path $distOut "nefuOS_dbg.exe"

$coreSrc = @(
  "core\apps\browser.cpp", "core\apps\minijs.cpp", "core\apps\compositor.cpp",
  "core\apps\sprite.cpp", "core\apps\windowmgr.cpp", "core\apps\appsrv.cpp"
) 
$coreSrc += (Get-ChildItem "core" -Recurse -Filter *.cpp -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName.Substring($root.Length+1) } | Where-Object { $_ -like "core\*" -and $_ -notlike "core\apps\*" })

# quickjs with -g
$qjSrc = @("quickjs.c","libregexp.c","libunicode.c","dtoa.c")
$qobjs = @()
foreach ($q in $qjSrc) {
  $qobj = "$bareOut\host_quickjs_" + [IO.Path]::GetFileNameWithoutExtension($q) + ".o"
  & $gcc -O0 -g -w -D_GNU_SOURCE -I "third_party\quickjs" -c "third_party\quickjs\$q" -o $qobj
  if ($LASTEXITCODE -ne 0) { throw "quickjs dbg failed: $q" }
  $qobjs += $qobj
}
$respFile = Join-Path $bareOut "host_sources_dbg.rsp"
$allSrc = $coreSrc + @("backends\win32\win32.cpp") + $qobjs
$respContent = ($allSrc | ForEach-Object { '"' + $_ + '"' }) -join "`r`n"
[IO.File]::WriteAllText($respFile, $respContent, (New-Object Text.UTF8Encoding $false))
& $g -std=c++17 -O0 -g -fno-exceptions -fno-rtti -fno-builtin -w -DNEFU_DUMP_REC -I core -I third_party "-Wl,--stack,16777216" -o $hostExe "@$respFile" -lgdi32 -luser32 -lgdiplus -lole32 -luuid -lws2_32 -liphlpapi -lwlanapi -lwininet -lwinmm -lmfplat -lmfreadwrite -lmf -lmfuuid -lcomdlg32 2>&1 | Select-Object -Last 8
if ($LASTEXITCODE -ne 0) { throw "dbg link failed" }
Write-Output "dbg exe OK: $((Get-Item $hostExe).Length) -> $hostExe"
