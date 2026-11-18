#pragma once

#include <cstddef>
#include <cstdint>

namespace espframe::native {

// Worker-owned packed RGB565 preview. Retain the primary until the optional
// companion is ready so a single photo can still fill the slide on failure.
class DecodedPreview {
 public:
  enum class Result { Ready, Invalid, NoMemory };

  DecodedPreview() = default;
  ~DecodedPreview();
  DecodedPreview(const DecodedPreview &) = delete;
  DecodedPreview &operator=(const DecodedPreview &) = delete;

  Result decode(const uint8_t *encoded, size_t length, bool companion = false);
  bool portrait() const { return this->portrait_; }
  void render(uint16_t *panel, const DecodedPreview *companion = nullptr) const;

 private:
  void paint_(uint16_t *panel, int x, int width) const;

  uint16_t *pixels_{};
  int width_{};
  int height_{};
  bool portrait_{};
};

}  // namespace espframe::native
