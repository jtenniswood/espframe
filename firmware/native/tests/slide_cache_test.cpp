#include "slide_cache.h"

#include <cassert>

using espframe::native::SlideCache;

int main() {
  uint16_t pixels[SlideCache::minimum_capacity]{};
  SlideCache::Buffers buffers{};
  for (size_t i = 0; i < SlideCache::minimum_capacity; ++i)
    buffers[i] = &pixels[i];
  SlideCache cache{buffers};
  assert(cache.size() == 6);

  auto first = cache.reserve(2, 0, 5);
  assert(first && cache.find(2) == nullptr);
  assert(!cache.reserve(2, 0, 5));
  *first->pixels = 42;
  assert(cache.complete(*first, true));
  assert(cache.find(2) == first->pixels && *cache.find(2) == 42);
  assert(!cache.complete(*first, false));

  for (int slide : {0, 1, 3, 4, 5}) {
    auto lease = cache.reserve(slide, 0, 5);
    assert(lease && cache.complete(*lease, true));
  }
  assert(!cache.reserve(6, 0, 5));

  auto shifted = cache.reserve(6, 1, 6);
  assert(shifted && cache.find(0) == nullptr && cache.find(1) != nullptr);
  assert(cache.find(6) == nullptr);

  // Moving the retention window cannot evict a buffer the worker is still
  // writing. Failed preparation returns that buffer to the empty pool.
  auto work = cache.reserve(7, 1, 6);
  assert(!work);
  assert(cache.complete(*shifted, false));
  auto retry = cache.reserve(6, 1, 6);
  assert(retry && retry->pixels == shifted->pixels);
  assert(!cache.complete(*shifted, true));
  assert(cache.find(6) == nullptr);
  assert(cache.complete(*retry, true));
  assert(cache.find(6) == retry->pixels);

  auto pending = cache.reserve(7, 2, 7);
  assert(pending);
  assert(!cache.reserve(8, 2, 6));
  assert(cache.complete(*pending, true));

  // Reversing direction shifts the extra prefetch slot behind the current
  // photo. Keep both adjacent slides readable during that refill.
  auto reversed = cache.reserve(1, 1, 6);
  assert(reversed && cache.find(7) == nullptr);
  assert(cache.find(3) && cache.find(4) && cache.find(5));
  assert(cache.complete(*reversed, true));

  // Reframing gets a different pixel buffer at the same index. Both
  // neighbors and the old crop remain available while the worker owns it.
  cache.mark_ready_for_refresh();
  const auto *old_crop = cache.find(4);
  const auto *previous_photo = cache.find(3);
  const auto *next_photo = cache.find(5);
  assert(cache.needs_refresh(3) && cache.needs_refresh(4) && cache.needs_refresh(5));
  auto redraw = cache.reserve(4, 3, 5, true);
  assert(redraw && redraw->replacement && redraw->pixels != old_crop);
  assert(cache.find(4) == old_crop && cache.find(3) && cache.find(5));
  assert(!cache.reserve(4, 3, 5, true));
  assert(cache.complete(*redraw, false) && cache.find(4) == old_crop);
  assert(cache.needs_refresh(4));
  auto new_crop = cache.reserve(4, 3, 5, true);
  assert(new_crop && !cache.complete(*redraw, true));
  *new_crop->pixels = 97;
  assert(cache.complete(*new_crop, true));
  assert(cache.find(4) == new_crop->pixels && *cache.find(4) == 97);
  assert(!cache.needs_refresh(4) && cache.needs_refresh(3) && cache.needs_refresh(5));
  assert(cache.find(3) == previous_photo && cache.find(5) == next_photo);

  // A thumbnail remains swipeable through a failed detail request and retry.
  SlideCache staged{buffers};
  const auto thumbnail = staged.reserve(8, 7, 9);
  assert(thumbnail && staged.complete(*thumbnail, true, false, false, true));
  const auto failed_detail = staged.reserve(8, 7, 9, true);
  assert(failed_detail && staged.has_thumbnail(8) && staged.find(8) == thumbnail->pixels);
  assert(staged.complete(*failed_detail, false) && staged.has_thumbnail(8));
  const auto full_detail = staged.reserve(8, 7, 9, true);
  assert(full_detail && !staged.complete(*failed_detail, true));
  assert(staged.find(8) == thumbnail->pixels);
  assert(staged.complete(*full_detail, true));
  assert(staged.find(8) == full_detail->pixels && !staged.has_thumbnail(8));

  const auto next_crop = cache.reserve(5, 3, 5, true);
  assert(next_crop && cache.complete(*next_crop, true));
  assert(cache.find(5) == next_crop->pixels && !cache.needs_refresh(5));
  cache.stop_refresh(3);
  assert(cache.find(3) == previous_photo && !cache.needs_refresh(3));

  // After retiring a feed, the current photo remains readable while its new
  // neighbor can reuse any released slot at the same logical index.
  const auto *anchor = cache.find(4);
  cache.mark_ready_for_refresh();
  cache.discard_other_ready_slides(4);
  assert(cache.find(4) == anchor && !cache.find(3) && !cache.find(5));
  assert(!cache.needs_refresh(4) && !cache.needs_refresh(5));
  const auto replacement = cache.reserve(5, 4, 9);
  assert(replacement && cache.complete(*replacement, true));
  assert(cache.find(4) == anchor && cache.find(5) == replacement->pixels);

  uint16_t roomy_pixels[SlideCache::capacity]{};
  for (size_t slot = 0; slot < buffers.size(); ++slot)
    buffers[slot] = &roomy_pixels[slot];
  SlideCache roomy{buffers};
  assert(roomy.size() == 9);
  for (int slide = 0; slide < 9; ++slide) {
    const auto lease = roomy.reserve(slide, 0, 8);
    assert(lease && roomy.complete(*lease, true));
  }
  assert(!roomy.reserve(9, 0, 8));
  roomy.mark_ready_for_refresh();
  const auto near_redraw = roomy.reserve(2, 1, 3, true);
  assert(near_redraw && !roomy.find(8) && roomy.find(0) && roomy.find(1) && roomy.find(2) && roomy.find(3));
  assert(roomy.complete(*near_redraw, true));
  const auto restore_distant = roomy.reserve(8, 0, 8);
  assert(restore_distant && roomy.complete(*restore_distant, true));
  assert(roomy.reserve(9, 1, 9) && !roomy.find(0) && roomy.find(8));

  SlideCache captions{buffers};
  const auto waiting = captions.reserve(10, 10, 12);
  const auto dated = captions.reserve(11, 10, 12);
  assert(waiting && dated && captions.complete(*dated, true));
  // Time can sync while a preview with the fallback date is still in flight.
  captions.mark_waiting_captions_for_refresh();
  assert(!captions.needs_refresh(10) && !captions.needs_refresh(11));
  assert(captions.complete(*waiting, true, true));
  captions.mark_waiting_captions_for_refresh();
  assert(captions.needs_refresh(10) && !captions.needs_refresh(11));
  assert(captions.find(10) == waiting->pixels && captions.find(11) == dated->pixels);
  captions.stop_refresh(10);
  captions.mark_waiting_captions_for_refresh();
  assert(!captions.needs_refresh(10));
  const auto old_source = captions.reserve(12, 10, 12);
  assert(old_source && captions.complete(*old_source, true, true, true));
  assert(captions.needs_caption_details(12));
  captions.discard_other_ready_slides(12);
  captions.mark_waiting_captions_for_refresh();
  assert(captions.find(12) == old_source->pixels && !captions.needs_refresh(12) && !captions.needs_caption_details(12));

  // Missing asset details do not prevent the uncaptioned slide from being
  // read. Failed and cancelled lookups leave the visible pixels untouched.
  const auto without_details = captions.reserve(13, 12, 13);
  assert(without_details && captions.complete(*without_details, true, false, true));
  assert(captions.find(13) == without_details->pixels && captions.needs_caption_details(13));
  const auto cancelled_details = captions.reserve(13, 12, 13, true);
  assert(cancelled_details && captions.complete(*cancelled_details, false));
  assert(captions.find(13) == without_details->pixels && captions.needs_caption_details(13));
  const auto new_caption = captions.reserve(13, 12, 13, true);
  assert(new_caption && captions.complete(*new_caption, true));
  assert(captions.find(13) == new_caption->pixels && !captions.needs_caption_details(13));
  const auto another = captions.reserve(14, 12, 14);
  assert(another && captions.complete(*another, true, false, true));
  captions.stop_caption_details(14);
  assert(captions.find(14) == another->pixels && !captions.needs_caption_details(14));

  // A worker reading the previous pixels can finish after a new crop or
  // feed replaces the photo. Its result then fails the current-source check.
  const auto zoom = cache.pin_ready(4);
  assert(zoom && cache.is_current(4, *zoom));
  const auto updated = cache.reserve(4, 4, 5, true);
  assert(updated && cache.complete(*updated, true));
  assert(!cache.is_current(4, *zoom) && cache.find(4) != zoom->pixels);
  cache.discard_other_ready_slides(4);
  for (int slide = 5; slide < 9; ++slide) {
    const auto work = cache.reserve(slide, 4, 9);
    assert(work && work->pixels != zoom->pixels && cache.complete(*work, true));
  }
  assert(!cache.reserve(9, 4, 9));
  assert(cache.release(*zoom));
  const auto reclaimed = cache.reserve(9, 4, 9);
  assert(reclaimed && reclaimed->pixels == zoom->pixels && cache.complete(*reclaimed, true));
  const auto next_zoom = cache.pin_ready(9);
  assert(next_zoom && !cache.release(*zoom) && cache.is_current(9, *next_zoom));
  assert(cache.release(*next_zoom));
}
