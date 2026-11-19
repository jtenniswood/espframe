#include "recovery_card.h"
#include "wifi_recovery.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace espframe::native;

int main(int argc, char **argv) {
  RecoveryWindow recovery;
  assert(!recovery.change_at(true, false));
  recovery.update(0, false);
  assert(recovery.change_at(false, false) == 0);
  assert(recovery.change_at(true, false) == 90'000'000);
  recovery.update(25'000'000, false);
  assert(recovery.change_at(true, false) == 90'000'000);
  assert(!recovery.change_at(true, true));
  recovery.update(28'000'000, true);
  assert(!recovery.change_at(true, false));
  assert(recovery.change_at(true, true) == 43'000'000);
  recovery.update(41'000'000, true);
  assert(recovery.change_at(true, true) == 43'000'000);
  recovery.update(42'000'000, false);
  assert(!recovery.change_at(true, true));
  assert(recovery.change_at(true, false) == 132'000'000);

  assert(argc == 2);
  std::ifstream atlas_file(argv[1], std::ios::binary);
  std::vector<uint8_t> atlas{std::istreambuf_iterator<char>{atlas_file}, {}};
  assert(atlas.size() == 95 * (1 + 32 * 36 / 2));
  RecoveryNetwork access_point;
  std::memcpy(access_point.ssid.data(), "espframe_BBBBBB", sizeof("espframe_BBBBBB"));
  std::memcpy(access_point.password.data(), "WWWWWWWWWWWW", sizeof("WWWWWWWWWWWW"));
  const uint16_t photo = 0x4321;
  std::vector<uint16_t> normal(pixel_count, photo);
  std::vector<uint16_t> inverted(pixel_count, photo);
  draw_recovery_card(normal.data(), atlas.data(), access_point, ScreenRotation::Normal);
  draw_recovery_card(inverted.data(), atlas.data(), access_point, ScreenRotation::UpsideDown);
  assert(std::equal(normal.begin(), normal.end(), inverted.rbegin()));
  assert(std::count(normal.begin(), normal.end(), photo) == pixel_count - 504 * 184);
  assert(std::count(normal.begin(), normal.end(), 0xffff) > 900);

  for (auto status : {MemoriesStatus::WaitingForTime, MemoriesStatus::Empty, MemoriesStatus::Unavailable}) {
    std::fill(normal.begin(), normal.end(), photo);
    std::fill(inverted.begin(), inverted.end(), photo);
    draw_memories_status(normal.data(), atlas.data(), status, ScreenRotation::Normal);
    draw_memories_status(inverted.data(), atlas.data(), status, ScreenRotation::UpsideDown);
    assert(std::equal(normal.begin(), normal.end(), inverted.rbegin()));
    assert(std::count(normal.begin(), normal.end(), photo) == pixel_count - 396 * 60);
  }
}
