#include "web_logs.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <new>

namespace espframe::native {
namespace {

std::mutex logs_mutex;
WebLogBuffer *logs{};
uint32_t boot_id{};
std::atomic<vprintf_like_t> console_writer{std::vprintf};

int capture_log(const char *format, va_list arguments) {
  // Most IDF calls use the regular UART writer. Only copy bounded Log V1
  // records whose tag is safe to show on the admin page.
  if (format[0] == 'E' || format[0] == 'W' || format[0] == 'I' ||
      format[0] == 'D' || format[0] == 'V' || format[0] == '\x1b') {
    std::array<char, 192> message{};
    va_list copy;
    va_copy(copy, arguments);
    const int length = std::vsnprintf(message.data(), message.size(), format, copy);
    va_end(copy);
    if (length > 0) {
      const std::unique_lock lock(logs_mutex, std::try_to_lock);
      if (lock.owns_lock())
        logs->append({message.data(), std::min(static_cast<size_t>(length), message.size() - 1)});
    }
  }
  return console_writer.load(std::memory_order_relaxed)(format, arguments);
}

}  // namespace

void start_web_logs() {
  auto *memory = heap_caps_malloc(sizeof(WebLogBuffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory)
    return;
  {
    const std::lock_guard lock(logs_mutex);
    logs = new (memory) WebLogBuffer{};
    boot_id = esp_random();
  }
  console_writer.store(esp_log_set_vprintf(capture_log), std::memory_order_relaxed);
}

WebLogPage read_web_logs(uint32_t after, std::span<WebLogLine> output) {
  const std::lock_guard lock(logs_mutex);
  return logs ? logs->read(boot_id, after, output) : WebLogPage{};
}

}  // namespace espframe::native
