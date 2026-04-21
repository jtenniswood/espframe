#pragma once

#include "esphome/core/component.h"
#include "espframe_helpers.h"

namespace esphome {
namespace espframe {

class EspFrameComponent : public Component {
 public:
  EspFrameSlideshow &slideshow() { return this->slideshow_; }
  const EspFrameSlideshow &slideshow() const { return this->slideshow_; }

 protected:
  EspFrameSlideshow slideshow_{};
};

}  // namespace espframe
}  // namespace esphome
