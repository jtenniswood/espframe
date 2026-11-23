#include "native_release_index.h"
#include "native_release_schedule.h"

#include <cassert>
#include <string>

using namespace espframe::native;

int main() {
  assert(parse_preview_version("v0.1.0-preview.10") > parse_preview_version("v0.1.0-preview.9"));
  assert(parse_preview_version("v0.2.0-preview.1") > parse_preview_version("v0.1.0-preview.99"));
  assert(!parse_preview_version("v1.0.0"));
  assert(!parse_preview_version("v0.1.0-preview.01"));
  assert(!parse_preview_version("v0.1.0-preview.4294967296"));
  assert(!parse_preview_version("v0.1.0-preview.1/../../v1"));

  const auto entry = [](const char *version, uint32_t bytes, char hex) {
    return std::string(R"({"version":")") + version + R"(","size":)" + std::to_string(bytes) +
           R"(,"sha256":")" + std::string(64, hex) + R"(","elf_sha256":")" + std::string(64, '1') + R"("})";
  };
  const auto index = [](const std::string &entries) {
    return std::string(R"({"schema":1,"board":"v1","versions":[)") + entries + "]}\n";
  };
  const auto latest = entry("v0.1.0-preview.10", 4'000'000, 'a');
  const auto previous = entry("v0.1.0-preview.9", 3'500'000, 'b');
  const auto parsed = read_release_index(index(latest + "," + previous), "v1");
  assert(parsed && parsed->size() == 2 && parsed->front().size == 4'000'000);
  assert(parsed->front().sha256[0] == 0xaa && parsed->front().elf_sha256[0] == 0x11);
  assert(read_release_index(index(""), "v1")->empty());
  assert(!read_release_index(index(latest), "v2"));
  assert(!read_release_index(index(latest + "," + latest), "v1"));
  assert(!read_release_index(index(previous + "," + latest), "v1"));
  assert(!read_release_index(index(entry("v0.1.0-preview.9", 4'194'305, 'a')), "v1"));
  assert(!read_release_index(index(entry("v0.1.0-preview.9", 3'500'000, 'x')), "v1"));
  assert(!read_release_index(index(latest) + "{}", "v1"));

  constexpr int64_t hour = 3'600'000'000;
  NativeReleaseSchedule schedule;
  schedule.configure(0, true, "Daily");
  assert(!schedule.due(hour - 1) && schedule.due(hour));
  schedule.checked(hour, true);
  schedule.configure(2 * hour, true, "Daily");
  assert(!schedule.due(25 * hour - 1) && schedule.due(25 * hour));
  schedule.configure(3 * hour, false, "Daily");
  assert(!schedule.due(25 * hour));
  schedule.configure(4 * hour, true, "Weekly");
  assert(schedule.due(4 * hour));
  schedule.checked(4 * hour, false);
  assert(!schedule.due(4 * hour + hour / 4 - 1) && schedule.due(4 * hour + hour / 4));
  schedule.configure(5 * hour, true, "Monthly");
  assert(!schedule.due(725 * hour - 1) && schedule.due(725 * hour));

  NativeReleaseSchedule disabled;
  disabled.configure(0, false, "Hourly");
  assert(!disabled.due(hour));
  disabled.configure(hour, true, "Hourly");
  assert(disabled.due(hour));
  disabled.checked(hour, true);
  assert(!disabled.due(2 * hour - 1) && disabled.due(2 * hour));
}
