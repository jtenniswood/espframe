#include "recovery_card.h"
#include "setup_dns.h"
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
  constexpr std::array<uint8_t, 4> setup_ip{192, 168, 4, 1};
  constexpr std::array<uint8_t, 19> query{
      0x12, 0x34, 0x01, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 'a', 0, 0, 1, 0, 1};
  std::array<uint8_t, 64> dns{};
  std::copy(query.begin(), query.end(), dns.begin());
  assert(setup_dns_reply(dns, query.size() + 11, setup_ip) == query.size() + 16);
  assert(dns[0] == 0x12 && dns[1] == 0x34 && dns[2] == 0x84 && dns[7] == 1 && dns[11] == 0);
  assert(std::equal(setup_ip.begin(), setup_ip.end(), dns.begin() + query.size() + 12));
  std::copy(query.begin(), query.end(), dns.begin());
  dns[16] = 28;
  assert(setup_dns_reply(dns, query.size(), setup_ip) == query.size());
  assert(dns[7] == 0);
  std::copy(query.begin(), query.end(), dns.begin());
  assert(setup_dns_reply(dns, query.size() - 1, setup_ip) == 0);
  dns[12] = 64;
  assert(setup_dns_reply(dns, query.size(), setup_ip) == 0);

  constexpr uint32_t home_ip = 0xc0a8012a;
  static_assert(setup_page(true, false, 0) == SetupPage::Wifi);
  static_assert(setup_page(true, false, home_ip) == SetupPage::Immich);
  static_assert(setup_page(false, false, home_ip) == SetupPage::Immich);
  static_assert(setup_page(false, false, 0) == SetupPage::None);
  static_assert(setup_page(false, true, home_ip) == SetupPage::None);
  static_assert(setup_page(true, true, home_ip) == SetupPage::None);

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

    for (const bool immich_setup : {false, true}) {
      const auto draw = [&](uint16_t *frame, ScreenRotation rotation) {
        if (immich_setup)
          draw_immich_setup(frame, localized, "http://192.168.1.42", rotation);
        else
          draw_wifi_setup(frame, localized, access_point, rotation);
      };
      std::fill(normal.begin(), normal.end(), photo);
      draw(normal.data(), ScreenRotation::Normal);
      draw(inverted.data(), ScreenRotation::UpsideDown);
      assert(std::equal(normal.begin(), normal.end(), inverted.rbegin()));
      assert(std::count(normal.begin(), normal.end(), uint16_t{0}) > 0.9 * pixel_count);
      assert(std::count(normal.begin(), normal.end(), uint16_t{0xffff}) > 100);
      if (!immich_setup) {
        const Viewport viewport{ScreenRotation::Normal};
        const int x = viewport.width / 2;
        const int y = viewport.height / 2;
        assert(normal[viewport.offset(0, 0)] == 0);
        // The icon fits above the heading and draws at every selected language.
        int icon_pixels = 0;
        for (int row = y - 165; row < y - 102; ++row)
          for (int col = x - 32; col < x + 32; ++col)
            icon_pixels += normal[viewport.offset(col, row)] == 0xffff;
        assert(icon_pixels > 300);

        int left = viewport.width;
        int right = 0;
        // Four 42px instruction lines share the same baseline spacing.
        for (int row = y + 82; row < y + 124; ++row)
          for (int col = 0; col < viewport.width; ++col)
            if (normal[viewport.offset(col, row)] != 0) {
              left = std::min(left, col);
              right = std::max(right, col);
            }
        assert(right - left > 245 && right - left < 300);

        for (const int top : {y - 2, y + 40, y + 124}) {
          int instruction_pixels = 0;
          for (int row = top; row < top + 42; ++row)
            for (int col = 0; col < viewport.width; ++col)
              instruction_pixels += normal[viewport.offset(col, row)] != 0;
          assert(instruction_pixels > 100);
        }
      }
      if (immich_setup) {
        const Viewport viewport{ScreenRotation::Normal};
        // The browser directions, IP address, and footer must all draw.
        for (const int top : {viewport.height / 2 - 55, viewport.height / 2 + 5, viewport.height / 2 + 110}) {
          int pixels = 0;
          for (int row = top; row < top + 60; ++row)
            for (int col = 0; col < viewport.width; ++col)
              pixels += normal[viewport.offset(col, row)] != 0;
          assert(pixels > 400);
        }
        draw_immich_setup(inverted.data(), localized, "http://192.168.1.43", ScreenRotation::Normal);
        assert(normal != inverted);
      }
      for (const auto rotation : {ScreenRotation::Clockwise, ScreenRotation::Counterclockwise}) {
        draw(normal.data(), rotation);
        assert(std::count(normal.begin(), normal.end(), uint16_t{0xffff}) > 100);
      }
    }
  }
}
