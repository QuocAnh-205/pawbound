#pragma once
// Kịch bản QA tự động chạy trong client thật (renderer + world + luật gameplay), dùng cho CTest.
// Mỗi bước tạo input như người chơi bấm phím, rồi kiểm tra trạng thái sau đó.
#include <string>
#include <vector>

#include "client/game_session.hpp"

namespace pb::client {

class SurvivalScenario {
 public:
  // Gọi mỗi tick TRƯỚC khi session xử lý input. Trả true khi kịch bản kết thúc.
  // Dùng bộ đếm tick riêng (mỗi lần gọi = đúng một tick sim).
  bool step(GameSession& s, InputState& in, std::vector<std::string>& pressed);
  [[nodiscard]] bool passed() const { return failed_ == 0 && checks_ > 0; }
  [[nodiscard]] const std::vector<std::string>& log() const { return log_; }

 private:
  void check(bool ok, const std::string& what);
  int checks_ = 0, failed_ = 0;
  std::vector<std::string> log_;
  float thirst_before_ = 0, hunger_before_ = 0, rest_before_ = 0, npc_hp_before_ = 0;
  int food_before_ = 0;
  uint64_t bite_start_ = 0;
  uint64_t tick_ = 0;
};

}  // namespace pb::client
