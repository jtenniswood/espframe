#include "recovery_card.h"
#include "i18n_generated.h"
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

  assert(argc == 3);
  std::ifstream atlas_file(argv[1], std::ios::binary);
  std::vector<uint8_t> atlas{std::istreambuf_iterator<char>{atlas_file}, {}};
  assert(atlas.size() == 95 * (1 + 32 * 36 / 2));
  std::ifstream masks_file(argv[2], std::ios::binary);
  const std::vector<uint8_t> masks{std::istreambuf_iterator<char>{masks_file}, {}};
  const StatusText english{masks, 0};
  RecoveryNetwork access_point;
  std::memcpy(access_point.ssid.data(), "espframe_BBBBBB", sizeof("espframe_BBBBBB"));
  std::memcpy(access_point.password.data(), "WWWWWWWWWWWW", sizeof("WWWWWWWWWWWW"));
  const uint16_t photo = 0x4321;
  std::vector<uint16_t> normal(pixel_count, photo);
  std::vector<uint16_t> inverted(pixel_count, photo);
  draw_recovery_card(normal.data(), atlas.data(), english, access_point, ScreenRotation::Normal);
  draw_recovery_card(inverted.data(), atlas.data(), english, access_point, ScreenRotation::UpsideDown);
  assert(std::equal(normal.begin(), normal.end(), inverted.rbegin()));
  assert(std::count(normal.begin(), normal.end(), photo) == pixel_count - 504 * 184);
  assert(std::count(normal.begin(), normal.end(), 0xffff) > 900);

  constexpr PhotoSourceStatus notices[]{
      PhotoSourceStatus::WaitingForTime, PhotoSourceStatus::MemoriesEmpty,
      PhotoSourceStatus::MemoriesUnavailable, PhotoSourceStatus::IncompleteFilters,
      PhotoSourceStatus::UnsupportedFilters, PhotoSourceStatus::FiltersUnavailable,
      PhotoSourceStatus::NoPhotos, PhotoSourceStatus::WaitingForPair};
  for (const auto status : notices) {
    std::fill(normal.begin(), normal.end(), photo);
    std::fill(inverted.begin(), inverted.end(), photo);
    draw_source_status(normal.data(), english, status, ScreenRotation::Normal);
    draw_source_status(inverted.data(), english, status, ScreenRotation::UpsideDown);
    assert(std::equal(normal.begin(), normal.end(), inverted.rbegin()));
    assert(std::count(normal.begin(), normal.end(), photo) <= pixel_count - 396 * 60);
  }

  std::fill(normal.begin(), normal.end(), photo);
  draw_connection_notice(normal.data(), english, ImmichResponse::Rejected, ScreenRotation::Normal);
  const auto english_warning = normal;
  auto verify_notice = [&](auto draw) {
    std::fill(normal.begin(), normal.end(), photo);
    std::fill(inverted.begin(), inverted.end(), photo);
    draw(normal.data(), ScreenRotation::Normal);
    draw(inverted.data(), ScreenRotation::UpsideDown);
    assert(std::equal(normal.begin(), normal.end(), inverted.rbegin()));
    assert(std::count(normal.begin(), normal.end(), uint16_t{0xffff}) > 10);
    assert(std::count(normal.begin(), normal.end(), photo) > 0.8 * pixel_count);
  };
  for (uint8_t language = 0; language < espframe_i18n_catalogue::LANGUAGE_COUNT; ++language) {
    const StatusText localized{masks, language};
    verify_notice([&](uint16_t *frame, ScreenRotation rotation) {
      draw_recovery_card(frame, atlas.data(), localized, access_point, rotation);
    });
    verify_notice([&](uint16_t *frame, ScreenRotation rotation) {
      draw_connection_notice(frame, localized, ImmichResponse::Unreachable, rotation);
    });
    for (const auto status : notices) {
      verify_notice([&](uint16_t *frame, ScreenRotation rotation) {
        draw_source_status(frame, localized, status, rotation);
      });
    }
    verify_notice([&](uint16_t *frame, ScreenRotation rotation) {
      draw_connection_notice(frame, localized, ImmichResponse::Rejected, rotation);
    });
    assert((normal == english_warning) == (language == 0));
  }
}
