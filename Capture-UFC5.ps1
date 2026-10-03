param([ValidateRange(5, 120)][int]$Seconds = 30)
$ErrorActionPreference = 'Stop'
$capture = Join-Path $PSScriptRoot '_Build\tracy-capture\tracy-capture.exe'
if (-not (Test-Path -LiteralPath $capture)) { throw 'Build tracy-capture first.' }
$folder = Join-Path $PSScriptRoot '_Build\captures'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$output = Join-Path $folder ('UFC5-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.tracy')
Write-Host 'Record while actively fighting in the local build with Tracy profiler enabled.'
& $capture -a 127.0.0.1 -o $output -s $Seconds
if ($LASTEXITCODE -ne 0) { throw "Capture failed with exit code $LASTEXITCODE" }
Write-Host "Capture saved: $output"
