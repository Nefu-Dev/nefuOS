# Probe: fetch bloxd.io homepage via System.Net.Http and report whether it
# contains real bundle references (static/js/*.js) or an obfuscated challenge.
param([string]$Url = "https://bloxd.io/")

Add-Type -AssemblyName System.Net.Http
$handler = New-Object System.Net.Http.HttpClientHandler
$handler.AutomaticDecompression = [System.Net.DecompressionMethods]::GZip -bor [System.Net.DecompressionMethods]::Deflate
$client = New-Object System.Net.Http.HttpClient($handler)
$client.Timeout = [TimeSpan]::FromSeconds(60)
$client.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36")
$client.DefaultRequestHeaders.Accept.ParseAdd("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")
try {
    $resp = $client.GetAsync($Url).GetAwaiter().GetResult()
    $body = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    "status=$([int]$resp.StatusCode) len=$($body.Length)"
    $ms = [regex]::Matches($body, 'static/js/[^"'']{1,120}\.js') | ForEach-Object { $_.Value } | Sort-Object -Unique
    if ($ms) { "BUNDLES:"; $ms | ForEach-Object { "  $_" } } else { "NO static/js refs" }
    $head = $body.Substring(0, [Math]::Min(150, $body.Length)) -replace "`r|`n", " "
    "HEAD: $head"
    # also probe the old bundle for reference
    try {
        $r2 = $client.GetAsync("https://bloxd.io/static/js/58e8zk.main.29707077.js").GetAwaiter().GetResult()
        "oldbundle=$([int]$r2.StatusCode)"
    } catch { "oldbundle=ERR $($_.Exception.Message)" }
} catch {
    $ex = $_.Exception
    while ($ex.InnerException) { $ex = $ex.InnerException }
    "ERR: $($ex.GetType().Name): $($ex.Message)"
}

# second attempt: explicit system proxy
Add-Type -AssemblyName System.Net
$wr = [System.Net.WebRequest]::GetSystemWebProxy()
$pr = $wr.GetProxy([Uri]$Url)
"sysproxy -> $($pr.AbsoluteUri)"
$handler2 = New-Object System.Net.Http.HttpClientHandler
$handler2.Proxy = $wr
$handler2.UseProxy = $true
$client2 = New-Object System.Net.Http.HttpClient($handler2)
$client2.Timeout = [TimeSpan]::FromSeconds(60)
$client2.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36")
try {
    $resp2 = $client2.GetAsync($Url).GetAwaiter().GetResult()
    $body2 = $resp2.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    "PROXY status=$([int]$resp2.StatusCode) len=$($body2.Length)"
    $ms2 = [regex]::Matches($body2, 'static/js/[^"'']{1,120}\.js') | ForEach-Object { $_.Value } | Sort-Object -Unique
    if ($ms2) { "BUNDLES:"; $ms2 | ForEach-Object { "  $_" } } else { "NO static/js refs" }
} catch {
    $ex2 = $_.Exception
    while ($ex2.InnerException) { $ex2 = $ex2.InnerException }
    "PROXY ERR: $($ex2.GetType().Name): $($ex2.Message)"
}
