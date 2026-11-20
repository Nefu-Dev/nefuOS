# Warm the HTTP disk cache for bloxd.io on a very slow link: fetch the main
# bundle with 6 parallel range requests, then concatenate the parts.
$ErrorActionPreference = "Continue"
$warm = "D:\mycppos1\nefuOS\verify_out\warm"
New-Item -ItemType Directory -Force -Path $warm | Out-Null
$url = "https://bloxd.io/static/js/58e8zk.main.29707077.js"
$total = 3257334
$n = 6
$per = [math]::Ceiling($total / $n)

$jobs = @()
for ($i = 0; $i -lt $n; $i++) {
    $s = $i * $per
    $e = [math]::Min($total - 1, ($i + 1) * $per - 1)
    $jobs += Start-Job -ScriptBlock {
        param($i, $s, $e, $url, $warm)
        Add-Type -AssemblyName System.Net.Http
        $handler = New-Object System.Net.Http.HttpClientHandler
        $handler.UseProxy = $true
        $handler.AutomaticDecompression = [System.Net.DecompressionMethods]::None
        $h = New-Object System.Net.Http.HttpClient($handler)
        $h.Timeout = [TimeSpan]::FromMinutes(20)
        $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, $url)
        $req.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue([int64]$s, [int64]$e)
        $resp = $h.SendAsync($req).Result
        $b = $resp.Content.ReadAsByteArrayAsync().Result
        [IO.File]::WriteAllBytes((Join-Path $warm ("pp_" + $i)), $b)
        "part $i code=$([int]$resp.StatusCode) len=$($b.Length)"
    } -ArgumentList $i, $s, $e, $url, $warm
}

$jobs | ForEach-Object { Wait-Job $_ -Timeout 1200 | Out-Null; Receive-Job $_ }
$jobs | Remove-Job -Force -ErrorAction SilentlyContinue

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
