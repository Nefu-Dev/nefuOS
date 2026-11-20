# Re-fetch the missing range parts of the bloxd main bundle, then assemble.
$ErrorActionPreference = "Continue"
$warm = "D:\mycppos1\nefuOS\verify_out\warm"
Add-Type -AssemblyName System.Net.Http
$url = "https://bloxd.io/static/js/58e8zk.main.29707077.js"
$total = 3257334
$n = 6
$per = [math]::Ceiling($total / $n)

foreach ($i in 0..($n - 1)) {
    $p = Join-Path $warm ("pp_" + $i)
    $need = $per
    if ($i -eq $n - 1) { $need = $total - $per * ($n - 1) }
    if ((Test-Path $p) -and ((Get-Item $p).Length -eq $need)) { "part $i already ok"; continue }
    $s = $i * $per
    $e = [math]::Min($total - 1, ($i + 1) * $per - 1)
    $handler = New-Object System.Net.Http.HttpClientHandler
    $handler.AutomaticDecompression = [System.Net.DecompressionMethods]::None
    $h = New-Object System.Net.Http.HttpClient($handler)
    $h.Timeout = [TimeSpan]::FromMinutes(8)
    $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, $url)
    $req.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue([int64]$s, [int64]$e)
    try {
        $resp = $h.SendAsync($req).Result
        $b = $resp.Content.ReadAsByteArrayAsync().Result
        [IO.File]::WriteAllBytes($p, $b)
        "part $i code=$([int]$resp.StatusCode) len=$($b.Length)"
    } catch { "part $i FAILED $($_.Exception.Message)" }
    $h.Dispose()
}

$out = Join-Path $warm "bloxd_main.js"
$fs = [IO.File]::Create($out)
$got = 0
foreach ($i in 0..($n - 1)) {
    $p = Join-Path $warm ("pp_" + $i)
    if (Test-Path $p) {
        $b = [IO.File]::ReadAllBytes($p)
        $fs.Write($b, 0, $b.Length)
        $got += $b.Length
    }
}
$fs.Close()
"assembled $got of $total bytes -> $out"
