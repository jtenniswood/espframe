#pragma once

#include "immich_selection.h"

namespace espframe::native {

// The fetch buffer belongs to the photo worker. Decode only individual assets
// so a large Memory never requires a second full JSON document in RAM.
std::optional<std::vector<ImmichPhoto>> parse_memories(std::string_view json, size_t limit,
                                                     PhotoOrientation orientation = PhotoOrientation::Any);

}  // namespace espframe::native
