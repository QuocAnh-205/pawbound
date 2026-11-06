# Xóa thư mục build của một preset (hoặc tất cả với -All). Không đụng .deps-cache.
param([string]$Preset = 'win-debug', [switch]$All)
$ErrorActionPreference = 'Stop'
$build = Join-Path (Split-Path $PSScriptRoot -Parent) 'build'
if ($All) { $target = $build } else { $target = Join-Path $build $Preset }
if (Test-Path $target) { Remove-Item -Recurse -Force $target; "Đã xóa $target" } else { "Không có $target" }
