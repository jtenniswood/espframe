#include <cassert>
#include <iostream>
#include <nvs.h>
#include "esphome/core/application.h"
#include "components/espframe/frame_identity.h"

using namespace esphome;
using namespace esphome::espframe;

void reset_app() {
  App.name = StringRef("original-frame-a1b2c3");
  App.friendly = StringRef("Original frame a1b2c3");
}
int main() {
  std::string name;
  assert(normalize_frame_name("  Living Room \n", name) && name == "Living Room");
  assert(normalize_frame_name(" \n\t", name) && name.empty());
  assert(normalize_frame_name("Chambre 日本語 🖼", name));
  assert(normalize_frame_name(std::string(120, 'x'), name));
  for (const auto &invalid : {std::string(121, 'x'), std::string("a\0b", 3), std::string("a\nb"),
       std::string("\xc2\x80"), std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
       std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82"), std::string("\xff")}) {
    assert(!normalize_frame_name(invalid, name));
  }
  assert(frame_hostname("Living Room", "b2c3") == "living-room-b2c3");
  assert(frame_hostname("日本語", "b2c3") == "frame-b2c3");
  assert(frame_hostname("  --Room !! TWO-- ", "b2c3") == "room-two-b2c3");
  assert(frame_hostname(std::string(120, 'a'), "b2c3").size() == 24);
  assert(frame_hostname("123456789012345678 xyz", "b2c3") == "123456789012345678-b2c3");

  reset_app();
  FrameIdentity first;
  first.setup();
  assert(first.saved_name().empty() && !first.restart_required());
  assert(write_count == 0 && identity_bytes.empty());
  assert(first.target_hostname() == "original-frame-a1b2c3");
  assert(first.save("  Living Room  "));
  assert(first.restart_required() && first.target_hostname() == "living-room-b2c3");
  assert(std::string(App.get_name().c_str()) == "original-frame-a1b2c3");
  assert(first.save("Living Room") && write_count == 1);

  reset_app();
  FrameIdentity reboot;
  reboot.setup();
  assert(reboot.saved_name() == "Living Room" && !reboot.restart_required());
  assert(std::string(App.get_name().c_str()) == "living-room-b2c3");
  assert(std::string(App.get_friendly_name().c_str()) == "Living Room");
  const char *running_name = App.get_name().c_str();
  fail_write = true;
  assert(!reboot.save("Office") && reboot.saved_name() == "Living Room");
  fail_write = false;
  fail_open = true;
  assert(!reboot.save("Office") && !reboot.restart_required());
  fail_open = false;
  fail_commit = true;
  assert(!reboot.save("Office") && reboot.saved_name() == "Living Room");
  fail_commit = false;
  assert(reboot.save(""));
  assert(App.get_name().c_str() == running_name && std::string(running_name) == "living-room-b2c3");
  assert(reboot.target_hostname() == "original-frame-a1b2c3");
  reset_app();
  FrameIdentity cleared;
  cleared.setup();
  assert(cleared.saved_name().empty() && !cleared.restart_required());
  assert(std::string(App.get_name().c_str()) == "original-frame-a1b2c3");

  identity_bytes.assign(128, 0xff);
  reset_app();
  FrameIdentity corrupt;
  corrupt.setup();
  assert(corrupt.saved_name().empty());
  assert(corrupt.save("Recovered"));
  fail_open = true;
  reset_app();
  FrameIdentity unavailable;
  unavailable.setup();
  assert(unavailable.saved_name().empty());
  assert(!unavailable.save("Office"));
  fail_open = false;
  assert(unavailable.save("Office"));
  for (bool open_failure : {true, false}) {
    assert(unavailable.save("Previously saved"));
    fail_open = open_failure;
    fail_read = !open_failure;
    reset_app();
    FrameIdentity unread;
    unread.setup();
    assert(unread.saved_name().empty());
    fail_open = fail_read = false;
    assert(unread.save(""));
    reset_app();
    FrameIdentity after_clear;
    after_clear.setup();
    assert(after_clear.saved_name().empty());
    assert(after_clear.target_hostname() == "original-frame-a1b2c3");
    assert(unavailable.save("Office"));
  }
  std::cout << "Frame identity validation, persistence, clearing and failure tests passed\n";
}
