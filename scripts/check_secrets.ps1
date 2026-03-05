$ErrorActionPreference = "Stop"

$patterns = @(
    'sk-[A-Za-z0-9]{16,}',
    'LMCHAN_SECRET_WIFI_PASS\s+"[^"]+"',
    'LMCHAN_SECRET_FS_APP_SECRET\s+"[^"]+"',
    'LMCHAN_SECRET_API_KEY\s+"[^"]+"',
    'LMCHAN_SECRET_TG_TOKEN\s+"[^"]+"'
)

$files = git ls-files
$hits = @()
$excludePathRegex = '^(README_CN\.md|docs/|skills/|scripts/check_secrets\.ps1$)'

foreach ($file in $files) {
    $norm = $file -replace '\\','/'
    if ($norm -match $excludePathRegex) {
        continue
    }
    foreach ($pattern in $patterns) {
        $result = Select-String -Path $file -Pattern $pattern -AllMatches
        if ($result) {
            $hits += $result
        }
    }
}

if ($hits.Count -gt 0) {
    Write-Host "Potential secrets found:" -ForegroundColor Red
    $hits | ForEach-Object {
        Write-Host ("{0}:{1}:{2}" -f $_.Path, $_.LineNumber, $_.Line.Trim())
    }
    exit 1
}

Write-Host "Secret scan passed." -ForegroundColor Green
exit 0
