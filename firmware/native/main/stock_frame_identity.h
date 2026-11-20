#pragma once

#include <optional>
#include <string>

namespace espframe::native {

// The caller has initialized the verified stock NVS partition. This opens
// only the stock identity namespace and never changes its contents.
std::optional<std::string> read_stock_frame_name(const char *partition);

}  // namespace espframe::native
