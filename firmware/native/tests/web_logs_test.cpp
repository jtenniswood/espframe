#include "web_logs.h"

#include <cassert>
#include <string>
#include <string_view>

int main() {
  using espframe::native::WebLogBuffer;
  using espframe::native::WebLogLine;
  WebLogBuffer buffer;
  std::array<WebLogLine, 3> lines{};
  assert(buffer.read(17, 0, lines).count == 0);
  buffer.append("W (1) http_transport: SDK diagnostics");
  buffer.append("I (broken tag prefix");
  buffer.append("E (12) immich: Preview failed\n");
  buffer.append("\x1b[0;33mW (13) network: Disconnected\x1b[0m\n");
  buffer.append("I swipe-probe: " + std::string(280, 'x'));
  buffer.append("D firmware-update: Details");
  auto page = buffer.read(17, 0, lines);
  assert(page.boot == 17 && page.count == 3 && page.more);
  assert(lines[0].id == 1 && lines[0].level == 1 && std::string_view(lines[0].message.data()) == "E (12) immich: Preview failed");
  assert(lines[1].id == 2 && lines[1].level == 2 && std::string_view(lines[1].message.data()).starts_with("\x1b[0;33mW"));
  assert(lines[2].id == 3 && lines[2].level == 3 && std::string_view(lines[2].message.data()).size() == lines[2].message.size() - 1);
  page = buffer.read(17, 3, lines);
  assert(page.count == 1 && !page.more && lines[0].id == 4 && lines[0].level == 5);
  assert(buffer.read(17, 99, lines).count == 0);

  for (size_t i = 0; i < WebLogBuffer::capacity + 2; ++i)
    buffer.append("I network: Connected");
  page = buffer.read(17, 4, lines);
  assert(page.count == 3 && page.more && lines[0].id == 7 && lines[2].id == 9);
  page = buffer.read(17, WebLogBuffer::capacity + 5, lines);
  assert(page.count == 1 && !page.more && lines[0].id == WebLogBuffer::capacity + 6);
}
