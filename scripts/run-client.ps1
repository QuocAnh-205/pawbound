param([string]$Preset = 'win-debug', [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$ErrorActionPreference = 'Continue'  # native tools ghi stderr; kiểm tra bằng $LASTEXITCODE
. "$PSScriptRoot\env.ps1"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try { & "build\$Preset\bin\pawbound_client.exe" @Rest; exit $LASTEXITCODE } finally { Pop-Location }
