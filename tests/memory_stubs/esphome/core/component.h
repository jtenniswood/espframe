#pragma once
namespace esphome {
namespace setup_priority { constexpr float PROCESSOR = 400; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual float get_setup_priority() const { return 600; }
};
}
