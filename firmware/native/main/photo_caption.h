#pragma once

#include "photo_metadata.h"
#include "slide_geometry.h"

#include <ctime>
#include <span>
#include <string>

namespace espframe::native {

std::string photo_caption_date(const PhotoMetadata &metadata, const PhotoMetadataSettings &settings,
                               const std::optional<std::tm> &local);
void paint_photo_caption(uint16_t *frame, std::span<const uint8_t> locations, std::span<const uint8_t> dates,
                         const PhotoMetadata &metadata,
                         const PhotoMetadataSettings &settings, ScreenRotation rotation, bool show_clock,
                         const std::optional<std::tm> &local = {});

}  // namespace espframe::native
