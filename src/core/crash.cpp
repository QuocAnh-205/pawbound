#include "core/crash.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>

#include "core/version.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pb {
namespace {
// Chuẩn bị sẵn đường dẫn và tiền tố lúc cài (không cấp phát trong lúc crash).
char g_path[512];
char g_app[64];

void write_report(const char* kind, unsigned long code, const void* addr) {
  if (std::FILE* f = std::fopen(g_path, "w")) {
    std::fprintf(f, "PAWBOUND crash report\napp=%s\nversion=%s\ntime_unix=%lld\nkind=%s\ncode=0x%08lx\naddress=%p\n", g_app,
                 kVersionString, static_cast<long long>(std::time(nullptr)), kind, code, addr);
    std::fprintf(f, "Gửi file này kèm logs/*.log khi báo lỗi. File KHÔNG chứa mật khẩu/token/dữ liệu cá nhân.\n");
    std::fclose(f);
  }
}

void crash_signal(int sig) {  // (tên riêng: tránh trùng on_signal của main server)
  write_report(sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGFPE ? "SIGFPE" : "SIGILL", static_cast<unsigned long>(sig), nullptr);
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

#if defined(_WIN32)
LONG WINAPI on_seh(EXCEPTION_POINTERS* e) {
  write_report("SEH", e && e->ExceptionRecord ? e->ExceptionRecord->ExceptionCode : 0, e && e->ExceptionRecord ? e->ExceptionRecord->ExceptionAddress : nullptr);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif
}  // namespace

void install_crash_handler(const std::string& app_name, const std::string& log_dir) {
  std::error_code ec;
  std::filesystem::create_directories(log_dir, ec);
  std::snprintf(g_app, sizeof(g_app), "%s", app_name.c_str());
  std::snprintf(g_path, sizeof(g_path), "%s/crash-%s-%lld.txt", log_dir.c_str(), app_name.c_str(), static_cast<long long>(std::time(nullptr)));
  for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(s, crash_signal);
  std::set_terminate([] {
    write_report("std::terminate (exception không được bắt)", 0, nullptr);
    std::abort();
  });
#if defined(_WIN32)
  SetUnhandledExceptionFilter(on_seh);
#endif
}

}  // namespace pb
