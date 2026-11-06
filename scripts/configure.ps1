param([string]$Preset = 'win-debug')
$ErrorActionPreference = 'Continue'  # native tools ghi stderr; kiểm tra bằng $LASTEXITCODE
. "$PSScriptRoot\env.ps1"
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "configure thất bại ($LASTEXITCODE)" }
} finally { Pop-Location }
