#include "improv_wire.h"

#include <cassert>

using namespace espframe::native;
using namespace espframe::native::improv;

int main() {
  const std::array<uint8_t, 13> state_command{'I', 'M', 'P', 'R', 'O', 'V', 1, 3, 2, 2, 0, 0xe5, '\n'};
  const std::array<uint8_t, 2> state_payload{2, 0};
  const auto outgoing = encode(Type::Command, state_payload);
  assert(outgoing.size == state_command.size());
  assert(std::equal(state_command.begin(), state_command.end(), outgoing.bytes.begin()));

  Reader reader;
  for (size_t i = 0; i < state_command.size() - 2; ++i)
    assert(reader.feed(state_command[i]) == Read::Partial);
  assert(reader.active() && reader.feed(state_command[11]) == Read::Complete);
  assert(!reader.active() && valid_command(reader.payload()) && reader.payload().front() == 2);

  for (size_t i = 0; i < state_command.size() - 2; ++i)
    assert(reader.feed(state_command[i]) == Read::Partial);
  assert(reader.feed(0) == Read::Invalid && !reader.active());
  assert(reader.feed('I') == Read::Partial && reader.feed('X') == Read::Invalid);

  const std::array<uint8_t, 9> credentials{3, 'L', 'a', 'b', 4, 'a', '\n', 'b', 'c'};
  const auto wifi = read_wifi(credentials);
  assert(wifi && std::string_view(wifi->ssid.data()) == "Lab" && std::string_view(wifi->password.data()) == "a\nbc");
  assert(!read_wifi(std::span(credentials).first(8)));
  auto bad = credentials;
  bad[2] = '\0';
  assert(!read_wifi(bad));
  bad[0] = 255;
  assert(!read_wifi(bad));

  const std::array<uint8_t, 2> bad_length{1, 255};
  assert(!valid_command(bad_length));
  const auto networks = result(Command::Scan, {"Lab", "-50", "YES"});
  const std::array<uint8_t, 14> expected{4, 12, 3, 'L', 'a', 'b', 3, '-', '5', '0', 3, 'Y', 'E', 'S'};
  assert(networks.bytes[7] == 4 && networks.bytes[8] == expected.size());
  assert(std::equal(expected.begin(), expected.end(), networks.bytes.begin() + payload_offset));
  const auto end = result(Command::Scan, {});
  assert(end.bytes[8] == 2 && end.bytes[9] == 4 && end.bytes[10] == 0);
}
