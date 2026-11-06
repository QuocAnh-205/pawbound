# Chuẩn hóa asset bằng Blender chạy headless (không mở UI).
# Dùng: .\scripts\blender_batch.ps1 -Source assets\prototype\fox\Fox.glb -Dest build\assets\fox_norm.glb [-Scale 0.0075] [-Prefix chr_]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Dest,
    [double]$Scale = 1.0,
    [string]$Prefix = ''
)
$ErrorActionPreference = 'Continue'
$blender = 'D:\DevTools\blender\blender.exe'
if (-not (Test-Path $blender)) { Write-Error "Không tìm thấy Blender tại $blender"; exit 2 }
$script = Join-Path $PSScriptRoot '..\tools\blender\normalize_export.py'
$bargs = @('--background', '--factory-startup', '--python-exit-code', '1', '--python', $script, '--', $Source, $Dest, '--scale', "$Scale")
if ($Prefix) { $bargs += @('--prefix', $Prefix) }
& $blender @bargs
exit $LASTEXITCODE
