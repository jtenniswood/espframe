#include "components/espframe/memory_diagnostics.h"
#include <cassert>
#include <cstdarg>
#include <string>

static bool stack_logged = false;
namespace memory_test {
void log(const char *format, ...) {
  stack_logged = std::string(format).find("minimum_free_bytes") != std::string::npos;
}
}

int main() {
#ifdef ESPFRAME_MEMORY_DIAGNOSTICS
  esphome::espframe::record_loop_stack("helper-only");
  assert(stack_logged);
#endif
}
