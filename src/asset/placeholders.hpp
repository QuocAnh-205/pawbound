#pragma once
// Asset placeholder sinh bằng code (quality = placeholder). Dùng khi chưa có asset thật.
#include "asset/glb_writer.hpp"

namespace pb::asset {

// Chó placeholder: các khối hộp gắn cứng vào 8 joint (root, body, head, tail, 4 chân),
// animation idle / walk / run lặp vòng. Cao ~0,6 m, mặt trước +Z.
struct PlaceholderDog {
  Skeleton skeleton;
  GlbDesc desc;  // desc.skeleton trỏ vào skeleton bên trên — không copy struct này sau khi tạo
};
void make_placeholder_dog(PlaceholderDog& out);

}  // namespace pb::asset
