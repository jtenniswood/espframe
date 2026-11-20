#pragma once

namespace espframe::native {

enum class PhotoOrientation { Any, Portrait, Landscape };

constexpr const char *photo_orientation_name(PhotoOrientation orientation) {
  switch (orientation) {
    case PhotoOrientation::Portrait: return "Portrait Only";
    case PhotoOrientation::Landscape: return "Landscape Only";
    case PhotoOrientation::Any: return "Any";
  }
  return "Any";
}

constexpr bool matches_photo_orientation(PhotoOrientation orientation, bool portrait) {
  return orientation == PhotoOrientation::Any || (orientation == PhotoOrientation::Portrait) == portrait;
}

}  // namespace espframe::native
