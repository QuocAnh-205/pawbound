#pragma once
// Skeleton, animation clip, sampling và blending (CPU). Dùng bởi client để tính ma trận skinning.
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace pb::asset {

struct Transform {
  glm::vec3 t{0.0f};
  glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 s{1.0f};
  [[nodiscard]] glm::mat4 matrix() const;
};

// Joint được sắp xếp sao cho parent luôn đứng trước child (parents[i] < i hoặc -1).
struct Skeleton {
  std::vector<std::string> names;
  std::vector<int> parents;
  std::vector<Transform> bind_local;
  std::vector<glm::mat4> inverse_bind;
  glm::mat4 root_transform{1.0f};  // transform của các node cha (không phải joint) phía trên root joint
  [[nodiscard]] size_t size() const { return parents.size(); }
  [[nodiscard]] int find(const std::string& name) const;
};

enum class Interp { Step, Linear, CubicSpline };
enum class Path { Translation, Rotation, Scale };

struct Track {
  int joint = -1;
  Path path = Path::Translation;
  Interp interp = Interp::Linear;
  std::vector<float> times;
  std::vector<glm::vec4> values;  // CubicSpline: 3 giá trị mỗi key (in-tangent, value, out-tangent)
};

struct AnimationClip {
  std::string name;
  float duration = 0.0f;
  std::vector<Track> tracks;
};

using Pose = std::vector<Transform>;

// Lấy mẫu clip tại thời điểm time (giây). Joint không có track giữ bind pose.
void sample(const AnimationClip& clip, const Skeleton& skel, float time, bool loop, Pose& out);
// out = lerp(a, b, w) (rotation dùng slerp theo đường ngắn nhất).
void blend(const Pose& a, const Pose& b, float w, Pose& out);
// Ma trận model-space của mỗi joint.
void model_matrices(const Skeleton& skel, const Pose& pose, std::vector<glm::mat4>& out);
// Ma trận skinning = model * inverse_bind.
void skinning_matrices(const Skeleton& skel, const Pose& pose, std::vector<glm::mat4>& out);
// Kiểm tra: parent hợp lệ và đứng trước, số inverse_bind khớp. Trả chuỗi rỗng nếu hợp lệ.
[[nodiscard]] std::string validate_skeleton(const Skeleton& skel);

}  // namespace pb::asset
