# Poll until the missing range of the bloxd main bundle can be fetched, then
# assemble the full file. Tries the system proxy and a direct connection,
# alternating, once a minute, for up to 60 attempts.
$ErrorActionPreference = "Continue"
$warm = "D:\mycppos1\nefuOS\verify_out\warm"
$url = "https://bloxd.io/static/js/58e8zk.main.29707077.js"
$s = 2171556
$e = 2714444
$want = 542889
$p4 = Join-Path $warm "pp_4"

for ($try = 1; $try -le 60; $try++) {
    $have = 0
    if (Test-Path $p4) { $have = (Get-Item $p4).Length }
    if ($have -ge $want) { break }
    $range = "bytes=$($s + $have)-$e"
    foreach ($mode in @("proxy", "direct")) {
        $args = @("-sS", "--max-time", "120", "-r", $range, "-A", "Mozilla/5.0 Chrome/120", "-o", "$p4.part", $url)
        if ($mode -eq "direct") { $args = @("-sS", "--noproxy", "*", "--max-time", "120", "-r", $range, "-A", "Mozilla/5.0 Chrome/120", "-o", "$p4.part", $url) }
        $null = & curl.exe @args 2>$null
        if ((Test-Path "$p4.part") -and ((Get-Item "$p4.part").Length -gt 0)) {
            $bytes = [IO.File]::ReadAllBytes("$p4.part")
            if ($bytes.Length -gt 0 -and $bytes[0] -ne 0x3C) {   # not an HTML error page
                $fs = [IO.File]::Open($p4, [IO.FileMode]::Append)
                $fs.Write($bytes, 0, $bytes.Length)
                $fs.Close()
                "try $try ${mode}: appended $($bytes.Length)"
            } else {
                "try $try ${mode}: error page ($($bytes.Length) bytes)"
            }
            Remove-Item "$p4.part" -ErrorAction SilentlyContinue
        }
        $have = 0
        if (Test-Path $p4) { $have = (Get-Item $p4).Length }
        if ($have -ge $want) { break }
        Start-Sleep -Seconds 5
    }
    if ($have -ge $want) { break }
    Start-Sleep -Seconds 60
}

$have = 0
if (Test-Path $p4) { $have = (Get-Item $p4).Length }
"part4 final: $have of $want"
if ($have -ge $want) {
    $out = Join-Path $warm "bloxd_main.js"
    $fs = [IO.File]::Create($out)
    foreach ($i in 0..5) {
        $b = [IO.File]::ReadAllBytes((Join-Path $warm ("pp_" + $i)))
        $fs.Write($b, 0, $b.Length)
    }
    $fs.Close()
    "assembled $((Get-Item $out).Length) bytes"
}
