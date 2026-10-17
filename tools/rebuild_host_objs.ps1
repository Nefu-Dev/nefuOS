# Rebuild the third-party host object files that build_host_only.ps1 reuses
# (LVGL, nanosvg, libwebp, quickjs) into $env:TEMP\nefu_build\bare.
# This mirrors the object-compile section of build_iso.ps1 (lines 58-128).
$ErrorActionPreference = 'Stop'
Set-Location 'D:\mycppos1\nefuOS'
$gcc = "D:\CLion\bin\mingw\bin\gcc.exe"
$bareOut = Join-Path $env:TEMP "nefu_build\bare"
New-Item -ItemType Directory -Force -Path $bareOut | Out-Null

$lvConfPath = (Resolve-Path "third_party\lvgl_conf\lv_conf.h").Path -replace '\\', '/'
$lvglRoot = "third_party\lvgl_src\lvgl-9.2.0"
$lvglSrc = Get-ChildItem (Join-Path $lvglRoot "src") -Recurse -Filter *.c | ForEach-Object { $_.FullName }
$lvglCFlags = @("-std=gnu11", "-O2", "-Wall", "-I", "third_party",
                "-I", "third_party\lvgl_conf", "-I", $lvglRoot,
                "-D", ('LV_CONF_PATH=' + $lvConfPath), "-c")

$count = 0
foreach ($lv in $lvglSrc) {
  $lname = ($lv -replace '.*\\src\\', 'lvgl_h_') -replace '[\\/]', '_' -replace '\.c$', '.o'
  $lobj = "$bareOut\$lname"
  if (-not (Test-Path $lobj)) {
    & $gcc @lvglCFlags $lv -o $lobj
    if ($LASTEXITCODE -ne 0) { throw "lvgl host compile failed: $lv" }
    $count++
  }
}
Write-Output "lvgl objs built: $count"

& $gcc -O2 -I third_party -c "third_party\nanosvg_impl.c" -o "$bareOut\host_nanosvg.o"
if ($LASTEXITCODE -ne 0) { throw "nanosvg host compile failed" }

$webpHostSrc = @(
  "third_party\libwebp\src\dec\alpha_dec.c", "third_party\libwebp\src\dec\buffer_dec.c",
  "third_party\libwebp\src\dec\frame_dec.c", "third_party\libwebp\src\dec\idec_dec.c",
  "third_party\libwebp\src\dec\io_dec.c", "third_party\libwebp\src\dec\quant_dec.c",
  "third_party\libwebp\src\dec\tree_dec.c", "third_party\libwebp\src\dec\vp8_dec.c",
  "third_party\libwebp\src\dec\vp8l_dec.c", "third_party\libwebp\src\dec\webp_dec.c",
  "third_party\libwebp\src\dsp\alpha_processing.c", "third_party\libwebp\src\dsp\cpu.c",
  "third_party\libwebp\src\dsp\dec.c", "third_party\libwebp\src\dsp\dec_clip_tables.c",
  "third_party\libwebp\src\dsp\filters.c", "third_party\libwebp\src\dsp\lossless.c",
  "third_party\libwebp\src\dsp\rescaler.c", "third_party\libwebp\src\dsp\upsampling.c",
  "third_party\libwebp\src\dsp\yuv.c",
  "third_party\libwebp\src\dsp\alpha_processing_sse2.c", "third_party\libwebp\src\dsp\dec_sse2.c",
  "third_party\libwebp\src\dsp\filters_sse2.c", "third_party\libwebp\src\dsp\lossless_sse2.c",
  "third_party\libwebp\src\dsp\rescaler_sse2.c", "third_party\libwebp\src\dsp\upsampling_sse2.c",
  "third_party\libwebp\src\dsp\yuv_sse2.c",
  "third_party\libwebp\src\utils\bit_reader_utils.c", "third_party\libwebp\src\utils\color_cache_utils.c",
  "third_party\libwebp\src\utils\filters_utils.c", "third_party\libwebp\src\utils\huffman_utils.c",
  "third_party\libwebp\src\utils\palette.c", "third_party\libwebp\src\utils\quant_levels_dec_utils.c",
  "third_party\libwebp\src\utils\random_utils.c", "third_party\libwebp\src\utils\rescaler_utils.c",
  "third_party\libwebp\src\utils\thread_utils.c", "third_party\libwebp\src\utils\utils.c"
)
$webpFlags = @("-O2", "-I", "third_party\libwebp", "-I", "third_party",
               "-include", "third_party\webp_impl.h",
               "-DWEBP_MALLOC=nefu_kalloc_x", "-DWEBP_CALLOC=nefu_kcalloc_x",
               "-DWEBP_FREE=nefu_kfree_x", "-DWEBP_REALLOC=nefu_krealloc_x")
foreach ($w in $webpHostSrc) {
  $wobj = "$bareOut\host_" + ($w -replace '.*\\', '' -replace '\.c$', '.o')
  if (-not (Test-Path $wobj)) {
    & $gcc @webpFlags -c $w -o $wobj
    if ($LASTEXITCODE -ne 0) { throw "webp host compile failed: $w" }
  }
}
$webpImplObj = "$bareOut\host_webp_impl.o"
if (-not (Test-Path $webpImplObj)) {
  & $gcc @webpFlags -c "third_party\webp_impl.c" -o $webpImplObj
  if ($LASTEXITCODE -ne 0) { throw "webp impl compile failed" }
}

$qjSrc = @("quickjs.c", "libregexp.c", "libunicode.c", "dtoa.c")
foreach ($q in $qjSrc) {
  $qobj = "$bareOut\host_quickjs_" + [IO.Path]::GetFileNameWithoutExtension($q) + ".o"
  if (-not (Test-Path $qobj)) {
    & $gcc -O2 -w -D_GNU_SOURCE -I "third_party\quickjs" -c "third_party\quickjs\$q" -o $qobj
    if ($LASTEXITCODE -ne 0) { throw "quickjs host compile failed: $q" }
  }
}

$n = (Get-ChildItem "$bareOut\lvgl_h_*.o" -File | Measure-Object).Count
Write-Output "bare object count (lvgl_h_*): $n"
if ($n -lt 100) { throw "lvgl host objects insufficient" }
Write-Output "OK: bare host objects ready"
