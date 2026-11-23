#include "preview_prefetch.h"

#include "esp_heap_caps.h"

#include <array>
#include <cassert>
#include <cstring>
#include <utility>

using namespace espframe::native;

struct MockSemaphore {
  bool ready{};
};

struct MockTask {
  TaskFunction_t run{};
  void *context{};
  bool notified{};
};

namespace {

struct BlockedTask {};
MockSemaphore completion;
MockTask task;
int task_creations;
int requests;
int reply = 200;
int64_t reply_delay_us{};
bool interrupt_wait;
bool hold_download;
bool cancel_after_transfer;
int64_t now_us = 100'000;
TickType_t last_wait_ms{};
std::atomic<bool> slide_cancelled{};
const std::string base_url = "https://photos.test/api";
const std::string key = "test-key";

AssetId asset(char label) {
  AssetId id{};
  id[0] = label;
  return id;
}

// Run an in-flight transfer at a controlled scheduling point.
void service_download() {
  assert(task.notified);
  try {
    task.run(task.context);
  } catch (const BlockedTask &) {
  }
}

}  // namespace

SemaphoreHandle_t xSemaphoreCreateBinary() { return &completion; }

int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
  if (!semaphore->ready && timeout != 0) {
    if (interrupt_wait) {
      slide_cancelled = true;
      interrupt_wait = false;
      return 0;
    }
    if (hold_download) {
      last_wait_ms = timeout;
      now_us += 1'000 * timeout;
      return 0;
    }
    service_download();
    if (std::exchange(cancel_after_transfer, false))
      slide_cancelled = true;
  }
  return std::exchange(semaphore->ready, false);
}

void xSemaphoreGive(SemaphoreHandle_t semaphore) {
  assert(!semaphore->ready);
  semaphore->ready = true;
}

int xTaskCreatePinnedToCore(TaskFunction_t fn, const char *, unsigned, void *context, unsigned,
                            TaskHandle_t *handle, int) {
  ++task_creations;
  task = {fn, context, false};
  *handle = &task;
  return pdPASS;
}

int xPortGetCoreID() { return 0; }

void xTaskNotifyGive(TaskHandle_t handle) {
  assert(!handle->notified);
  handle->notified = true;
}

uint32_t ulTaskNotifyTake(int, TickType_t) {
  if (!std::exchange(task.notified, false))
    throw BlockedTask{};
  return 1;
}

int64_t esp_timer_get_time() { return now_us; }

ImmichHttp::~ImmichHttp() = default;
void ImmichHttp::disconnect() {}
void ImmichHttp::reset() {}

int ImmichHttp::read(const std::string &url, const char *api_key, std::string_view query, std::span<uint8_t> output,
                      size_t &received, int64_t deadline) {
  received = 0;
  if (cancelled_)
    return 0;
  const bool thumbnail = url.ends_with("/thumbnail?size=thumbnail");
  assert(url.starts_with(base_url + "/assets/") && (thumbnail || url.ends_with("/thumbnail?size=preview")));
  assert(api_key == key && query.empty() && deadline > esp_timer_get_time());
  assert(output.size() == (thumbnail ? PreviewPrefetch::thumbnail_capacity : PreviewPrefetch::capacity));
  ++requests;
  if (reply_delay_us >= deadline - now_us) {
    now_us = deadline;
    return 0;
  }
  now_us += reply_delay_us;
  if (reply == 200) {
    output.front() = static_cast<uint8_t>(url[base_url.size() + std::strlen("/assets/")]);
    received = 1;
  }
  return reply;
}

int main() {
  PreviewPrefetch prefetch(slide_cancelled);
  std::array<uint8_t, 4> buffer{};
  free_spiram = PreviewPrefetch::capacity;
  prefetch.start(asset('a'), 1, base_url, key);
  assert(!task.notified && !prefetch.take(asset('a'), 1, buffer));
  free_spiram = 32 * 1024 * 1024;

  std::string transient_key = key;
  prefetch.start(asset('a'), 1, base_url, transient_key);
  transient_key.assign("replaced-key");
  prefetch.start(asset('a'), 1, base_url, transient_key);
  assert(task.notified && requests == 0);
  assert(prefetch.take(asset('a'), 1, buffer) == 1);
  assert(buffer[0] == 'a' && buffer[1] == 0 && requests == 1);

  prefetch.start(asset('b'), 1, base_url, key);
  service_download();
  assert(prefetch.take(asset('b'), 1, buffer) == 1);
  assert(buffer[0] == 'b' && buffer[1] == 0 && requests == 2);

  prefetch.start(asset('c'), 1, base_url, key);
  service_download();
  assert(!prefetch.take(asset('c'), 2, buffer) && buffer[0] == 'b');
  assert(!prefetch.take(asset('c'), 1, buffer));
  prefetch.start(asset('d'), 2, base_url, key);
  interrupt_wait = true;
  assert(!prefetch.take(asset('d'), 2, buffer) && buffer[0] == 'b');
  service_download();
  slide_cancelled = false;
  assert(requests == 3);

  reply = 503;
  prefetch.start(asset('e'), 2, base_url, key);
  assert(!prefetch.take(asset('e'), 2, buffer) && buffer[0] == 'b');
  assert(requests == 4);

  // A companion started during the primary decode uses the same staging
  // slot, including when its bytes are ready before the bounded wait starts.
  reply = 200;
  prefetch.start(asset('f'), 2, base_url, key);
  service_download();
  assert(prefetch.take(asset('f'), 2, buffer, now_us + 1'500'000) == 1);
  assert(requests == 5 && buffer[0] == 'f');

  prefetch.start(asset('g'), 2, base_url, key);
  const auto deadline = now_us + 25'000;
  hold_download = true;
  assert(!prefetch.take(asset('g'), 2, buffer, deadline));
  assert(now_us == deadline && last_wait_ms == 5 && task.notified && buffer[0] == 'f');
  hold_download = false;
  service_download();
  assert(requests == 5);

  // An over-limit staged download falls back to the caller's larger buffer.
  reply = 413;
  prefetch.start(asset('h'), 2, base_url, key);
  assert(!prefetch.take(asset('h'), 2, buffer, now_us + 1'500'000));
  assert(requests == 6 && buffer[0] == 'f');

  prefetch.start(asset('i'), 2, base_url, key);
  prefetch.release_on_memory_pressure();
  prefetch.start(asset('j'), 2, base_url, key);
  assert(!task.notified && !prefetch.take(asset('j'), 2, buffer));
  assert(requests == 6 && buffer[0] == 'f');

  now_us += 29'999'999;
  prefetch.start(asset('j'), 2, base_url, key);
  assert(!task.notified);

  // A recovered decoder needs more headroom than on initial startup.
  ++now_us;
  free_spiram = 7 * PreviewPrefetch::capacity;
  prefetch.start(asset('j'), 2, base_url, key);
  assert(!task.notified);
  free_spiram = 8 * PreviewPrefetch::capacity;
  reply = 200;
  prefetch.start(asset('j'), 2, base_url, key);
  assert(task.notified && prefetch.take(asset('j'), 2, buffer) == 1);
  assert(requests == 7 && task_creations == 1 && buffer[0] == 'j');

  // A rewind may need a different preview before the already downloaded
  // lookahead is needed again.
  prefetch.start(asset('k'), 2, base_url, key);
  service_download();
  const auto downloaded = requests;
  slide_cancelled = true;
  prefetch.cancel_incomplete();
  assert(!prefetch.take(asset('k'), 2, buffer));
  slide_cancelled = false;
  assert(!prefetch.take(asset('l'), 2, buffer));
  assert(prefetch.take(asset('k'), 2, buffer) == 1);
  assert(requests == downloaded && buffer[0] == 'k');

  // A queued transfer still yields without holding up the new swipe.
  prefetch.start(asset('m'), 2, base_url, key);
  prefetch.cancel_incomplete();
  assert(task.notified && !prefetch.take(asset('m'), 2, buffer));
  service_download();
  assert(requests == downloaded);

  // Cancellation can arrive after the download signaled completion.
  prefetch.start(asset('n'), 2, base_url, key);
  cancel_after_transfer = true;
  assert(!prefetch.take(asset('n'), 2, buffer) && buffer[0] == 'k');
  assert(slide_cancelled && requests == downloaded + 1);
  slide_cancelled = false;
  assert(prefetch.take(asset('n'), 2, buffer) == 1 && buffer[0] == 'n');
  assert(requests == downloaded + 1);

  // A needed preview can take longer than a speculative-only request.
  reply_delay_us = 3'000'000;
  prefetch.start(asset('o'), 2, base_url, key);
  const auto slow_started = now_us;
  assert(prefetch.take(asset('o'), 2, buffer) == 1);
  assert(now_us - slow_started == reply_delay_us && requests == downloaded + 2 && buffer[0] == 'o');

  // A required transfer that times out while needed should use the normal
  // slide retry path without immediately starting a second full download.
  reply_delay_us = ImmichHttp::transfer_budget_us + 1;
  prefetch.start(asset('p'), 2, base_url, key);
  const auto timeout_started = now_us;
  assert(prefetch.take(asset('p'), 2, buffer) == 0);
  assert(now_us - timeout_started == ImmichHttp::transfer_budget_us && requests == downloaded + 3);

  // A request that already failed during lookahead doesn't delay the
  // foreground attempt when the slide worker eventually needs it.
  prefetch.start(asset('q'), 2, base_url, key);
  service_download();
  assert(!prefetch.take(asset('q'), 2, buffer) && buffer[0] == 'o');

  // The slide worker also stops waiting if the network task has yet to
  // signal when the deadline arrives.
  prefetch.start(asset('r'), 2, base_url, key);
  hold_download = true;
  assert(prefetch.take(asset('r'), 2, buffer) == 0);
  hold_download = false;
  service_download();
  assert(requests == downloaded + 4);

  constexpr auto thumbnail = PreviewPrefetch::Size::Thumbnail;
  reply_delay_us = 0;
  const int before_thumbnails = requests;
  assert(prefetch.start(asset('s'), 2, base_url, key));
  assert(!prefetch.start(asset('s'), 2, base_url, key, thumbnail));
  assert(!prefetch.take(asset('s'), 2, buffer, now_us + PreviewPrefetch::thumbnail_budget_us, thumbnail));
  assert(prefetch.take(asset('s'), 2, buffer) == 1 && buffer[0] == 's');

  assert(prefetch.start(asset('s'), 2, base_url, key, thumbnail));
  assert(!prefetch.take(asset('s'), 2, buffer));
  service_download();
  assert(prefetch.take(asset('s'), 2, buffer, now_us + PreviewPrefetch::thumbnail_budget_us, thumbnail) == 1);
  assert(requests == before_thumbnails + 2 && buffer[0] == 's');

  reply = 503;
  assert(prefetch.start(asset('t'), 2, base_url, key, thumbnail));
  assert(prefetch.take(asset('t'), 2, buffer, now_us + PreviewPrefetch::thumbnail_budget_us, thumbnail) == 0);
  assert(!prefetch.take(asset('t'), 2, buffer, now_us + PreviewPrefetch::thumbnail_budget_us, thumbnail));
  assert(requests == before_thumbnails + 3 && buffer[0] == 's');

  reply = 200;
  reply_delay_us = 2 * PreviewPrefetch::thumbnail_budget_us + 1;
  assert(prefetch.start(asset('u'), 2, base_url, key, thumbnail));
  const auto thumbnail_started = now_us;
  assert(prefetch.take(asset('u'), 2, buffer, now_us + 3 * PreviewPrefetch::thumbnail_budget_us, thumbnail) == 0);
  assert(now_us - thumbnail_started == 2 * PreviewPrefetch::thumbnail_budget_us);

  assert(prefetch.start(asset('v'), 2, base_url, key, thumbnail));
  hold_download = true;
  const auto thumbnail_needed = now_us;
  assert(prefetch.take(asset('v'), 2, buffer, now_us + PreviewPrefetch::thumbnail_budget_us, thumbnail) == 0);
  assert(now_us - thumbnail_needed == PreviewPrefetch::thumbnail_budget_us);
  hold_download = false;
  service_download();
  prefetch.release_on_memory_pressure();
}
