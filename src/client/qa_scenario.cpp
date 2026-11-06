#include "client/qa_scenario.hpp"

#include <cmath>
#include <format>

#include "core/log.hpp"

namespace pb::client {

void SurvivalScenario::check(bool ok, const std::string& what) {
  ++checks_;
  if (!ok) ++failed_;
  log_.push_back(std::format("[{}] {}", ok ? "PASS" : "FAIL", what));
  log::info("qa", "{}", log_.back());
}

bool SurvivalScenario::step(GameSession& s, InputState& in, std::vector<std::string>& pressed) {
  const uint64_t t = ++tick_;
  in = {};  // kịch bản điều khiển toàn bộ input
  pressed.clear();
  switch (t) {
    case 5:
      s.qa_set_needs(30, 30, 40);
      break;
    case 10:
      check(s.qa_teleport_near(world::PoiKind::Water), "tìm được nguồn nước gần nhất");
      thirst_before_ = s.survival().thirst;
      break;
    case 12: in.interact = true; break;  // E
    case 14:
      check(std::abs(s.survival().thirst - (thirst_before_ + 30.0f)) < 1.0f, std::format("uống nước đúng một lần (+30): khát {:.1f} -> {:.1f} ({})", thirst_before_,
                                                                   s.survival().thirst, s.last_message()));
      break;
    case 20:
      check(s.qa_teleport_near(world::PoiKind::Food), "tìm được nguồn thức ăn gần nhất");
      hunger_before_ = s.survival().hunger;
      food_before_ = s.inventory().count_of("scrap_food");
      break;
    case 22: in.interact = true; break;
    case 24:
      check(std::abs(s.survival().hunger - (hunger_before_ + 35.0f)) < 1.0f, std::format("ăn đúng một lần (+35): đói {:.1f} -> {:.1f} ({})", hunger_before_,
                                                                   s.survival().hunger, s.last_message()));
      pressed.push_back("G");
      break;
    case 26:
      check(s.inventory().count_of("scrap_food") == food_before_ + 1,
            std::format("lấy đồ ăn vào túi: {} -> {} ({})", food_before_, s.inventory().count_of("scrap_food"), s.last_message()));
      pressed.push_back("1");  // dùng ô 1 (chai nước)
      break;
    case 28:
      check(s.inventory().count_of("water_bottle") == 0, "dùng chai nước trong túi (ô 1)");
      break;
    case 30:
      check(s.qa_face_npc(0, 1.0f), "đứng đối diện chó NPC");
      npc_hp_before_ = s.npc_hp(0);
      bite_start_ = t;
      break;
    default: break;
  }
  if (t > 31 && t < 200 && bite_start_ && (t - bite_start_) % 27 == 0) in.attack = true;  // ~0,9 s/lần > hồi chiêu 0,8 s
  if (t == 200) {
    check(s.npc_hp(0) < npc_hp_before_ - 30,
          std::format("cắn NPC nhiều lần: HP {:.0f} -> {:.0f} ({})", npc_hp_before_, s.npc_hp(0), s.last_message()));
    check(s.survival().stamina < 100.0f, std::format("cắn tốn thể lực (còn {:.0f})", s.survival().stamina));
  }
  if (t == 210) {
    rest_before_ = s.survival().rest;
    pressed.push_back("Z");
  }
  if (t == 300) {
    check(s.survival().rest > rest_before_ + 2.0f, std::format("nghỉ ngơi: rest {:.1f} -> {:.1f}", rest_before_, s.survival().rest));
    return true;
  }
  return false;
}

}  // namespace pb::client
