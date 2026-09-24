# nefuOS �?exe + + + ISO�?GrubEsp 閺冨爼顤傛径鏍晸閹?ESP+GRUB 绾句胶娲忛梹婊冨剼閿?
param(
    [switch]$GrubEsp
)
$ErrorActionPreference = "Continue"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$as = "D:\CLion\bin\mingw\bin\as.exe"
$ld = "D:\CLion\bin\mingw\bin\ld.exe"
$objcopy = "D:\CLion\bin\mingw\bin\objcopy.exe"
Set-Location $root

# 0) Admin hash injection (FNV-1a 64, hex). The plain password is read from
#    the NEFU_ADMIN_PASSWORD env var, hashed, and ONLY the hash is embedded in
#    the build. The password never appears in source, ISO, or logs.
#    If the env var is unset, a random placeholder hash is used (login denied).
$python3 = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python3)) { $python3 = "python" }
$adminPw = [Environment]::GetEnvironmentVariable("NEFU_ADMIN_PASSWORD")
& $python3 "tools\admin_hash.py" $adminPw | Out-Null
if ($LASTEXITCODE -ne 0) { throw "admin hash gen failed" }

$coreSrc = @(
  "core\nefuos.cpp", "core\klib\memory.cpp", "core\klib\string.cpp", "core\klib\printf.cpp",
  "core\vfs\vfs.cpp", "core\vfs\nvfs.cpp", "core\gui\gfx.cpp", "core\gui\lv_cjk_font.cpp", "core\gui\wm.cpp", "core\gui\widgets.cpp", "core\gui\desktop.cpp",
  "core\apps\apps.cpp", "core\apps\terminal.cpp", "core\apps\filemgr.cpp", "core\apps\calc.cpp",
  "core\apps\textview.cpp", "core\apps\sysinfo.cpp", "core\apps\settings.cpp", "core\apps\store.cpp",
  "core\apps\snake.cpp", "core\apps\paint.cpp", "core\apps\clock.cpp", "core\apps\notepad.cpp",
  "core\apps\editor.cpp",
  "core\apps\minesweep.cpp", "core\apps\imageviewer.cpp", "core\apps\music.cpp", "core\apps\videoplayer.cpp", "core\apps\monitor.cpp",
  "core\apps\browser.cpp", "core\apps\browser_engine.cpp", "core\apps\netcfg.cpp",
  "core\apps\nefvm.cpp", "core\apps\nefud.cpp", "core\apps\jpeg.cpp", "core\apps\minijs.cpp",
  "core\net\net.cpp",
  "core\gui\ttfont.cpp", "core\apps\fontview.cpp",
  "core\gui\lvgl_win.cpp",
    "core\sys\settings.cpp", "core\sys\sha256.cpp",
  "core\apps\wiki.cpp",
  "core\apps\calendar.cpp", "core\apps\diskusage.cpp",
  "core\apps\passgen.cpp", "core\apps\sticky.cpp", "core\apps\weather.cpp", "core\apps\help.cpp", "core\apps\dictionary.cpp", "core\apps\screenshot.cpp", "core\apps\colorpicker.cpp", "core\apps\search.cpp", "core\apps\recyclebin.cpp", "core\apps\taskmgr.cpp",
  "core\apps\inputmethod.cpp",
  "core\apps\clipboard.cpp",
  "core\apps\notifcenter.cpp",
  "core\apps\shortcuts.cpp",
  "core\apps\wallpaper.cpp",
  "core\apps\theme.cpp",
  "core\apps\processlist.cpp",
  "core\apps\about_full.cpp",
  "core\apps\sysinfo_ext.cpp",
  "core\apps\credits.cpp",
  "core\apps\releasenotes.cpp",
  "core\apps\installer.cpp",
  "core\apps\downloadmgr.cpp",
  "core\sys\power.cpp",
  "core\sys\sysapi.cpp",

  "core\apps\bios.cpp",
  "core\audio.cpp",
    "core\lib\json.cpp", "core\lib\regex.cpp", "core\lib\softmath.cpp", "core\lib\hash.cpp",
  "core\lib\deflate.cpp", "core\lib\bigint.cpp", "core\lib\prime.cpp",
  "core\net\dns.cpp",
  "core\apps\term_ext.cpp", "core\apps\tetris.cpp", "core\apps\game2048.cpp", "core\apps\sudoku.cpp",
  "core\apps\memorymatch.cpp", "core\apps\hexedit.cpp", "core\apps\jsonview.cpp", "core\apps\findfiles.cpp",
  "core\apps\pomodoro.cpp", "core\apps\stopwatch.cpp", "core\apps\wordcount.cpp",
  "core\algo\sort.cpp", "core\algo\search.cpp", "core\algo\graph.cpp", "core\algo\ds.cpp",
  "core\algo\numeric.cpp", "core\apps\algoviz.cpp",
  "core\gfxlib\raster.cpp", "core\gfxlib\geo.cpp", "core\gfxlib\transform.cpp",
  "core\gfxlib\noise.cpp", "core\gfxlib\color.cpp", "core\apps\gfxlab.cpp",
  "core\textlib\levenshtein.cpp", "core\textlib\lcs.cpp", "core\textlib\kmp.cpp",
  "core\textlib\aho.cpp", "core\textlib\token.cpp", "core\textlib\ngram.cpp",
  "core\textlib\regexlite.cpp", "core\textlib\diff.cpp",
    "core\datlib\vec.cpp", "core\datlib\ringbuf.cpp", "core\datlib\bitset.cpp",
    "core\datlib\hashtab.cpp", "core\datlib\rbtree.cpp", "core\datlib\treap.cpp",
    "core\datlib\btree.cpp", "core\datlib\segtree.cpp", "core\datlib\fenwick.cpp",
    "core\datlib\bloom.cpp", "core\datlib\lru.cpp", "core\datlib\sortedlist.cpp",
    "core\datlib\ipq.cpp", "core\datlib\objpool.cpp", "core\datlib\radix.cpp",
    "core\datlib\timerwheel.cpp",
    "core\cryptlib\sha256.cpp", "core\cryptlib\sha1.cpp", "core\cryptlib\md5.cpp",
    "core\cryptlib\crc.cpp", "core\cryptlib\b64.cpp", "core\cryptlib\hmac.cpp",
    "core\cryptlib\pbkdf2.cpp", "core\cryptlib\aes.cpp", "core\cryptlib\rc4.cpp",
    "core\cryptlib\xor.cpp",
    "core\complib\bitio.cpp", "core\complib\rle.cpp", "core\complib\huffman.cpp",
    "core\complib\lz77.cpp", "core\complib\lzw.cpp", "core\complib\arithmetic.cpp",
    "core\complib\bwt.cpp",
    "core\mathlib\bigint.cpp", "core\mathlib\rational.cpp", "core\mathlib\complex.cpp",
    "core\mathlib\matrix.cpp", "core\mathlib\fft.cpp", "core\mathlib\polynomial.cpp",
    "core\mathlib\stat.cpp", "core\mathlib\regression.cpp", "core\mathlib\prime.cpp",
    "core\mathlib\random.cpp", "core\mathlib\vector2.cpp", "core\mathlib\gcd.cpp",
    "core\simlib\life.cpp", "core\simlib\queue.cpp", "core\simlib\world.cpp",
    "core\simlib\boids.cpp", "core\simlib\epidemic.cpp", "core\simlib\traffic.cpp",
    "core\simlib\perlin.cpp", "core\simlib\langton.cpp",
    "core\gfxmath\vec3.cpp", "core\gfxmath\mat4.cpp", "core\gfxmath\quat.cpp",
    "core\gfxmath\ray.cpp", "core\gfxmath\aabb.cpp", "core\gfxmath\camera.cpp",
    "core\gfxmath\mesh.cpp", "core\gfxmath\proj.cpp",
    "core\audlib\wave.cpp", "core\audlib\synth.cpp", "core\audlib\env.cpp",
    "core\audlib\filter.cpp", "core\audlib\seq.cpp", "core\audlib\mixer.cpp",
    "core\audlib\modulate.cpp",
    "core\dblib\csv.cpp", "core\dblib\ini.cpp", "core\dblib\jsonstore.cpp",
    "core\dblib\bptree.cpp", "core\dblib\page.cpp", "core\dblib\table.cpp",

  "third_party\stb_image_wrap.cpp"
)

# LVGL 9.2.0 sources (third_party/lvgl_src/lvgl-9.2.0/src/*.c)
$gcc = "D:\CLion\bin\mingw\bin\gcc.exe"
$lvglRoot = "third_party\lvgl_src\lvgl-9.2.0"
$lvglSrc = Get-ChildItem (Join-Path $lvglRoot "src") -Recurse -Filter *.c | ForEach-Object { $_.FullName }
$lvConfPath = (Resolve-Path "third_party\lvgl_conf\lv_conf.h").Path -replace '\\', '/'
$lvglInclude = @("-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                 "-D", ("LV_CONF_PATH=" + $lvConfPath))
$lvglCFlags = @("-std=gnu11", "-O2", "-Wall", "-I", "third_party",
                "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                "-D", ('LV_CONF_PATH=' + $lvConfPath), "-c")

# 1) 閿涘澋in32�?
$bareOut = Join-Path $env:TEMP "nefu_build\bare"
$distOut = Join-Path $env:TEMP "nefu_dist"
New-Item -ItemType Directory -Force -Path $bareOut | Out-Null
New-Item -ItemType Directory -Force -Path $distOut | Out-Null
$hostExe = Join-Path $distOut "nefuOS.exe"
# LVGL is pure C: compile with gcc first, then link via g++ with the core
$lvglHostObjs = @()
foreach ($lv in $lvglSrc) {
  $lname = ($lv -replace '.*\\src\\', 'lvgl_h_') -replace '[\\/]', '_' -replace '\.c$', '.o'
  $lobj = "$bareOut\$lname"
  if (-not (Test-Path $lobj)) {
    & $gcc @lvglCFlags $lv -o $lobj
    if ($LASTEXITCODE -ne 0) { throw "lvgl host compile failed: $lv" }
  }
  $lvglHostObjs += $lobj
}
$nanosvgObj = "$bareOut\host_nanosvg.o"
& $gcc -O2 -I third_party -c "third_party\nanosvg_impl.c" -o $nanosvgObj
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
  $lvglHostObjs += $wobj
}
$webpImplObj = "$bareOut\host_webp_impl.o"
if (-not (Test-Path $webpImplObj)) {
  & $gcc @webpFlags -c "third_party\webp_impl.c" -o $webpImplObj
  if ($LASTEXITCODE -ne 0) { throw "webp impl compile failed" }
}
$lvglHostObjs += $webpImplObj
$lvglHostObjs += $nanosvgObj
# QuickJS (bellard/quickjs, MIT): host JS engine for the browser
$qjSrc = @(
  "quickjs.c", "libregexp.c", "libunicode.c", "cutils.c", "dtoa.c"
)
foreach ($q in $qjSrc) {
  $qobj = "$bareOut\host_quickjs_" + [IO.Path]::GetFileNameWithoutExtension($q) + ".o"
  if (-not (Test-Path $qobj)) {
    & $gcc -O2 -w -D_GNU_SOURCE -I "third_party\quickjs" -c "third_party\quickjs\$q" -o $qobj
    if ($LASTEXITCODE -ne 0) { throw "quickjs host compile failed: $q" }
  }
  $lvglHostObjs += $qobj
}
$linkErr = Join-Path $env:TEMP "nefu_link.log"
& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Wno-sized-deallocation -I core -I third_party @lvglInclude -o $hostExe ($coreSrc + @("backends\win32\win32.cpp", "backends\win32\media_win32.cpp")) $lvglHostObjs -lgdi32 -luser32 -lgdiplus -lole32 -lws2_32 -liphlpapi -lwlanapi -lwininet -lwinmm -lwinmm -lmfplat -lmfreadwrite -lmf -lmfuuid -lcomdlg32 2> $linkErr
if ($LASTEXITCODE -ne 0) { throw "host build failed" }
Write-Output "host exe OK: $((Get-Item $hostExe).Length) bytes"

# 2)
$bareFlags = @(
  "-std=c++17", "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-builtin",
  "-fno-stack-protector", "-mno-red-zone", "-mgeneral-regs-only", "-O2", "-Wall", "-Wextra",
  "-ffunction-sections", "-fdata-sections",
  "-Wno-sized-deallocation", "-DNEFU_BARE", "-I", "core", "-I", "third_party",
  "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
  "-D", ('LV_CONF_PATH=' + $lvConfPath), "-c"
)
$objs = @()
foreach ($s in $coreSrc) {
  # numeric.cpp is double-based and the bare kernel has no soft-double:
  # it is compiled for the host build only, never into the kernel.
  if ($s -like "*numeric.cpp") { continue }
  $name = $s -replace '[\\/]', '_' -replace '\.cpp$', '.o'
  $obj = "$bareOut\$name"
  & $g @bareFlags $s -o $obj
  if ($LASTEXITCODE -ne 0) { throw "bare compile failed: $s" }
  $objs += $obj
}
# LVGL 9.2.0 bare objects (pure C, compiled with gcc -mgeneral-regs-only so
# any float math resolves to the kernel soft-float library)
$lvglBareFlags = @("-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-mno-red-zone",
                   "-mgeneral-regs-only", "-O2", "-Wall", "-ffunction-sections", "-fdata-sections",
                   "-I", "third_party",
                   "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                   "-D", ('LV_CONF_PATH=' + $lvConfPath), "-c")
foreach ($lv in $lvglSrc) {
  $lname = ($lv -replace '.*\\src\\', 'lvgl_') -replace '[\\/]', '_' -replace '\.c$', '.o'
  $lobj = "$bareOut\$lname"
  if (-not (Test-Path $lobj)) {
    & $gcc @lvglBareFlags $lv -o $lobj
    if ($LASTEXITCODE -ne 0) { throw "lvgl compile failed: $lv" }
  }
  $objs += $lobj
}
& $g @bareFlags "backends\bare\bare.cpp" -o "$bareOut\bare_bare.o"
if ($LASTEXITCODE -ne 0) { throw "bare.cpp failed" }
$objs += "$bareOut\bare_bare.o"
# minimal soft-float (IEEE-754 single precision) for the bare kernel
& $g @bareFlags "backends\bare\softfloat.cpp" -o "$bareOut\bare_softfloat.o"
if ($LASTEXITCODE -ne 0) { throw "softfloat.cpp failed" }
$objs += "$bareOut\bare_softfloat.o"
$webpBareSrc = @(
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
  "third_party\libwebp\src\utils\bit_reader_utils.c", "third_party\libwebp\src\utils\color_cache_utils.c",
  "third_party\libwebp\src\utils\filters_utils.c", "third_party\libwebp\src\utils\huffman_utils.c",
  "third_party\libwebp\src\utils\palette.c", "third_party\libwebp\src\utils\quant_levels_dec_utils.c",
  "third_party\libwebp\src\utils\random_utils.c", "third_party\libwebp\src\utils\rescaler_utils.c",
  "third_party\libwebp\src\utils\thread_utils.c", "third_party\libwebp\src\utils\utils.c"
)
$webpBareFlags = @("-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-mno-red-zone",
                   "-mgeneral-regs-only", "-mno-sse", "-mno-sse2", "-O2", "-Wall",
                   "-ffunction-sections", "-fdata-sections",
                   "-I", "third_party\libwebp", "-I", "third_party",
                   "-include", "third_party\webp_impl.h",
                   "-DWEBP_MALLOC=nefu_kalloc_x", "-DWEBP_CALLOC=nefu_kcalloc_x",
                   "-DWEBP_FREE=nefu_kfree_x", "-DWEBP_REALLOC=nefu_krealloc_x")
foreach ($w in $webpBareSrc) {
  $wobj = "$bareOut\" + ($w -replace '.*\\', '' -replace '\.c$', '.o')
  if (-not (Test-Path $wobj)) {
    & $gcc @webpBareFlags -c $w -o $wobj
    if ($LASTEXITCODE -ne 0) { throw "webp bare compile failed: $w" }
  }
  $objs += $wobj
}
$webpImplBare = "$bareOut\webp_impl.o"
if (-not (Test-Path $webpImplBare)) {
  & $gcc @webpBareFlags -c "third_party\webp_impl.c" -o $webpImplBare
  if ($LASTEXITCODE -ne 0) { throw "webp impl bare compile failed" }
}
$objs += $webpImplBare

# 3) 閿涙瓬oot sector + kernel entry + multi-boot picker
& $as "backends\bare\boot.s" -o "$bareOut\boot.o"
if ($LASTEXITCODE -ne 0) { throw "boot.s failed" }
& $as "backends\bare\entry.s" -o "$bareOut\entry.o"
if ($LASTEXITCODE -ne 0) { throw "entry.s failed" }
& $as "backends\bare\menu.s" -o "$bareOut\menu.o"
if ($LASTEXITCODE -ne 0) { throw "menu.s failed" }
& $objcopy -O binary -j .text "$bareOut\menu.o" "$bareOut\menu.bin"
if ($LASTEXITCODE -ne 0) { throw "menu objcopy failed" }
$menuLen = (Get-Item "$bareOut\menu.bin").Length
if ($menuLen -gt 4096) { throw "menu.bin too large: $menuLen bytes (max 4096)" }
Write-Output "menu.bin OK: $menuLen bytes"

# �?x20000 kernel_start
$objs = @("$bareOut\entry.o") + $objs

# 4) link閿涘湧E �?>
& $ld -mi386pep --image-base 0x100000 --gc-sections -T "backends\bare\linker.ld" -o "$bareOut\kernel.exe" -Map "$bareOut\kernel.map" $objs
if ($LASTEXITCODE -ne 0) { throw "link failed" }
# mingw ld of PE �?section(.text) of VirtualAddress = absoluteVMA - image_base(=0)�?# rest section of VirtualAddress = absoluteVMA�?objcopy -O binary閿涘MA �?# loaded to 0x20000 after .rdata/.data/.bss 0x20000閿涘澃tring/閿涘矉绱氶妴?# 閿涙arse PE section �?section by RVA(absoluteVMA-0x100000) 閿涘異ss 0�?
function Rebin-Kernel {
    param([string]$InExe, [string]$OutBin)
    $fs = [IO.File]::OpenRead($InExe)
    $br = New-Object IO.BinaryReader($fs)
    try {
        $fs.Seek(0x3C, 0) | Out-Null
        $peOff = $br.ReadInt32()
        $fs.Seek($peOff + 6, 0) | Out-Null
        $numSec = $br.ReadUInt16()
        $fs.Seek($peOff + 20, 0) | Out-Null
        $optSize = $br.ReadUInt16()
        $secTab = $peOff + 24 + $optSize
        $secs = @()
        for ($i = 0; $i -lt $numSec; $i++) {
            $fs.Seek($secTab + $i * 40, 0) | Out-Null
            $nameBytes = $br.ReadBytes(8)
            $name = ([Text.Encoding]::ASCII.GetString($nameBytes)).Trim([char]0)
            $vs = $br.ReadUInt32()      # VirtualSize
            $va = $br.ReadUInt32()      # VirtualAddress
            $rawSize = $br.ReadUInt32() # SizeOfRawData
            $rawPtr = $br.ReadUInt32()  # PointerToRawData
            # PE section VirtualAddress image-base(0x100000) of RVA�?            # loaded to 0x100000 after�?= RVA�?text VA=0 -> 0x100000 閿涘鈧?            # note�?>= 0x100000 of VA 0x100000�?.pdata/.data
            # 0x40000 �?.text entry閿涘鈧?            $rva = $va
            $secs += [pscustomobject]@{ Name = $name; RVA = $rva; RawSize = $rawSize; RawPtr = $rawPtr; VSize = $vs }
        }
        $maxEnd = 0
        $bssRva = 0
        $bssVSize = 0
        foreach ($s in $secs) {
            if ($s.Name -eq ".bss") {
                $bssRva = $s.RVA
                $bssVSize = $s.VSize
            }
            # .bss has no raw data (zeroed by entry.s at runtime); .reloc is
            # not needed for a flat kernel image. Skip both so kernel.bin
            # stays compact (no 42KB zero padding).
            if ($s.Name -eq ".reloc") { continue }
            if ($s.RawSize -eq 0) { continue }
            $end = $s.RVA + $s.RawSize
            if ($end -gt $maxEnd) { $maxEnd = $end }
        }
        if ($bssVSize -gt 0 -and $bssRva -gt 0) {
            # bss runtime address = load base (0x100000) + RVA. Store it with
            # the size so entry.s can zero-fill the correct region.
            Set-Variable -Name kernelBssStart -Value ($bssRva + 0x100000) -Scope Script
            Set-Variable -Name kernelBssSize -Value $bssVSize -Scope Script
        } elseif ($bssRva -gt 0) {
            # mingw ld may leave VirtualSize=0 for .bss; derive the size from
            # the next section's VirtualAddress instead.
            $next = 0x7FFFFFFF
            foreach ($s2 in $secs) {
                if ($s2.RVA -gt $bssRva -and $s2.RVA -lt $next) { $next = $s2.RVA }
            }
            if ($next -eq 0x7FFFFFFF) { $next = $maxEnd }
            Set-Variable -Name kernelBssStart -Value ($bssRva + 0x100000) -Scope Script
            Set-Variable -Name kernelBssSize -Value ($next - $bssRva) -Scope Script
        }
        $out = New-Object byte[] $maxEnd
        foreach ($s in $secs) {
            if ($s.Name -eq ".reloc") { continue }
            if ($s.RawSize -eq 0 -or $s.RawPtr -eq 0) { continue }
            $fs.Seek($s.RawPtr, 0) | Out-Null
            $data = $br.ReadBytes($s.RawSize)
            [Array]::Copy($data, 0, $out, $s.RVA, $s.RawSize)
        }
        [IO.File]::WriteAllBytes($OutBin, $out)
    } finally {
        $fs.Close()
    }
}
Rebin-Kernel "$bareOut\kernel.exe" "$bareOut\kernel.bin"
if (-not $kernelBssStart) { $kernelBssStart = 0x1073020 }  # 0x100000 + old 0x73020 RVA fallback
if (-not $kernelBssSize) { $kernelBssSize = 0 }
$kSize = (Get-Item "$bareOut\kernel.bin").Length
Write-Output "kernel.bin OK: $kSize bytes"
$isoKernelMax = 1024 * 2048   # ISO layout: kernel occupies up to 1024 x 2048B blocks at LBA24
if ($kSize -gt $isoKernelMax) { throw "kernel too large for ISO (max $isoKernelMax bytes, got $kSize)" }

# 4b) patch the multiboot2 header (entry.s) so GRUB/ESP boot works:
#     Address-tag load_end_addr/bss_end_addr + in-kernel kernel_boot_params
#     (bss_start/bss_size) read by entry.s on both boot paths.
& $python3 "tools\patch_mb2.py" "$bareOut\kernel.bin" $kernelBssStart $kernelBssSize
if ($LASTEXITCODE -ne 0) { throw "patch_mb2 failed" }

# 4c) compressed payload: stub + gzip(kernel.bin).  The 1.14MB kernel cannot
#     be loaded at 0x20000 directly (VGA/ROM hole 0xA0000-0xBFFFF + 16-bit
#     segment wrap + real-mode 1.06MB cap), so boot.s loads a ~507KB payload
#     (stub + raw-DEFLATE kernel) to 0x20000; the stub inflates the kernel to
#     0x100000.  kernel.bin itself stays uncompressed for the GRUB/ESP path.
$stubAs = "D:\CLion\bin\mingw\bin\as.exe"
$stubLd = "D:\CLion\bin\mingw\bin\ld.exe"
$stubGcc = "D:\CLion\bin\mingw\bin\gcc.exe"
& $stubAs --32 "backends\bare\stub.s" -o "$bareOut\stub.o"
if ($LASTEXITCODE -ne 0) { throw "stub.s failed" }
& $g -m32 -ffreestanding -fno-builtin -Os -c "backends\bare\inflate.cpp" -o "$bareOut\inflate32.o"
if ($LASTEXITCODE -ne 0) { throw "inflate.c (m32) failed" }
& $stubLd -mi386pe --image-base 0 -T "backends\bare\stub.ld" "$bareOut\stub.o" "$bareOut\inflate32.o" -o "$bareOut\stub.exe"
if ($LASTEXITCODE -ne 0) { throw "stub link failed" }
& $objcopy -O binary -j .text "$bareOut\stub.exe" "$bareOut\stub.bin"
if ($LASTEXITCODE -ne 0) { throw "stub objcopy failed" }
$stubLen = (Get-Item "$bareOut\stub.bin").Length
if ($stubLen -gt 0x2000) { throw "stub too large: $stubLen bytes" }
Write-Output "stub.bin OK: $stubLen bytes"

# compress kernel.bin -> kernel.gz (raw DEFLATE wbits=-15, zlib level 9)
& $python3 -c "import zlib; d=open(r'$bareOut\kernel.bin','rb').read(); c=zlib.compressobj(9,zlib.DEFLATED,-15); gz=c.compress(d)+c.flush(); open(r'$bareOut\kernel.gz','wb').write(gz); print('kernel.gz OK:',len(d),'->',len(gz))"
if ($LASTEXITCODE -ne 0) { throw "kernel.gz compress failed" }
$gzLen = (Get-Item "$bareOut\kernel.gz").Length
$gzCap = 0x200000   # decompression capacity (>= kernel.bin size)
if ($kSize -gt $gzCap) { $gzCap = $kSize }
# patch stub slots: +0x80 src_off = stub length, +0x84 src_len = gz size,
#                    +0x88 dst_cap
$sf = [IO.File]::OpenWrite("$bareOut\stub.bin")
$sf.Position = 0x80
$sb = [byte[]]::new(12)
[BitConverter]::GetBytes([uint32]$stubLen).CopyTo($sb, 0)
[BitConverter]::GetBytes([uint32]$gzLen).CopyTo($sb, 4)
[BitConverter]::GetBytes([uint32]$gzCap).CopyTo($sb, 8)
$sf.Write($sb, 0, 12)
$sf.Close()
# payload.bin = stub.bin + kernel.gz
$payloadBytes = [IO.File]::ReadAllBytes("$bareOut\stub.bin") + [IO.File]::ReadAllBytes("$bareOut\kernel.gz")
[IO.File]::WriteAllBytes("$bareOut\payload.bin", $payloadBytes)
$payloadLen = $payloadBytes.Length
Write-Output "payload.bin OK: $payloadLen bytes (stub $stubLen + gz $gzLen)"

# 5) boot sector ->
& $objcopy -O binary -j .text "$bareOut\boot.o" "$bareOut\boot.bin"
if ($LASTEXITCODE -ne 0) { throw "boot objcopy failed" }
$bootLen = (Get-Item "$bareOut\boot.bin").Length
if ($bootLen -ne 512) { throw "boot.bin size $bootLen != 512" }
$payloadSize = (Get-Item "$bareOut\payload.bin").Length
$kSectors = [Math]::Ceiling($payloadSize / 512)
$fs = [IO.File]::OpenWrite("$bareOut\boot.bin")
# 0x58�?x7C58閿涘矉绱氶敍?$fs.Position = 0x58
$fs.WriteByte([byte]($kSectors -band 0xFF))
$fs.WriteByte([byte](($kSectors -shr 8) -band 0xFF))
# CD load is fully dynamic in boot.s now (single DAP at 0x7D60 rewritten
# per call); no static chunk patch needed here.
$fs.Close()
Write-Output "boot.bin patched: $kSectors kernel sectors (kernel_count@0x58)"

# 6) ISO閿涘湕l Torito no-emulation閿涙瓬oot.bin 閻╁瓨甯撮弨鎯ф躬 ISO LBA23閿涘ernel.bin 閺€?LBA24�?#    boot.s 闁俺绻?int13 0x42 �?CD 閻╁瓨甯寸拠璇插絿閳ユ柡鈧柧绗夌紒蹇氱�?floppy.img 娑擃參妫跨仦鍌︾礉鐟佸憡婧€閸欘垳娲块幒銉ユ儙閸旑煉�?
$python = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python)) { $python = "python" }
$isoOut = Join-Path $distOut "nefuOS.iso"
& $python "tools\make_iso.py" "$bareOut\boot.bin" "$bareOut\menu.bin" "$bareOut\payload.bin" $isoOut
if ($LASTEXITCODE -ne 0) { throw "make_iso failed" }
Write-Output "ISO OK: $((Get-Item $isoOut).Length) bytes"

# 7) ESP + GRUB (UEFI) 绾句胶娲忛梹婊冨剼閿涘牆褰查柅澶涚礉-GrubEsp閿涘�?
#    GPT 閸掑棗灏悰?+ FAT32 ESP閿涘苯鎯?BOOTX64.EFI閿涘湙RUB閿涘鈧宫rub.cfg閵嗕礁鍙忛柈?GRUB 濡€虫健閸?#    �?multiboot2 婢跺娈?kernel.bin閵嗕敬RUB 闁俺绻?multiboot2 閸楀繗顔呴崝鐘烘祰閸愬懏鐗抽敍灞藉敶閺嶆瓕鍤滅悰?#    鐟欙絾鐎?framebuffer 閺嶅洨顒烽獮璺虹紦缁斿鍨庢�?闂€鎸幠佸蹇ョ礄鐟?backends/bare/entry.s閿涘鈧?
if ($GrubEsp) {
    $grubRoot = Join-Path $root "tools\grub_toolchain"
    $grubCore = Join-Path $grubRoot "extracted\usr\lib\grub\x86_64-efi\monolithic\grubx64.efi"
    $grubMods = Join-Path $grubRoot "extracted\usr\lib\grub\x86_64-efi"
    $grubCfg = Join-Path $root "tools\grub.cfg"
    if (-not (Test-Path $grubCore)) {
        Write-Output "GRUB toolchain missing �?fetching (tools\fetch_grub_toolchain.py) ..."
        & $python "tools\fetch_grub_toolchain.py"
        if ($LASTEXITCODE -ne 0) { throw "fetch_grub_toolchain failed" }
    }
    if (-not (Test-Path $grubCore)) { throw "grubx64.efi still missing after fetch" }
    $espOut = Join-Path $distOut "nefuOS_esp.img"
    & $python "tools\make_esp.py" $grubCore $grubCfg "$bareOut\kernel.bin" $grubMods $espOut
    if ($LASTEXITCODE -ne 0) { throw "make_esp failed" }
    New-Item -ItemType Directory -Force -Path "dist" | Out-Null
    Copy-Item $espOut "dist\nefuOS_esp.img" -Force
    Write-Output "ESP+GRUB image OK: dist\nefuOS_esp.img ($((Get-Item 'dist\nefuOS_esp.img').Length) bytes)"
}

Write-Output "BUILD DONE"

