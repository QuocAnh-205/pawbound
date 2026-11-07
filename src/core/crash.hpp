#pragma once
// Ghi báo cáo crash CỤC BỘ (logs/crash-<ứng dụng>-<unix>.txt): phiên bản, loại lỗi, mã/địa chỉ.
// Không thu thập dữ liệu cá nhân, không tự gửi đi đâu — người chơi chủ động đính kèm khi báo lỗi.
#include <string>

namespace pb {
void install_crash_handler(const std::string& app_name, const std::string& log_dir = "logs");
}  // namespace pb
