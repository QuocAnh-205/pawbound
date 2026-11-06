# Môi trường phát triển (Windows, công cụ trên ổ D)

Xác minh ngày 2026-11-06 trên máy phát triển chính.

| Công cụ | Phiên bản | Đường dẫn | Nguồn |
|---|---|---|---|
| MinGW-w64 GCC | 15.2.0 (posix, seh) | `C:\mingw64\bin` (có sẵn, không cài thêm) | MinGW-Builds |
| CMake | 4.4.5 | `D:\DevTools\cmake\bin` | github.com/Kitware/CMake (zip portable) |
| Ninja | 1.13.2 | `D:\DevTools\ninja` | github.com/ninja-build/ninja |
| Vulkan SDK | 1.4.363.1 (shaderc v2026.4) | `D:\DevTools\VulkanSDK\1.4.363.1` | LunarG (installer có chữ ký hợp lệ; cần quyền admin để đăng ký layer) |
| Git | 2.52.0 | có sẵn | |
| Node.js | v24.11.1 | có sẵn | dùng ở Phase 11 |
| Docker | 29.5.3 | có sẵn | ⚠ chuyển "Disk image location" sang ổ D trước khi dùng |

Bộ cài gốc được giữ trong `D:\DevTools\_downloads` (có thể xóa để lấy lại khoảng 340 MB).

## GPU
- Intel UHD Graphics: Vulkan 1.3.215.
- NVIDIA GeForce GTX 1650 Max-Q: Vulkan 1.3.194, driver 30.0.15.1165 (khoảng đầu năm 2022). **Nên cập nhật driver NVIDIA** trước Phase 3.

## Thiết lập PATH cho một phiên PowerShell
Từ Phase 1 sẽ có `scripts/env.ps1`. Trong lúc chưa có:
```powershell
$env:PATH = "D:\DevTools\cmake\bin;D:\DevTools\ninja;C:\mingw64\bin;$env:VULKAN_SDK\Bin;$env:PATH"
```
`VULKAN_SDK` đã được bộ cài đặt ở mức Machine (`D:\DevTools\VulkanSDK\1.4.363.1`). Mở terminal mới để nhận biến này.

## Spike đã chạy (Phase 0)
Chương trình C++20 tối thiểu gọi `vkEnumerateInstanceVersion`, `vkCreateInstance` (API 1.3) và liệt kê GPU. Build bằng CMake 4.4.5 + Ninja + MinGW GCC 15.2, link `Vulkan::Vulkan` (`vulkan-1.lib` của SDK). Kết quả:
```
Loader Vulkan 1.4.363
GPU: Intel(R) UHD Graphics (API 1.3)
GPU: NVIDIA GeForce GTX 1650 with Max-Q Design (API 1.3)
```
Spike chỉ chứng minh toolchain link được Vulkan, **không** phải renderer.
