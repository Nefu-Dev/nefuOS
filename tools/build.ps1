#Requires -Version 5.1
<#
    nefuOS unified build driver  (tools\build.ps1)

    What this replaces
    ------------------
    build_iso.ps1 and build_host_only.ps1 each re-implemented the same toolchain
    discovery, source enumeration and link plumbing, and both drifts were real:
    the ISO build left ~230 core files out of the link at one point, the bare
    kernel missed nanosvg, and neither script ever published the artifacts (the
    host exe stayed in %TEMP%, so dist\nefuOS.exe was five days stale).

    What it adds
    ------------
      * one source of truth for every translation unit, enumerated from disk;
      * incremental builds  -- .d dependency files + a toolchain/flag stamp per
        object, so a rebuild only touches what actually changed;
      * parallel compiles   -- a bounded process pool (default cores-4, gcc's
        GGC heap capped) instead of 1300 serial g++ invocations;
      * a tree audit        -- files that exist but are never compiled are
        reported instead of silently drifting out of the build;
      * publish             -- host exe + ISO + version stamp land in dist\;
        the host exe is linked -static, so it no longer needs the three MinGW
        runtime DLLs (libstdc++-6 / libgcc_s_seh-1 / libwinpthread-1);
      * per-stage timing, a build log, and a real failure report.

    Usage
    -----
      .\tools\build.ps1                    # host exe + bootable ISO (default)
      .\tools\build.ps1 -Target host       # host exe only
      .\tools\build.ps1 -Target iso        # bare kernel + ISO only
      .\tools\build.ps1 -Target esp        # iso + GRUB/UEFI ESP image
      .\tools\build.ps1 -Clean             # drop build\obj, then build
      .\tools\build.ps1 -Full              # ignore the incremental cache
      .\tools\build.ps1 -Jobs 10           # compile breadth
      .\tools\build.ps1 -NoCopy            # do not publish into dist\
      .\tools\build.ps1 -AuditOnly         # just report uncompiled files
#>

[CmdletBinding()]
param(
    [ValidateSet('all', 'host', 'iso', 'esp')]
    [string]$Target = 'all',

    [switch]$Clean,
    [switch]$Full,
    [switch]$NoCopy,
    [switch]$AuditOnly,
    [switch]$GrubEsp,

    # 0 = auto (ProcessorCount - 4, clamped to 1..12)
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

# ----------------------------------------------------------------------------
# paths
# ----------------------------------------------------------------------------
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptRoot      # nefuOS repo root
Set-Location -LiteralPath $root

$buildRoot = Join-Path $root 'build'
$objRoot = Join-Path $buildRoot 'obj'
$logRoot = Join-Path $buildRoot 'logs'
$distRoot = Join-Path $root 'dist'
$isoRoot = Join-Path $buildRoot 'iso'

foreach ($d in @($buildRoot, $objRoot, $logRoot, $isoRoot)) {
    if (-not (Test-Path -LiteralPath $d)) {
        New-Item -ItemType Directory -Force -Path $d | Out-Null
    }
}

$logFile = Join-Path $logRoot 'build.log'
$script:stageTimes = New-Object System.Collections.ArrayList
$script:warnings = New-Object System.Collections.ArrayList
$script:runtimeDlls = @()

# ----------------------------------------------------------------------------
# logging helpers (console + log file)
# ----------------------------------------------------------------------------
function Write-Log {
    param([string]$Message, [string]$Color = 'Gray')
    $stamp = (Get-Date).ToString('HH:mm:ss')
    # Write-Host writes to the information stream, which "1> file" does not
    # capture -- redirected runs lost every line. Plain output when redirected,
    # colour when somebody is actually watching the console.
    if ([Console]::IsOutputRedirected) {
        Write-Output $Message
    } else {
        Write-Host $Message -ForegroundColor $Color
    }
    Add-Content -LiteralPath $logFile -Value ("[$stamp] " + $Message) -Encoding UTF8
}

function Write-Warn {
    param([string]$Message)
    Write-Log ("  ! " + $Message) 'Yellow'
    [void]$script:warnings.Add($Message)
}

function Start-Stage {
    param([string]$Name)
    Write-Log ""
    Write-Log ("== " + $Name) 'Cyan'
    $script:stageName = $Name
    $script:stageWatch = [Diagnostics.Stopwatch]::StartNew()
}

function Stop-Stage {
    $elapsed = $script:stageWatch.Elapsed
    [void]$script:stageTimes.Add([pscustomobject]@{ Stage = $script:stageName; Seconds = [Math]::Round($elapsed.TotalSeconds, 1) })
    Write-Log ("   -> " + $script:stageName + " done in " + [Math]::Round($elapsed.TotalSeconds, 1) + " s") 'DarkGray'
}

# ----------------------------------------------------------------------------
# toolchain discovery
# ----------------------------------------------------------------------------
function Find-Tool {
    param([string]$Name, [string[]]$Preferred = @())

    foreach ($p in $Preferred) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

$mingwBin = 'D:\CLion\bin\mingw\bin'
$toolGxx = Find-Tool 'g++.exe'  @((Join-Path $mingwBin 'g++.exe'))
$toolGcc = Find-Tool 'gcc.exe'  @((Join-Path $mingwBin 'gcc.exe'))
$toolAs = Find-Tool 'as.exe'   @((Join-Path $mingwBin 'as.exe'))
$toolLd = Find-Tool 'ld.exe'   @((Join-Path $mingwBin 'ld.exe'))
$toolObjcopy = Find-Tool 'objcopy.exe' @((Join-Path $mingwBin 'objcopy.exe'))
$toolObjdump = Find-Tool 'objdump.exe' @((Join-Path $mingwBin 'objdump.exe'))

$pyCandidates = @(
    'C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe',
    'C:\Users\huawei\AppData\Local\Programs\Python\Python312\python.exe',
    'C:\Users\huawei\AppData\Local\Programs\Python\Python310\python.exe'
)
$toolPy = Find-Tool 'python.exe' $pyCandidates

$missing = @()
foreach ($pair in @(
        @('g++', $toolGxx), @('gcc', $toolGcc), @('as', $toolAs),
        @('ld', $toolLd), @('objcopy', $toolObjcopy), @('python', $toolPy))) {
    if (-not $pair[1]) { $missing += $pair[0] }
}
if ($missing.Count -gt 0) {
    throw ("toolchain incomplete, missing: " + ($missing -join ', ') + ". Install MinGW-w64 (CLion bundle) and Python, or put them on PATH.")
}

# how many compiles may run at once
if ($Jobs -le 0) {
    $Jobs = [Environment]::ProcessorCount - 4
    if ($Jobs -lt 1) { $Jobs = 1 }
    if ($Jobs -gt 12) { $Jobs = 12 }
}
$script:throttle = $Jobs

Write-Log ("nefuOS build  |  target=" + $Target + "  jobs=" + $Jobs + "  root=" + $root) 'White'
Write-Log ("toolchain: " + (Split-Path -Parent $toolGxx)) 'DarkGray'

# ----------------------------------------------------------------------------
# generic process invocation (sequential steps)
# ----------------------------------------------------------------------------
function Invoke-Tool {
    param(
        [string]$Exe,
        [string[]]$Arguments,
        [string]$What
    )

    # Start-Process -PassThru with redirected streams deadlocks in Windows
    # PowerShell 5.1 and leaves ExitCode $null, so drive the exe through the call
    # operator. File redirection (1> / 2>) writes raw text and never raises a
    # NativeCommandError, which is what would happen with 2>&1 under Stop.
    $outFile = Join-Path $logRoot '.last.out'
    $errFile = Join-Path $logRoot '.last.err'

    & $Exe @Arguments 1> $outFile 2> $errFile
    $code = $LASTEXITCODE
    # this tool just rewrote files the freshness checks look at; a memoised
    # timestamp from before the call would claim downstream steps are current
    Reset-MtimeCache

    $out = ''
    if (Test-Path -LiteralPath $outFile) { $out = [IO.File]::ReadAllText($outFile) }
    $err = ''
    if (Test-Path -LiteralPath $errFile) { $err = [IO.File]::ReadAllText($errFile) }
    $text = $err.Trim()

    if ($code -ne 0) {
        Write-Log ("FAILED: " + $What) 'Red'
        if ($text) { Write-Log $text 'Red' }
        throw ($What + " failed (exit " + $code + "): " + (Split-Path -Leaf $Exe))
    }
    if ($text) {
        # gcc warnings on the sequential steps are worth keeping visible
        Write-Log $text 'DarkYellow'
    }
    return $out
}

# ----------------------------------------------------------------------------
# incremental engine
# ----------------------------------------------------------------------------
function Get-TextHash {
    param([string]$Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        $hash = $sha.ComputeHash($bytes)
        return ([BitConverter]::ToString($hash) -replace '-', '').Substring(0, 16)
    } finally {
        $sha.Dispose()
    }
}

function Read-DepList {
    param([string]$Path)

    $raw = [IO.File]::ReadAllText($Path)
    # line continuations first, then newlines; both are plain char replacements
    $raw = $raw.Replace("\`r`n", ' ').Replace("\`n", ' ').Replace("`r`n", ' ').Replace("`n", ' ')
    # gcc writes "<target>: <dep> <dep> ...". The target here is an absolute
    # Windows path, so its drive colon comes first: cutting at the first ':' would
    # leave "eOS\build\obj\..." as a "dependency" and every object would look
    # stale. The real separator is ": " (colon followed by space).
    $idx = $raw.IndexOf(': ')
    if ($idx -lt 0) { $idx = $raw.IndexOf(':') }
    if ($idx -lt 0) { return @() }
    $rest = $raw.Substring($idx + 1)
    # string.Split with the exact delimiter set beats -split '\s+' here: this runs
    # for every dependency of every object (~25k tokens on a full tree)
    $seps = [char[]]@(' ', "`t", "`r", "`n")
    return [string[]]($rest.Split($seps, [StringSplitOptions]::RemoveEmptyEntries))
}

# Freshness checks run once per translation unit and once per dependency, which
# comes to ~20k lookups on a full tree. Test-Path/Get-Item cost about a
# millisecond each through the provider layer (40 s of a "nothing to do" build),
# so go straight to .NET and memoise: headers repeat across hundreds of TUs.
$script:mtimeCache = @{}

function Get-FileTimeUtc {
    param([string]$Path)

    if ($script:mtimeCache.ContainsKey($Path)) { return $script:mtimeCache[$Path] }
    $time = [DateTime]::MinValue
    if ([IO.File]::Exists($Path)) { $time = [IO.File]::GetLastWriteTimeUtc($Path) }
    $script:mtimeCache[$Path] = $time
    return $time
}

# Every step that writes a file has to drop the cache: the whole point is
# "does this output predate its inputs", and a remembered pre-write timestamp
# makes a stale output look current. Clearing is cheap - lookups refill it.
function Reset-MtimeCache {
    if ($script:mtimeCache.Count -gt 0) { $script:mtimeCache.Clear() }
}

function Test-OutputFresh {
    param([string]$Output, [string[]]$Inputs)

    if ($Full) { return $false }
    $outTime = Get-FileTimeUtc $Output
    if ($outTime -eq [DateTime]::MinValue) { return $false }
    foreach ($i in $Inputs) {
        $t = Get-FileTimeUtc $i
        if ($t -eq [DateTime]::MinValue) { return $false }
        if ($t -gt $outTime) { return $false }
    }
    return $true
}

function Test-ObjFresh {
    param($Item)

    # NEFU_BUILD_WHY=1 prints the reason a unit was rebuilt; without it the
    # hot loop stays quiet.
    $why = ''

    if ($Full) { $why = 'forced by -Full' }
    elseif (-not [IO.File]::Exists($Item.Obj)) { $why = 'no object file' }
    elseif (-not [IO.File]::Exists($Item.Meta)) { $why = 'no stamp file' }
    elseif (-not [IO.File]::Exists($Item.Dep)) { $why = 'no dependency file' }
    else {
        $stamp = ([IO.File]::ReadAllText($Item.Meta)).Trim()
        if ($stamp -ne $Item.Stamp) {
            $why = 'compiler or flags changed'
        } else {
            $objTime = Get-FileTimeUtc $Item.Obj
            foreach ($dep in (Read-DepList $Item.Dep)) {
                $depTime = Get-FileTimeUtc $dep
                if ($depTime -eq [DateTime]::MinValue) { $why = 'dependency vanished: ' + $dep; break }
                if ($depTime -gt $objTime) {
                    $why = 'newer dependency: ' + $dep
                    break
                }
            }
        }
    }

    if (-not $why) { return $true }

    if ($env:NEFU_BUILD_WHY -eq '1') {
        Write-Log ("    rebuild " + $Item.Name + ": " + $why) 'DarkGray'
    }
    return $false
}

function New-CompileItem {
    param(
        [string]$Name,
        [string]$Src,
        [string]$Obj,
        [string]$Compiler,
        [string[]]$Flags
    )

    $dep = [IO.Path]::ChangeExtension($Obj, '.d')
    $meta = $Obj + '.stamp'
    $stamp = Get-TextHash ($Compiler + "|" + ($Flags -join ' '))
    return [pscustomobject]@{
        Name     = $Name
        Src      = $Src
        Obj      = $Obj
        Dep      = $dep
        Meta     = $meta
        Compiler = $Compiler
        Flags    = $Flags
        Stamp    = $stamp
    }
}

function Split-Shards {
    param([string[]]$Lines, [int]$Count)

    # One manifest string per shard. Translation units differ by an order of
    # magnitude in compile time (browser.cpp is 235 KB, most libs a few KB), so
    # deal the lines round-robin instead of slicing by count.
    $buckets = @()
    for ($i = 0; $i -lt $Count; $i++) { $buckets += , (New-Object System.Collections.ArrayList) }

    $idx = 0
    foreach ($line in $Lines) {
        [void]$buckets[$idx % $Count].Add($line)
        $idx++
    }

    $manifests = @()
    for ($i = 0; $i -lt $Count; $i++) {
        if ($buckets[$i].Count -gt 0) { $manifests += (@($buckets[$i]) -join "`n") }
    }
    return $manifests
}

function Compile-Batch {
    param([object[]]$Items, [string]$Label)

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $toDo = @()
    $cached = 0
    foreach ($it in $Items) {
        if (Test-ObjFresh $it) { $cached++ } else { $toDo += $it }
    }

    Write-Log ("  [{0}] {1} units: {2} cached, {3} compiling" -f $Label, $Items.Count, $cached, $toDo.Count)

    if ($toDo.Count -eq 0) {
        if ($env:NEFU_BUILD_TIME -eq '1') { Write-Log ("    (" + $Label + " decided in " + $sw.Elapsed.TotalSeconds.ToString('0.0') + " s)") 'DarkGray' }
        return
    }

    # A job boundary carries one flat string, never nested arrays: a single-unit
    # shard would collapse to its first element and the job would see fields
    # instead of records. One compact JSON object per line survives the
    # serialization intact (a control-char separator does not).
    $lines = @()
    foreach ($it in $toDo) {
        # -c is what makes this a compile instead of a link: without it gcc
        # consumes the .cpp and immediately tries to produce an executable
        $argv = @($it.Flags) + @('-c', '-MMD', '-MF', $it.Dep, $it.Src, '-o', $it.Obj)
        $record = @{
            c   = $it.Compiler
            a   = @($argv)
            m   = $it.Meta
            s   = $it.Stamp
            n   = $it.Name
            src = $it.Src
        }
        $lines += ($record | ConvertTo-Json -Depth 3 -Compress)
    }

    $shardCount = [Math]::Min($script:throttle, $lines.Count)
    $manifests = @(Split-Shards -Lines $lines -Count $shardCount)

    $jobs = @()
    $shardNo = 0
    foreach ($manifest in $manifests) {
        $shardNo++
        $jobs += Start-Job -Name ("compile_" + $shardNo) -ScriptBlock {
            param($manifest, $logRoot, $repoRoot)

            Set-Location -LiteralPath $repoRoot
            $worked = 0
            foreach ($line in @($manifest -split "`n")) {
                if (-not $line.Trim()) { continue }
                $rec = $line | ConvertFrom-Json
                $compiler = $rec.c
                $argv = @($rec.a)
                $meta = $rec.m
                $stamp = $rec.s
                $name = $rec.n

                $outFile = Join-Path $logRoot ($name + '.out')
                $errFile = Join-Path $logRoot ($name + '.err')

                & $compiler @argv > $outFile 2> $errFile
                if ($LASTEXITCODE -eq 0) {
                    [IO.File]::WriteAllText($meta, $stamp)
                    Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue
                    if (Test-Path -LiteralPath $errFile) {
                        $warn = [IO.File]::ReadAllText($errFile)
                        if (-not $warn.Trim()) { Remove-Item -LiteralPath $errFile -Force -ErrorAction SilentlyContinue }
                    }
                }
                $worked++
            }
            return $worked
        } -ArgumentList $manifest, $logRoot, $root
    }

    if ($jobs.Count -eq 0) { return }

    # success is judged by the stamp each job writes, so no exit code has to
    # survive the job boundary
    Wait-Job -Job $jobs | Out-Null
    $compiled = 0
    foreach ($j in $jobs) {
        $compiled += [int](@(Receive-Job -Job $j)[0])
        Remove-Job -Job $j -Force -ErrorAction SilentlyContinue
    }

    $failed = @()
    foreach ($it in $toDo) {
        # The stamp alone is not proof: it survives a compile that was killed
        # before gcc ever produced output (the machine ran out of commit here),
        # and the old stamp then matched and the missing object only surfaced as
        # "ld.exe: cannot find ...o" at link time.
        if (-not [IO.File]::Exists($it.Obj)) { $failed += $it; continue }
        if (-not (Test-Path -LiteralPath $it.Meta)) { $failed += $it; continue }
        $stamp = ([IO.File]::ReadAllText($it.Meta)).Trim()
        if ($stamp -ne $it.Stamp) { $failed += $it }
    }

    if ($failed.Count -gt 0) {
        foreach ($it in $failed) {
            Write-Log ("   compile FAILED: " + $it.Src) 'Red'
            $errFile = Join-Path $logRoot ($it.Name + '.err')
            if (Test-Path -LiteralPath $errFile) {
                $text = [IO.File]::ReadAllText($errFile)
                $head = @($text -split "`n" | Select-Object -First 25) -join "`n"
                Write-Log $head 'Red'
            }
        }
        throw ("[" + $Label + "] " + $failed.Count + " of " + $toDo.Count + " units failed to compile")
    }

    Write-Log ("  [{0}] {1} compiled across {2} shards" -f $Label, $compiled, $shardCount) 'DarkGray'
    Reset-MtimeCache
}

function Clear-Objects {
    param([string]$Filter)

    $dirs = Get-ChildItem -LiteralPath $objRoot -Directory -ErrorAction SilentlyContinue
    foreach ($d in $dirs) {
        if (-not $Filter -or $d.Name -like $Filter) {
            Remove-Item -LiteralPath $d.FullName -Recurse -Force
        }
    }
}

# ----------------------------------------------------------------------------
# source enumeration (one source of truth)
# ----------------------------------------------------------------------------
# Retired / superseded files that must stay out of the link. Each entry has a
# reason; anything else that is skipped is reported by the audit below.
$coreExclude = @(
    'core\apps\calculator.cpp',      # replaced by core\apps\calc.cpp
    'core\apps\html5.cpp',           # replaced by browser_engine.cpp
    'core\apps\lockscreen.cpp',      # merged into gui\desktop.cpp
    'core\apps\recycle.cpp',         # replaced by recyclebin.cpp
    'core\apps\stubs.cpp',           # stub bodies, superseded
    'core\apps\terminal_commands.cpp', # merged into terminal.cpp
    'core\apps\terminal_extra.cpp',  # merged into term_ext.cpp
    'core\sys\sysapi_extended.cpp',  # duplicate of sysapi.cpp symbols
    'core\net\tls13_fixed.cpp',      # duplicate of tls13.cpp symbols
    'core\stdcxx\test_stdcxx.cpp'    # defines main(), collides with win32.cpp
)

$allCpp = @(Get-ChildItem -LiteralPath 'core' -Recurse -Filter *.cpp |
    ForEach-Object { $_.FullName.Substring($root.Length + 1) } |
    Sort-Object)

$coreSrc = @($allCpp | Where-Object { $coreExclude -notcontains $_ })

# third_party units that are genuinely part of the product
$thirdPartySrc = @('third_party\stb_image_wrap.cpp')
$hostOnlySrc = @(
    'backends\win32\win32.cpp',
    'backends\win32\ws_win32.cpp',
    'backends\win32\media_win32.cpp'
)
$bareOnlySrc = @(
    'backends\bare\bare.cpp',
    'backends\bare\softfloat.cpp',
    'backends\bare\bare_libc.cpp'
)
# assembled sources: boot sector, kernel entry, multiboot picker, 16-bit stub,
# plus the setjmp/longjmp shim kept for the MicroPython NLR path (currently
# unreferenced, --gc-sections drops it from the image)
$bareAsmSrc = @(
    'backends\bare\boot.s',
    'backends\bare\entry.s',
    'backends\bare\menu.s',
    'backends\bare\stub.s',
    'backends\bare\setjmp.S'
)

# numeric.cpp is double-based and the bare kernel has no soft-double path, so it
# is host-only. minijs.cpp is the one TU that needs the FPU registers model.
$bareSkip = @('core\algo\numeric.cpp')

$lvglRoot = 'third_party\lvgl_src\lvgl-9.2.0'
$lvglSrc = @(Get-ChildItem -LiteralPath (Join-Path $lvglRoot 'src') -Recurse -Filter *.c |
    ForEach-Object { $_.FullName })

$webpSrc = @(
    'third_party\libwebp\src\dec\alpha_dec.c', 'third_party\libwebp\src\dec\buffer_dec.c',
    'third_party\libwebp\src\dec\frame_dec.c', 'third_party\libwebp\src\dec\idec_dec.c',
    'third_party\libwebp\src\dec\io_dec.c', 'third_party\libwebp\src\dec\quant_dec.c',
    'third_party\libwebp\src\dec\tree_dec.c', 'third_party\libwebp\src\dec\vp8_dec.c',
    'third_party\libwebp\src\dec\vp8l_dec.c', 'third_party\libwebp\src\dec\webp_dec.c',
    'third_party\libwebp\src\dsp\alpha_processing.c', 'third_party\libwebp\src\dsp\cpu.c',
    'third_party\libwebp\src\dsp\dec.c', 'third_party\libwebp\src\dsp\dec_clip_tables.c',
    'third_party\libwebp\src\dsp\filters.c', 'third_party\libwebp\src\dsp\lossless.c',
    'third_party\libwebp\src\dsp\rescaler.c', 'third_party\libwebp\src\dsp\upsampling.c',
    'third_party\libwebp\src\dsp\yuv.c',
    'third_party\libwebp\src\dsp\alpha_processing_sse2.c', 'third_party\libwebp\src\dsp\dec_sse2.c',
    'third_party\libwebp\src\dsp\filters_sse2.c', 'third_party\libwebp\src\dsp\lossless_sse2.c',
    'third_party\libwebp\src\dsp\rescaler_sse2.c', 'third_party\libwebp\src\dsp\upsampling_sse2.c',
    'third_party\libwebp\src\dsp\yuv_sse2.c',
    'third_party\libwebp\src\utils\bit_reader_utils.c', 'third_party\libwebp\src\utils\color_cache_utils.c',
    'third_party\libwebp\src\utils\filters_utils.c', 'third_party\libwebp\src\utils\huffman_utils.c',
    'third_party\libwebp\src\utils\palette.c', 'third_party\libwebp\src\utils\quant_levels_dec_utils.c',
    'third_party\libwebp\src\utils\random_utils.c', 'third_party\libwebp\src\utils\rescaler_utils.c',
    'third_party\libwebp\src\utils\thread_utils.c', 'third_party\libwebp\src\utils\utils.c'
)

$qjsSrc = @('quickjs.c', 'libregexp.c', 'libunicode.c', 'cutils.c', 'dtoa.c')

# wasm3 (MIT) backs the WebAssembly object in the browser JS engine; bloxd.io
# refuses to boot without it. Only the interpreter core is vendored - the wasi
# and tracer API layers are not compiled because nothing imports them.
$wasm3Src = @(
    'third_party\wasm3\m3_bind.c', 'third_party\wasm3\m3_code.c',
    'third_party\wasm3\m3_compile.c', 'third_party\wasm3\m3_core.c',
    'third_party\wasm3\m3_emit.c', 'third_party\wasm3\m3_env.c',
    'third_party\wasm3\m3_exec.c', 'third_party\wasm3\m3_function.c',
    'third_party\wasm3\m3_info.c', 'third_party\wasm3\m3_module.c',
    'third_party\wasm3\m3_optimize.c', 'third_party\wasm3\m3_parse.c',
    'third_party\wasm3\m3_api_libc.c'
)

Write-Log ("sources: core " + $coreSrc.Count + " (+" + $thirdPartySrc.Count + " third_party), lvgl " + $lvglSrc.Count +
    ", webp " + $webpSrc.Count + ", quickjs " + $qjsSrc.Count + ", wasm3 " + $wasm3Src.Count) 'DarkGray'

# ----------------------------------------------------------------------------
# audit: anything in the tree that is never compiled
# ----------------------------------------------------------------------------
function Invoke-Audit {
    param([string[]]$Built)

    $known = @{
        'backends\win32\ttftest.cpp'    = 'defines main(), standalone TTF probe'
        'backends\win32\ttfstub.cpp'    = 'standalone stub, not linked'
        'third_party\stbtt_nefu.cpp'    = 'alternate stbtt impl; ttfont.cpp owns the symbols'
        'core\apps\browser_old.cpp.bak' = 'backup file'
        'backends\bare\boot_hd.s'       = 'legacy HD boot variant, superseded by boot.s'
        'backends\bare\test_boot.s'     = 'boot experiment, kept for reference'
        'backends\bare\test_boot2.s'    = 'boot experiment, kept for reference'
        'backends\bare\test_payload.s'  = 'payload experiment, kept for reference'
    }
    foreach ($e in $coreExclude) { $known[$e] = 'retired / superseded' }

    $builtFull = @{}
    foreach ($b in $Built) { $builtFull[(Resolve-Path -LiteralPath $b).Path] = $true }

    $skipped = @()
    $candidates = @()

    # NOTE: -Include combined with -LiteralPath silently matches everything here
    # (it filters the container, not the children), so filter after the walk.
    $sourceExts = @('.cpp', '.c', '.cc', '.cxx', '.S')
    $candidates += @(Get-ChildItem -LiteralPath 'backends' -Recurse -File |
        Where-Object { $sourceExts -contains $_.Extension } |
        ForEach-Object { $_.FullName.Substring($root.Length + 1) })
    $candidates += @(Get-ChildItem -LiteralPath 'third_party' -File |
        Where-Object { $sourceExts -contains $_.Extension } |
        ForEach-Object { $_.FullName.Substring($root.Length + 1) })
    $candidates += @(Get-ChildItem -LiteralPath 'core' -Recurse -File |
        Where-Object { $sourceExts -contains $_.Extension } |
        ForEach-Object { $_.FullName.Substring($root.Length + 1) })

    foreach ($c in ($candidates | Sort-Object -Unique)) {
        if ($builtFull.ContainsKey((Resolve-Path -LiteralPath $c).Path)) { continue }
        if ($known.ContainsKey($c)) { continue }
        # vendored libraries are reported separately below
        if ($c -like 'third_party\*') { continue }
        $skipped += $c
    }

    if ($skipped.Count -gt 0) {
        Write-Warn ("files present but never compiled: " + ($skipped -join ', '))
    } else {
        Write-Log "  audit: every backend/core source is in the build" 'DarkGray'
    }

    foreach ($dir in @('third_party\micropython', 'third_party\hubbub')) {
        if (Test-Path -LiteralPath $dir) {
            Write-Log ("  audit: " + $dir + " is vendored but not compiled (no consumer in core yet)") 'DarkGray'
        }
    }
}

if ($AuditOnly) {
    $built = @($coreSrc + $thirdPartySrc + $hostOnlySrc + $bareOnlySrc + $bareAsmSrc)
    Invoke-Audit -Built $built
    exit 0
}

# ----------------------------------------------------------------------------
# flag sets  (kept byte-identical to the old build so the output is comparable)
# ----------------------------------------------------------------------------
$lvConfPath = (Resolve-Path -LiteralPath 'third_party\lvgl_conf\lv_conf.h').Path -replace '\\', '/'
$lvConfDefine = 'LV_CONF_PATH=' + $lvConfPath
$lvglInclude = @('-I', 'third_party\lvgl_conf', '-I', 'third_party\lvgl_src\lvgl-9.2.0', '-D', $lvConfDefine)

# bounded GGC heap: with many parallel cc1plus processes the default target lets
# the compiler balloon and die with "out of memory allocating ..."
$memCap = @('--param', 'ggc-min-expand=10', '--param', 'ggc-min-heapsize=65536')

$hostCxxFlags = @(
    '-std=c++17', '-O2', '-fno-exceptions', '-fno-rtti', '-fno-builtin',
    '-Wall', '-Wextra', '-Wno-sized-deallocation',
    '-I', 'core', '-I', 'third_party', '-DNEFU_LVGL_DEMO'
) + $lvglInclude + $memCap

$bareCxxFlags = @(
    '-std=c++17', '-fno-exceptions', '-fno-rtti', '-fno-builtin',
    '-fno-stack-protector', '-mno-red-zone', '-O2', '-Wall', '-Wextra',
    '-ffunction-sections', '-fdata-sections', '-fno-asynchronous-unwind-tables',
    '-Wno-sized-deallocation', '-DNEFU_BARE', '-DNEFU_LVGL_DEMO',
    '-I', 'core', '-I', 'third_party', '-I', 'third_party\lvgl_conf',
    '-I', 'third_party\lvgl_src\lvgl-9.2.0', '-D', $lvConfDefine
) + $memCap

# bare.cpp hosts the IDT/ISRs; GCC's interrupt attribute requires the integer
# register model, so the platform file alone keeps -mgeneral-regs-only.
$bareIntFlags = $bareCxxFlags + @('-mgeneral-regs-only')

$lvglHostFlags = @(
    '-std=gnu11', '-O2', '-Wall', '-I', 'third_party',
    '-I', 'third_party\lvgl_conf', '-I', 'third_party\lvgl_src\lvgl-9.2.0', '-D', $lvConfDefine
) + $memCap

$lvglBareFlags = @(
    '-std=gnu11', '-ffreestanding', '-fno-stack-protector', '-mno-red-zone',
    '-O2', '-Wall', '-ffunction-sections', '-fdata-sections',
    '-fno-asynchronous-unwind-tables', '-I', 'third_party',
    '-I', 'third_party\lvgl_conf', '-I', 'third_party\lvgl_src\lvgl-9.2.0', '-D', $lvConfDefine
) + $memCap

$webpCommon = @(
    '-I', 'third_party\libwebp', '-I', 'third_party',
    '-include', 'third_party\webp_impl.h',
    '-DWEBP_MALLOC=nefu_kalloc_x', '-DWEBP_CALLOC=nefu_kcalloc_x',
    '-DWEBP_FREE=nefu_kfree_x', '-DWEBP_REALLOC=nefu_krealloc_x'
)
$webpHostFlags = @('-O2') + $webpCommon + $memCap
$webpBareFlags = @(
    '-std=gnu11', '-ffreestanding', '-fno-stack-protector', '-mno-red-zone', '-O2', '-Wall',
    '-ffunction-sections', '-fdata-sections', '-fno-asynchronous-unwind-tables'
) + $webpCommon + $memCap

$qjsHostFlags = @('-O2', '-w', '-D_GNU_SOURCE', '-I', 'third_party\quickjs') + $memCap
$qjsBareFlags = @(
    '-std=gnu11', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
    '-fno-asynchronous-unwind-tables', '-Os', '-w', '-D_GNU_SOURCE',
    '-DCONFIG_DISABLE_ATOMICS', '-DNDEBUG',
    '-I', 'third_party\quickjs', '-I', 'third_party'
) + $memCap

$nanosvgHostFlags = @('-O2', '-I', 'third_party') + $memCap
# wasm3 ships its own d_m3HasFloat switch; the host has an FPU so f64 paths
# compile normally. -w silences the vendored code's own warnings.
$wasm3HostFlags = @('-std=gnu11', '-O2', '-w', '-Dd_m3HasFloat=1', '-I', 'third_party\wasm3') + $memCap
$nanosvgBareFlags = @(
    '-std=gnu11', '-fno-stack-protector', '-mno-red-zone', '-O2',
    '-ffunction-sections', '-fdata-sections', '-fno-asynchronous-unwind-tables',
    '-I', 'third_party'
) + $memCap

# ----------------------------------------------------------------------------
# object path helper: build\obj\<group>\<flattened source>.o
# ----------------------------------------------------------------------------
function Get-ObjPath {
    param([string]$Group, [string]$Src)

    $dir = Join-Path $objRoot $Group
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    # LVGL is enumerated with absolute paths ($_.FullName); a drive letter left
    # in the name would make "D:\..." an invalid file name ("C:_repo_...").
    $rel = $Src
    if ($rel.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        $rel = $rel.Substring($root.Length).TrimStart('\', '/')
    }
    $flat = ($rel -replace '[\\/:]', '_') -replace '\.\w+$', '.o'
    return (Join-Path $dir $flat)
}

function New-GroupItems {
    param([string]$Group, [string]$Compiler, [string[]]$Flags, [string[]]$Sources)

    $items = @()
    foreach ($s in $Sources) {
        if (-not (Test-Path -LiteralPath $s)) { throw ("source missing: " + $s) }
        $obj = Get-ObjPath -Group $Group -Src $s
        # log names must be unique: several directories hold a file with the same
        # leaf name (env.cpp lives in audlib, audsp and minilang), and two parallel
        # compiles redirecting into one .err file would fail on sharing.
        $name = $Group + '_' + [IO.Path]::GetFileNameWithoutExtension($obj)
        $items += New-CompileItem -Name $name -Src $s -Obj $obj -Compiler $Compiler -Flags $Flags
    }
    return $items
}

# ----------------------------------------------------------------------------
# stage 0: prechecks + admin hash
# ----------------------------------------------------------------------------
Start-Stage 'precheck'

foreach ($f in @('tools\admin_hash.py', 'tools\rebin_kernel.py', 'tools\patch_mb2.py', 'tools\make_iso.py',
        'backends\bare\linker.ld', 'backends\bare\stub.ld')) {
    $abs = Join-Path $root $f
    if (-not (Test-Path -LiteralPath $abs)) { throw ("required build input missing: " + $f) }
}

$adminPw = [Environment]::GetEnvironmentVariable('NEFU_ADMIN_PASSWORD')
$adminArgs = @('tools\admin_hash.py')
if ($adminPw) { $adminArgs += $adminPw }

# admin_hash.py writes a random placeholder hash when no password is supplied, so
# its header changed on every build -- which forced a rebuild of every TU that
# includes it on a no-op build. Password case: run the script, and put the old
# timestamp back when the bytes came out identical. No password case: write a
# fixed deny hash ourselves (still unguessable, still "login denied"), and only
# when the content actually differs.
$adminHeader = Join-Path $root 'core\sys\admin_hash.h'

if ($adminPw) {
    $adminBefore = $null
    if (Test-Path -LiteralPath $adminHeader) {
        $adminBefore = @{
            Bytes = [IO.File]::ReadAllBytes($adminHeader)
            Time  = (Get-Item -LiteralPath $adminHeader).LastWriteTimeUtc
        }
    }
    [void](Invoke-Tool -Exe $toolPy -Arguments $adminArgs -What 'admin hash')
    if ($adminBefore -and (Test-Path -LiteralPath $adminHeader)) {
        $adminAfter = [IO.File]::ReadAllBytes($adminHeader)
        if ([Convert]::ToBase64String($adminBefore.Bytes) -eq [Convert]::ToBase64String($adminAfter)) {
            (Get-Item -LiteralPath $adminHeader).LastWriteTimeUtc = $adminBefore.Time
        }
    }
} else {
    # same salt and file layout as tools\admin_hash.py, so the kernel's sha256
    # check keeps working
    $salt = 'nefuos-admin-salt-v1'
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $hex = ([BitConverter]::ToString($sha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($salt + 'nefuos-deny-no-password'))) -replace '-', '').ToLower()
    } finally { $sha.Dispose() }

    $text = "// auto-generated at build time; contains ONLY a salted SHA-256 hash`n" +
            "#define NEFU_ADMIN_HASH `"$hex`"`n"

    $current = ''
    if (Test-Path -LiteralPath $adminHeader) { $current = [IO.File]::ReadAllText($adminHeader) }
    if ($current -ne $text) {
        [IO.File]::WriteAllText($adminHeader, $text, (New-Object Text.ASCIIEncoding))
        Write-Log '  admin hash: no NEFU_ADMIN_PASSWORD, wrote deny hash (login denied)' 'DarkGray'
    } else {
        Write-Log '  admin hash: no NEFU_ADMIN_PASSWORD, existing deny hash is current' 'DarkGray'
    }
}

Stop-Stage

if ($Clean) {
    Start-Stage 'clean'
    Remove-Item -LiteralPath $objRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $objRoot | Out-Null
    Write-Log "  build\obj wiped" 'DarkGray'
    Stop-Stage
}

# ----------------------------------------------------------------------------
# stage 1: host exe
# ----------------------------------------------------------------------------
$hostExe = Join-Path $buildRoot 'nefuOS.exe'

if ($Target -eq 'all' -or $Target -eq 'host') {

    Start-Stage 'host build'

    $hostCore = New-GroupItems -Group 'host_core' -Compiler $toolGxx -Flags $hostCxxFlags `
        -Sources (@($coreSrc + $thirdPartySrc + $hostOnlySrc))
    Compile-Batch -Items $hostCore -Label 'host core'

    $lvglHost = New-GroupItems -Group 'host_lvgl' -Compiler $toolGcc -Flags $lvglHostFlags -Sources $lvglSrc
    Compile-Batch -Items $lvglHost -Label 'host lvgl'

    $webpHost = New-GroupItems -Group 'host_webp' -Compiler $toolGcc -Flags $webpHostFlags `
        -Sources (@($webpSrc + @('third_party\webp_impl.c')))
    Compile-Batch -Items $webpHost -Label 'host webp'

    $qjsHost = New-GroupItems -Group 'host_qjs' -Compiler $toolGcc -Flags $qjsHostFlags `
        -Sources @($qjsSrc | ForEach-Object { 'third_party\quickjs\' + $_ })
    Compile-Batch -Items $qjsHost -Label 'host quickjs'

    $nsvgHost = New-GroupItems -Group 'host_nanosvg' -Compiler $toolGcc -Flags $nanosvgHostFlags `
        -Sources @('third_party\nanosvg_impl.c')
    Compile-Batch -Items $nsvgHost -Label 'host nanosvg'

    $wasm3Host = New-GroupItems -Group 'host_wasm3' -Compiler $toolGcc -Flags $wasm3HostFlags `
        -Sources $wasm3Src
    Compile-Batch -Items $wasm3Host -Label 'host wasm3'

    # ---- link: response file, because ~460 objects blow past the 32KB command
    # line limit and CreateProcess would fail before g++ even starts.
    $allObjs = @()
    foreach ($set in @($hostCore, $lvglHost, $webpHost, $qjsHost, $nsvgHost, $wasm3Host)) {
        foreach ($it in $set) { $allObjs += $it.Obj }
    }

    $hostLinkArgs = @(
        '-std=c++17', '-O2', '-fno-exceptions', '-fno-rtti', '-fno-builtin',
        '-Wall', '-Wextra', '-Wno-sized-deallocation', '-I', 'core', '-I', 'third_party',
        '-DNEFU_LVGL_DEMO'
    ) + $lvglInclude + $memCap + @('-static-libgcc', '-static-libstdc++', '-o', $hostExe) + $allObjs + @(
        '-lgdi32', '-luser32', '-lgdiplus', '-lole32', '-lws2_32', '-liphlpapi',
        '-lwlanapi', '-lwininet', '-lwinhttp', '-lwinmm', '-lmfplat', '-lmfreadwrite', '-lmf',
        '-lmfuuid', '-lcomdlg32'
    )

    $rsp = Join-Path $buildRoot 'link_host.rsp'
    $lines = @()
    foreach ($a in $hostLinkArgs) {
        $v = $a -replace '\\', '/'
        if ($v -match '\s') { $v = '"' + $v + '"' }
        $lines += $v
    }

    # WriteAllLines terminates the last line, so the expected text has to end
    # with the same newline or every run looks like the link line changed
    $rspText = ($lines -join "`r`n") + "`r`n"
    $rspOld = ''
    if (Test-Path -LiteralPath $rsp) { $rspOld = [IO.File]::ReadAllText($rsp) }
    $rspChanged = ($rspOld -ne $rspText)
    if ($rspChanged) {
        [IO.File]::WriteAllLines($rsp, $lines, (New-Object Text.ASCIIEncoding))
    }

    # linking 460 objects is the slowest single step of a no-op build: skip it
    # when the exe is newer than every object and the link line is unchanged
    if (-not $Full -and -not $rspChanged -and
        (Test-OutputFresh -Output $hostExe -Inputs $allObjs)) {
        Write-Log "  host link: up to date, skipped" 'DarkGray'
    } else {
        Invoke-Tool -Exe $toolGxx -Arguments @('@' + $rsp) -What 'host link'
    }

    $exeSize = (Get-Item -LiteralPath $hostExe).Length
    Write-Log ("  host exe: " + $exeSize + " bytes") 'Green'

    # -static is unusable in this toolchain: GCC 15.2's libwinpthread.a calls
    # __intrinsic_setjmpex, which nothing in the link provides, so any std::thread
    # user fails to link. -static-libgcc/-static-libstdc++ still kills two of the
    # three runtime DLLs; whatever is left gets copied next to the exe and into
    # dist\ so the folder stays portable.
    if ($toolObjdump) {
        $deps = @()
        $dump = Invoke-Tool -Exe $toolObjdump -Arguments @('-p', $hostExe) -What 'objdump -p'
        foreach ($line in ($dump -split "`n")) {
            if ($line -match 'DLL Name:\s*(\S+)') { $deps += $matches[1] }
        }
        $mingwDeps = @($deps | Where-Object {
            $_ -like 'libstdc++*' -or $_ -like 'libgcc*' -or $_ -like 'libwinpthread*'
        })
        $script:runtimeDlls = @()
        foreach ($dep in $mingwDeps) {
            $src = Join-Path $mingwBin $dep
            if (Test-Path -LiteralPath $src) {
                Copy-Item -LiteralPath $src -Destination (Join-Path (Split-Path -Parent $hostExe) $dep) -Force
                $script:runtimeDlls += $dep
                Write-Log ("  shipping runtime DLL next to the exe: " + $dep) 'DarkYellow'
            } else {
                Write-Warn ("host exe needs " + $dep + " but it is not in " + $mingwBin)
            }
        }
        if ($mingwDeps.Count -eq 0) {
            Write-Log ("  host exe is self-contained (no MinGW runtime DLLs, " + $deps.Count + " system imports)") 'DarkGray'
        } else {
            Write-Log ("  host exe imports " + $deps.Count + " DLLs, " + $mingwDeps.Count + " of them MinGW runtime (shipped alongside)") 'DarkGray'
        }
    }

    Stop-Stage
}

# ----------------------------------------------------------------------------
# stage 2: bare kernel
# ----------------------------------------------------------------------------
$bareOut = Join-Path $buildRoot 'bare'
$isoOut = Join-Path $isoRoot 'nefuOS.iso'

if ($Target -eq 'all' -or $Target -eq 'iso' -or $Target -eq 'esp') {

    Start-Stage 'bare kernel'

    if (-not (Test-Path -LiteralPath $bareOut)) {
        New-Item -ItemType Directory -Force -Path $bareOut | Out-Null
    }

    $bareCoreSrc = @($coreSrc | Where-Object { $bareSkip -notcontains $_ })
    $bareCore = New-GroupItems -Group 'bare_core' -Compiler $toolGxx -Flags $bareCxxFlags `
        -Sources (@($bareCoreSrc + $thirdPartySrc + @('backends\bare\softfloat.cpp', 'backends\bare\bare_libc.cpp')))
    Compile-Batch -Items $bareCore -Label 'bare core'

    # bare.cpp alone keeps the integer register model (ISR attribute)
    $barePlatform = New-GroupItems -Group 'bare_platform' -Compiler $toolGxx -Flags $bareIntFlags `
        -Sources @('backends\bare\bare.cpp')
    Compile-Batch -Items $barePlatform -Label 'bare platform'

    $lvglBare = New-GroupItems -Group 'bare_lvgl' -Compiler $toolGcc -Flags $lvglBareFlags -Sources $lvglSrc
    Compile-Batch -Items $lvglBare -Label 'bare lvgl'

    $webpBare = New-GroupItems -Group 'bare_webp' -Compiler $toolGcc -Flags $webpBareFlags `
        -Sources (@($webpSrc + @('third_party\webp_impl.c')))
    Compile-Batch -Items $webpBare -Label 'bare webp'

    $qjsBare = New-GroupItems -Group 'bare_qjs' -Compiler $toolGcc -Flags $qjsBareFlags `
        -Sources @($qjsSrc | ForEach-Object { 'third_party\quickjs\' + $_ })
    Compile-Batch -Items $qjsBare -Label 'bare quickjs'

    $nsvgBare = New-GroupItems -Group 'bare_nanosvg' -Compiler $toolGcc -Flags $nanosvgBareFlags `
        -Sources @('third_party\nanosvg_impl.c')
    Compile-Batch -Items $nsvgBare -Label 'bare nanosvg'

    # ---- assembler: boot sector, kernel entry, multiboot picker, setjmp shim
    $entryObj = Join-Path $bareOut 'entry.o'
    $bootObj = Join-Path $bareOut 'boot.o'
    $menuObj = Join-Path $bareOut 'menu.o'
    $setjmpObj = Join-Path $bareOut 'setjmp.o'

    # Assembly is cheap but not free, and entry.o / setjmp.o are kernel link
    # inputs: reassembling them unguarded made every object newer than
    # kernel.bin, so the link - and through it payload, boot.bin and the ISO -
    # reran on every "nothing changed" build.
    foreach ($step in @(
            @{ Src = 'backends\bare\boot.s';      Obj = $bootObj;   What = 'boot.s'   },
            @{ Src = 'backends\bare\entry.s';     Obj = $entryObj;  What = 'entry.s'  },
            @{ Src = 'backends\bare\menu.s';      Obj = $menuObj;   What = 'menu.s'   },
            @{ Src = 'backends\bare\setjmp.S';    Obj = $setjmpObj; What = 'setjmp.S' })) {
        if (-not (Test-OutputFresh -Output $step.Obj -Inputs @($step.Src))) {
            Invoke-Tool -Exe $toolAs -Arguments @($step.Src, '-o', $step.Obj) -What $step.What
        }
    }

    $menuBin = Join-Path $bareOut 'menu.bin'
    if (-not (Test-OutputFresh -Output $menuBin -Inputs @($menuObj))) {
        Invoke-Tool -Exe $toolObjcopy -Arguments @('-O', 'binary', '-j', '.text', $menuObj, $menuBin) -What 'menu objcopy'
    }
    $menuLen = (Get-Item -LiteralPath $menuBin).Length
    if ($menuLen -gt 4096) { throw ("menu.bin too large: " + $menuLen + " bytes (max 4096)") }
    Write-Log ("  menu.bin " + $menuLen + " bytes") 'DarkGray'

    # ---- link kernel, flatten, patch multiboot2 header.
    # All three are skipped together when kernel.bin is newer than its inputs:
    # they are one logical step and each one rewrites kernel.bin's bytes.
    $kernelExe = Join-Path $bareOut 'kernel.exe'
    $kernelMap = Join-Path $bareOut 'kernel.map'
    $kernelBin = Join-Path $bareOut 'kernel.bin'
    $objs = @($entryObj, $setjmpObj)
    foreach ($set in @($bareCore, $barePlatform, $lvglBare, $webpBare, $qjsBare, $nsvgBare)) {
        foreach ($it in $set) { $objs += $it.Obj }
    }

    $bareLinkArgs = @('-mi386pep', '--image-base', '0x100000', '--gc-sections',
        '-T', 'backends\bare\linker.ld', '-o', $kernelExe, '-Map', $kernelMap) + $objs

    $bareRsp = Join-Path $buildRoot 'link_bare.rsp'
    $lines = @()
    foreach ($a in $bareLinkArgs) {
        $v = $a -replace '\\', '/'
        if ($v -match '\s') { $v = '"' + $v + '"' }
        $lines += $v
    }
    $bareRspText = ($lines -join "`r`n") + "`r`n"
    $bareRspOld = ''
    if (Test-Path -LiteralPath $bareRsp) { $bareRspOld = [IO.File]::ReadAllText($bareRsp) }
    $bareRspChanged = ($bareRspOld -ne $bareRspText)
    if ($bareRspChanged) {
        [IO.File]::WriteAllLines($bareRsp, $lines, (New-Object Text.ASCIIEncoding))
    }

    if (-not $bareRspChanged -and (Test-OutputFresh -Output $kernelBin -Inputs (@($objs) + @(
                'backends\bare\linker.ld', 'tools\rebin_kernel.py'))) ) {
        Write-Log '  kernel: up to date, skipped link + flatten' 'DarkGray'
    } else {
        Invoke-Tool -Exe $toolLd -Arguments @('@' + $bareRsp) -What 'kernel link'

        # ---- flatten PE -> kernel.bin (self-verifies the multiboot2 header offset)
        $rebinOut = Invoke-Tool -Exe $toolPy -Arguments @('tools\rebin_kernel.py', $kernelExe, $kernelBin) -What 'rebin_kernel'

        $bssStart = 0
        $bssSize = 0
        $text = ($rebinOut -join "`n")
        if ($text -match 'REBIN_OK\s+\d+\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)') {
            $bssStart = [Convert]::ToUInt32($matches[1], 16)
            $bssSize = [Convert]::ToUInt32($matches[2], 16)
        } else {
            throw ("rebin_kernel output unparsable: " + $text)
        }

        # ---- multiboot2 header: load_end_addr / bss_end_addr / boot params
        [void](Invoke-Tool -Exe $toolPy -Arguments @('tools\patch_mb2.py', $kernelBin, $bssStart, $bssSize) -What 'patch_mb2')
    }

    $kernelSize = (Get-Item -LiteralPath $kernelBin).Length
    Write-Log ("  kernel.bin " + $kernelSize + " bytes") 'DarkGray'

    # the flat image loads at 0x100000 and must stay below the stub staging base
    # (STAGE_BASE in backends\bare\stub.s; keep the two in sync)
    $stageBase = 0x1400000
    $kernelEnd = 0x100000 + $kernelSize
    if ($kernelEnd -ge $stageBase) {
        throw ("kernel image ends at 0x" + ('{0:X}' -f $kernelEnd) + " >= 0x" + ('{0:X}' -f $stageBase) + " staging base")
    }
    $isoKernelMax = 4194304 * 2048
    if ($kernelSize -gt $isoKernelMax) {
        throw ("kernel too large for the ISO layout: " + $kernelSize + " > " + $isoKernelMax)
    }

    # ---- compressed payload: 16-bit stub + raw-DEFLATE kernel.
    # boot.s cannot load a multi-MB image at 0x20000 (VGA/ROM hole + real-mode
    # segment wrap), so a ~500KB payload lands there and the stub inflates the
    # kernel to 0x100000. Re-running this whole block is what costs the ~30 s of
    # deflate, so it only runs when the kernel or the stub itself changed.
    $payload = Join-Path $bareOut 'payload.bin'
    $stubBin = Join-Path $bareOut 'stub.bin'
    $payloadFresh = Test-OutputFresh -Output $payload -Inputs @(
        $kernelBin, 'backends\bare\stub.s', 'backends\bare\stub.ld', 'core\net\inflate.cpp')

    if ($payloadFresh) {
        Write-Log '  payload: up to date, skipped stub build + deflate' 'DarkGray'
    } else {
        $stubObj = Join-Path $bareOut 'stub.o'
        $inflateObj = Join-Path $bareOut 'inflate32.o'
        $stubExe = Join-Path $bareOut 'stub.exe'

        Invoke-Tool -Exe $toolAs -Arguments @('--32', 'backends\bare\stub.s', '-o', $stubObj) -What 'stub.s'
        Invoke-Tool -Exe $toolGxx -Arguments @('-m32', '-ffreestanding', '-fno-builtin', '-Os',
            '-c', 'core\net\inflate.cpp', '-o', $inflateObj) -What 'inflate (m32)'
        Invoke-Tool -Exe $toolLd -Arguments @('-mi386pe', '--image-base', '0',
            '-T', 'backends\bare\stub.ld', $stubObj, $inflateObj, '-o', $stubExe) -What 'stub link'
        Invoke-Tool -Exe $toolObjcopy -Arguments @('-O', 'binary', '-j', '.text', $stubExe, $stubBin) -What 'stub objcopy'

        $stubLen = (Get-Item -LiteralPath $stubBin).Length
        if ($stubLen -gt 0x2000) { throw ("stub too large: " + $stubLen + " bytes") }
        Write-Log ("  stub.bin " + $stubLen + " bytes") 'DarkGray'

        $kernelGz = Join-Path $bareOut 'kernel.gz'
        $gzCode = "import zlib,sys; d=open(sys.argv[1],'rb').read(); g=zlib.compressobj(9,zlib.DEFLATED,-15); o=g.compress(d)+g.flush(); open(sys.argv[2],'wb').write(o); print(len(d),len(o))"
        $gzOut = Invoke-Tool -Exe $toolPy -Arguments @('-c', $gzCode, $kernelBin, $kernelGz) -What 'kernel.gz'
        $gzLen = (Get-Item -LiteralPath $kernelGz).Length
        $gzCap = 0x200000
        if ($kernelSize -gt $gzCap) { $gzCap = $kernelSize }
        Write-Log ("  kernel.gz " + $gzLen + " bytes (cap " + $gzCap + ")") 'DarkGray'

        # stub slots: +0x80 source offset, +0x84 source length, +0x88 dst capacity
        $slotBytes = [byte[]]::new(12)
        [BitConverter]::GetBytes([uint32]$stubLen).CopyTo($slotBytes, 0)
        [BitConverter]::GetBytes([uint32]$gzLen).CopyTo($slotBytes, 4)
        [BitConverter]::GetBytes([uint32]$gzCap).CopyTo($slotBytes, 8)

        $fs = [IO.File]::OpenWrite($stubBin)
        try { $fs.Position = 0x80; $fs.Write($slotBytes, 0, 12) } finally { $fs.Close() }

        $payloadBytes = [IO.File]::ReadAllBytes($stubBin) + [IO.File]::ReadAllBytes($kernelGz)
        [IO.File]::WriteAllBytes($payload, $payloadBytes)
        Write-Log ("  payload.bin " + $payloadBytes.Length + " bytes") 'DarkGray'
        Reset-MtimeCache
    }

    # ---- boot sector + kernel sector count patch
    $bootBin = Join-Path $bareOut 'boot.bin'
    if (-not (Test-OutputFresh -Output $bootBin -Inputs @('backends\bare\boot.s', $payload))) {
        Invoke-Tool -Exe $toolObjcopy -Arguments @('-O', 'binary', '-j', '.text', $bootObj, $bootBin) -What 'boot objcopy'
        $bootLen = (Get-Item -LiteralPath $bootBin).Length
        if ($bootLen -ne 512) { throw ("boot.bin size " + $bootLen + " != 512") }

        $payloadSize = (Get-Item -LiteralPath $payload).Length
        $kSectors = [Math]::Ceiling($payloadSize / 512)
        $fs = [IO.File]::OpenWrite($bootBin)
        try {
            $fs.Position = 0x58        # kernel_count, file offset (runtime 0x7C58)
            $fs.WriteByte([byte]($kSectors -band 0xFF))
            $fs.WriteByte([byte](($kSectors -shr 8) -band 0xFF))
        } finally { $fs.Close() }
        Write-Log ("  boot.bin patched: " + $kSectors + " kernel sectors") 'DarkGray'
        Reset-MtimeCache
    } else {
        Write-Log '  boot.bin: up to date, skipped' 'DarkGray'
    }

    # ---- El Torito ISO
    if (Test-OutputFresh -Output $isoOut -Inputs @($bootBin, $menuBin, $payload, 'tools\make_iso.py')) {
        Write-Log '  ISO: up to date, skipped' 'DarkGray'
    } else {
        Invoke-Tool -Exe $toolPy -Arguments @('tools\make_iso.py', $bootBin, $menuBin, $payload, $isoOut) -What 'make_iso'
        Write-Log ("  ISO " + (Get-Item -LiteralPath $isoOut).Length + " bytes") 'Green'

        # Structural self-check: the image must carry exactly the payload the
        # linker produced, at LBA26, and boot.s' 512-byte sector counter must
        # cover it. Catches the "iso built from yesterday's payload" class of
        # mistake without needing QEMU. Zero-diff QEMU runs are not started here
        # on purpose -- booting the kernel belongs to the test pass, not the build.
        if (Test-Path -LiteralPath 'tools\check_iso.py') {
            $check = Invoke-Tool -Exe $toolPy -Arguments @('tools\check_iso.py') -What 'check_iso'
            foreach ($line in ($check -split "`n")) {
                if ($line.Trim()) { Write-Log ("  " + $line.TrimEnd()) 'DarkGray' }
            }
        }
    }

    Stop-Stage

    # ---- optional GRUB / UEFI ESP image
    if ($Target -eq 'esp' -or $GrubEsp) {
        Start-Stage 'ESP image'

        $grubRoot = Join-Path $root 'tools\grub_toolchain'
        $grubCore = Join-Path $grubRoot 'extracted\usr\lib\grub\x86_64-efi\monolithic\grubx64.efi'
        $grubMods = Join-Path $grubRoot 'extracted\usr\lib\grub\x86_64-efi'
        $grubCfg = Join-Path $root 'tools\grub.cfg'

        if (-not (Test-Path -LiteralPath $grubCore)) {
            Write-Log '  GRUB toolchain missing, fetching…' 'Yellow'
            [void](Invoke-Tool -Exe $toolPy -Arguments @('tools\fetch_grub_toolchain.py') -What 'fetch_grub_toolchain')
        }
        if (-not (Test-Path -LiteralPath $grubCore)) { throw 'grubx64.efi still missing after fetch' }

        $espImg = Join-Path $isoRoot 'nefuOS_esp.img'
        [void](Invoke-Tool -Exe $toolPy -Arguments @('tools\make_esp.py', $grubCore, $grubCfg, $kernelBin, $grubMods, $espImg) -What 'make_esp')
        Write-Log ("  ESP image " + (Get-Item -LiteralPath $espImg).Length + " bytes") 'Green'

        Stop-Stage
    }
}

# ----------------------------------------------------------------------------
# stage 3: publish to dist\ (the old scripts never did this)
# ----------------------------------------------------------------------------
if (-not $NoCopy) {
    Start-Stage 'publish'

    if (-not (Test-Path -LiteralPath $distRoot)) {
        New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
    }

    $published = @()

    if (Test-Path -LiteralPath $hostExe) {
        $dst = Join-Path $distRoot 'nefuOS.exe'
        Copy-Item -LiteralPath $hostExe -Destination $dst -Force
        $published += ('nefuOS.exe ' + (Get-Item -LiteralPath $dst).Length + ' B')
    }

    # runtime DLLs the exe still imports: without them dist\nefuOS.exe runs on
    # this machine only
    foreach ($dll in @($script:runtimeDlls)) {
        if (-not $dll) { continue }
        $src = Join-Path (Split-Path -Parent $hostExe) $dll
        if (Test-Path -LiteralPath $src) {
            $dst = Join-Path $distRoot $dll
            Copy-Item -LiteralPath $src -Destination $dst -Force
            $published += ($dll + ' ' + (Get-Item -LiteralPath $dst).Length + ' B')
        }
    }

    if (Test-Path -LiteralPath $isoOut) {
        $dst = Join-Path $distRoot 'nefuOS.iso'
        Copy-Item -LiteralPath $isoOut -Destination $dst -Force
        $published += ('nefuOS.iso ' + (Get-Item -LiteralPath $dst).Length + ' B')
    }

    $espImg = Join-Path $isoRoot 'nefuOS_esp.img'
    if (Test-Path -LiteralPath $espImg) {
        $dst = Join-Path $distRoot 'nefuOS_esp.img'
        Copy-Item -LiteralPath $espImg -Destination $dst -Force
        $published += ('nefuOS_esp.img ' + (Get-Item -LiteralPath $dst).Length + ' B')
    }

    $gitHash = ''
    try {
        $gitHash = (& git -C $root rev-parse --short HEAD 2>$null)
        if ($LASTEXITCODE -ne 0) { $gitHash = '' }
    } catch { $gitHash = '' }

    $stampText = @(
        'nefuOS build',
        ('time     : ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')),
        ('git      : ' + $gitHash),
        ('target   : ' + $Target),
        ('compiler : ' + $toolGxx),
        ('artifacts: ' + ($published -join ' | '))
    ) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $distRoot 'nefuOS_build.txt'), $stampText + "`r`n", (New-Object Text.UTF8Encoding($false)))

    foreach ($p in $published) { Write-Log ("  dist\ " + $p) 'Green' }
    Stop-Stage
}

# ----------------------------------------------------------------------------
# audit + summary
# ----------------------------------------------------------------------------
Invoke-Audit -Built @($coreSrc + $thirdPartySrc + $hostOnlySrc + $bareOnlySrc + $bareAsmSrc)

Write-Log ''
Write-Log 'timings:' 'White'
foreach ($t in $script:stageTimes) {
    Write-Log ("  {0,-16} {1,7} s" -f $t.Stage, $t.Seconds) 'DarkGray'
}
$totalSeconds = ($script:stageTimes | Measure-Object -Property Seconds -Sum).Sum

if ($script:warnings.Count -gt 0) {
    Write-Log ("build finished with " + $script:warnings.Count + " warning(s), " + $totalSeconds + " s total") 'Yellow'
} else {
    Write-Log ("BUILD DONE  (" + $totalSeconds + " s)") 'Green'
}
Write-Log ("log: " + $logFile) 'DarkGray'
