# nefuOS: host-only (win32) build for quick verification.
# Outputs to $env:TEMP\nefu_fix_test\nefuOS.exe (does NOT touch dist\ or nefu_dist).
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
Set-Location $root

$coreSrc = @(
  "core\nefuos.cpp", "core\klib\memory.cpp", "core\klib\string.cpp", "core\klib\printf.cpp",
  "core\vfs\vfs.cpp", "core\vfs\nvfs.cpp", "core\gui\gfx.cpp", "core\gui\wm.cpp", "core\gui\widgets.cpp", "core\gui\desktop.cpp",
  "core\apps\apps.cpp", "core\apps\terminal.cpp", "core\apps\filemgr.cpp", "core\apps\calc.cpp",
  "core\apps\textview.cpp", "core\apps\sysinfo.cpp", "core\apps\settings.cpp", "core\apps\store.cpp",
  "core\apps\snake.cpp", "core\apps\paint.cpp", "core\apps\clock.cpp", "core\apps\notepad.cpp",
  "core\apps\minesweep.cpp", "core\apps\imageviewer.cpp", "core\apps\music.cpp", "core\apps\videoplayer.cpp", "core\apps\monitor.cpp",
  "core\apps\browser.cpp", "core\apps\netcfg.cpp",
  "core\apps\nefvm.cpp", "core\apps\nefud.cpp", "core\apps\jpeg.cpp", "core\net\net.cpp",
  "core\gui\ttfont.cpp", "core\apps\fontview.cpp", "core\apps\lvgl_demo.cpp",
  "core\gui\lvgl_win.cpp",
  "core\apps\lvgl_desktop.cpp",
  "core\sys\settings.cpp", "core\sys\sha256.cpp", "core\sys\power.cpp",
  "core\apps\wiki.cpp",
  "core\audio.cpp",
  "core\apps\editor.cpp", "core\apps\minijs.cpp", "core\apps\calendar.cpp",
  "core\apps\diskusage.cpp", "core\apps\passgen.cpp", "core\apps\sticky.cpp",
  "core\apps\screenshot.cpp", "core\apps\colorpicker.cpp", "core\apps\search.cpp",
  "core\apps\recyclebin.cpp", "core\apps\weather.cpp", "core\apps\help.cpp",
  "core\apps\dictionary.cpp", "core\apps\taskmgr.cpp", "core\apps\inputmethod.cpp",
  "core\apps\clipboard.cpp", "core\apps\notifcenter.cpp", "core\apps\shortcuts.cpp",
  "core\apps\wallpaper.cpp", "core\apps\theme.cpp", "core\apps\processlist.cpp",
  "core\apps\about_full.cpp", "core\apps\sysinfo_ext.cpp", "core\apps\credits.cpp",
  "core\apps\releasenotes.cpp", "core\apps\installer.cpp", "core\apps\bios.cpp",
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
  "core\simulate\ca.cpp", "core\simulate\particle.cpp", "core\simulate\physics.cpp",
  "core\simulate\fluid.cpp", "core\simulate\lsystem.cpp", "core\simulate\flocking.cpp",
  "core\apps\simlab.cpp",
  "core\mathext\complex.cpp", "core\mathext\matrix.cpp", "core\mathext\poly.cpp",
  "core\mathext\statistics.cpp", "core\mathext\numtheory.cpp", "core\mathext\geometry.cpp",
  "core\apps\mathtool.cpp",
  "core\uiwidgets\widget.cpp", "core\uiwidgets\controls.cpp", "core\uiwidgets\tableview.cpp",
  "core\uiwidgets\chart.cpp", "core\uiwidgets\dialog.cpp", "core\uiwidgets\menu.cpp",
  "core\uiwidgets\painter.cpp", "core\apps\widgetgallery.cpp",
  "core\database\wal.cpp", "core\database\btree.cpp", "core\database\kvstore.cpp",
  "core\database\sqlparse.cpp", "core\database\sqlexec.cpp", "core\database\cursor.cpp",
  "core\apps\dbmanager.cpp",
  "core\apps\games2_util.cpp", "core\apps\breakout.cpp", "core\apps\pong.cpp",
  "core\apps\flappy.cpp", "core\apps\spaceinv.cpp", "core\apps\pacman.cpp",
  "core\apps\tictactoe.cpp", "core\apps\connect4.cpp", "core\apps\minesweeper2.cpp",
  "core\apps\life.cpp",
  "core\ml\matrix.cpp", "core\ml\dataset.cpp", "core\ml\linear.cpp", "core\ml\knn.cpp",`r`n  "core\ml\decisiontree.cpp", "core\ml\randomforest.cpp", "core\ml\kmeans.cpp",`r`n  "core\ml\pca.cpp", "core\ml\neuralnet.cpp", "core\ml\bayes.cpp",`r`n  "core\apps\mllab.cpp",
  "core\termcmds\termcmds_all.cpp", "core\termcmds\filecmd.cpp", "core\termcmds\textcmd.cpp",`r`n  "core\termcmds\syscmd.cpp", "core\termcmds\netcmd.cpp", "core\termcmds\devcmd.cpp",`r`n  "core\termcmds\funcmd.cpp", "core\termcmds\extracmd.cpp",`r`n`r`n
  "core\textlib\levenshtein.cpp", "core\textlib\lcs.cpp", "core\textlib\kmp.cpp",
  "core\textlib\aho.cpp", "core\textlib\token.cpp", "core\textlib\ngram.cpp",
  "core\textlib\regexlite.cpp", "core\textlib\diff.cpp",
  "core\textproc\editdist.cpp", "core\textproc\search.cpp", "core\textproc\trie.cpp",
  "core\textproc\stats.cpp", "core\textproc\phonetic.cpp", "core\textproc\tokenize.cpp",
  "core\apps\texttool.cpp",
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
    "core\crypto\cipher.cpp", "core\crypto\hashx.cpp", "core\crypto\mac.cpp",
    "core\crypto\kdf.cpp", "core\crypto\rng.cpp", "core\crypto\codec.cpp",
    "core\crypto\classic.cpp", "core\crypto\pubkey.cpp",
    "core\compress\rle.cpp", "core\compress\huffman.cpp", "core\compress\lz.cpp",
    "core\compress\bwt.cpp", "core\compress\arith.cpp", "core\compress\dict.cpp",
    "core\compress\other.cpp", "core\compress\compress_all.cpp",
    "core\serialize\textfmt.cpp", "core\serialize\binary.cpp",
    "core\serialize\imagec.cpp", "core\serialize\audiof.cpp",
    "core\audsp\osc.cpp", "core\audsp\env.cpp", "core\audsp\filter.cpp",
    "core\audsp\effect.cpp", "core\audsp\synth.cpp", "core\audsp\analysis.cpp",
    "core\audsp\midi.cpp",
    "core\gfx3d\math3d.cpp", "core\gfx3d\mesh.cpp", "core\gfx3d\raster.cpp",
    "core\gfx3d\light.cpp", "core\gfx3d\scene.cpp", "core\gfx3d\ppm.cpp",
    "core\gfx3d\renderer.cpp", "core\gfx3d\bezier.cpp", "core\gfx3d\shadows.cpp",
    "core\gfx3d\tonemap.cpp", "core\gfx3d\texture.cpp", "core\gfx3d\camera.cpp",
    "core\apps\cryptolab.cpp", "core\apps\compresstool.cpp", "core\apps\serialab.cpp",
    "core\apps\audiolab.cpp", "core\apps\gfx3dview.cpp",

  "third_party\stb_image_wrap.cpp"
)
$lvConfPath = (Resolve-Path "third_party\lvgl_conf\lv_conf.h").Path -replace '\\', '/'
$lvglInclude = @("-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                 "-D", ("LV_CONF_PATH=" + $lvConfPath), "-DNEFU_LVGL_DEMO")
$bareOut = Join-Path $env:TEMP "nefu_build\bare"
$distOut = Join-Path $env:TEMP "nefu_fix_test"
New-Item -ItemType Directory -Force -Path $distOut | Out-Null
$hostExe = Join-Path $distOut "nefuOS.exe"
$lvglHostObjs = Get-ChildItem "$bareOut\lvgl_h_*.o" -File | ForEach-Object { $_.FullName }
if ($lvglHostObjs.Count -lt 100) { throw "lvgl host objects missing ($($lvglHostObjs.Count))" }
$nanosvgObj = "$bareOut\host_nanosvg.o"
& "D:\CLion\bin\mingw\bin\gcc.exe" -O2 -I third_party -c "third_party\nanosvg_impl.c" -o $nanosvgObj
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
    & D:\CLion\bin\mingw\bin\gcc.exe @webpFlags -c $w -o $wobj
    if ($LASTEXITCODE -ne 0) { throw "webp host compile failed: $w" }
  }
  $lvglHostObjs += $wobj
}
$webpImplObj = "$bareOut\host_webp_impl.o"
if (-not (Test-Path $webpImplObj)) {
  & D:\CLion\bin\mingw\bin\gcc.exe @webpFlags -c "third_party\webp_impl.c" -o $webpImplObj
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
    & D:\CLion\bin\mingw\bin\gcc.exe -O2 -w -D_GNU_SOURCE -I "third_party\quickjs" -c "third_party\quickjs\$q" -o $qobj
    if ($LASTEXITCODE -ne 0) { throw "quickjs host compile failed: $q" }
  }
  $lvglHostObjs += $qobj
}
& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Wno-sized-deallocation -I core -I third_party @lvglInclude -o $hostExe ($coreSrc + @("backends\win32\win32.cpp", "backends\win32\media_win32.cpp")) $lvglHostObjs -lgdi32 -luser32 -lgdiplus -lole32 -luuid -lws2_32 -liphlpapi -lwlanapi -lwininet -lwinmm -lmfplat -lmfreadwrite -lmf -lmfuuid -lcomdlg32
if ($LASTEXITCODE -ne 0) { throw "host build failed" }
Write-Output "host exe OK: $((Get-Item $hostExe).Length) bytes -> $hostExe"
