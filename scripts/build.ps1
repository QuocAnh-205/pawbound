param([string]$Preset = 'win-debug', [string]$Target = '')
$ErrorActionPreference = 'Continue'  # native tools ghi stderr; kiểm tra bằng $LASTEXITCODE
. "$PSScriptRoot\env.ps1"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if (-not (Test-Path "build\$Preset\CMakeCache.txt")) {
        cmake --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "configure thất bại ($LASTEXITCODE)" }
    }
    if ($Target) { cmake --build --preset $Preset --target $Target } else { cmake --build --preset $Preset }
    if ($LASTEXITCODE -ne 0) { throw "build thất bại ($LASTEXITCODE)" }
} finally { Pop-Location }
