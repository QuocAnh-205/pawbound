# Thêm công cụ trên ổ D vào PATH của phiên PowerShell hiện tại (không sửa PATH hệ thống).
# Dùng: . .\scripts\env.ps1
$ErrorActionPreference = 'Stop'
$tools = 'D:\DevTools'
if (-not $env:VULKAN_SDK) {
    $sdk = Get-ChildItem "$tools\VulkanSDK" -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if ($sdk) { $env:VULKAN_SDK = $sdk.FullName }
}
$paths = @("$tools\cmake\bin", "$tools\ninja", 'C:\mingw64\bin')
if ($env:VULKAN_SDK) { $paths += "$env:VULKAN_SDK\Bin" }
foreach ($p in $paths) {
    if (-not (Test-Path $p)) { Write-Warning "Không tìm thấy $p" ; continue }
    if (($env:PATH -split ';') -notcontains $p) { $env:PATH = "$p;$env:PATH" }
}
