#include "components/remote_image/webp_rgb565.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using esphome::remote_image::decode_webp_rgb565;
static WebPDecoderConfig config_for(int width, int height) {
  WebPDecoderConfig config;
  assert(WebPInitDecoderConfig(&config));
  config.options.no_fancy_upsampling = 1;
  config.options.use_scaling = 1;
  config.options.scaled_width = width;
  config.options.scaled_height = height;
  return config;
}
static void check(const std::vector<uint8_t> &input, int width, int height, bool big_endian) {
  // Reference is the existing RGB888 decode followed by truncating RGB565 conversion.
  auto reference = config_for(width, height);
  std::vector<uint8_t> rgb(width * height * 3);
  reference.output.colorspace = MODE_RGB;
  reference.output.is_external_memory = 1;
  reference.output.u.RGBA.rgba = rgb.data();
  reference.output.u.RGBA.stride = width * 3;
  reference.output.u.RGBA.size = rgb.size();
  assert(WebPDecode(input.data(), input.size(), &reference) == VP8_STATUS_OK);
  WebPFreeDecBuffer(&reference.output);
  // Sentinel bytes prove the decoder and byte swap respect output bounds.
  std::vector<uint8_t> actual(width * height * 2 + 16, 0xA5);
  auto config = config_for(width, height);
  assert(decode_webp_rgb565(input.data(), input.size(), config, actual.data() + 8,
                            actual.size() - 16, width, height, big_endian) == VP8_STATUS_OK);
  for (int i = 0; i < 8; ++i) assert(actual[i] == 0xA5 && actual[actual.size() - 1 - i] == 0xA5);
  for (int i = 0; i < width * height; ++i) {
    uint16_t value = ((rgb[i * 3] & 0xF8) << 8) | ((rgb[i * 3 + 1] & 0xFC) << 3) | (rgb[i * 3 + 2] >> 3);
    assert(actual[8 + i * 2] == (big_endian ? value >> 8 : value & 255));
    assert(actual[9 + i * 2] == (big_endian ? value & 255 : value >> 8));
  }
  auto small = config_for(width, height);
  assert(decode_webp_rgb565(input.data(), input.size(), small, actual.data() + 8,
                            width * height * 2 - 1, width, height, big_endian) == VP8_STATUS_INVALID_PARAM);
  auto truncated = config_for(width, height);
  assert(decode_webp_rgb565(input.data(), input.size() / 2, truncated, actual.data() + 8,
                            actual.size() - 16, width, height, big_endian) != VP8_STATUS_OK);
}
int main() {
  for (const char *name : {"landscape-lossless", "landscape-lossy", "portrait-alpha"}) {
    std::ifstream file(std::string("tests/fixtures/webp/") + name + ".webp", std::ios::binary);
    std::vector<uint8_t> input{std::istreambuf_iterator<char>(file), {}};
    assert(!input.empty());
    int width, height;
    assert(WebPGetInfo(input.data(), input.size(), &width, &height));
    for (bool big_endian : {false, true}) {
      check(input, width, height, big_endian);
      check(input, width * 2, height * 2, big_endian);
      check(input, std::max(1, width / 2), std::max(1, height / 2), big_endian);
    }
  }
  std::cout << "WebP RGB565 real-decoder tests passed\n";
}
