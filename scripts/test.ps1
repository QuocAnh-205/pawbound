param([string]$Preset = 'win-debug', [string]$Filter = '')
$ErrorActionPreference = 'Continue'  # native tools ghi stderr; kiểm tra bằng $LASTEXITCODE
. "$PSScriptRoot\env.ps1"
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if ($Filter) { ctest --preset $Preset -R $Filter } else { ctest --preset $Preset }
    if ($LASTEXITCODE -ne 0) { throw "test thất bại ($LASTEXITCODE)" }
} finally { Pop-Location }
