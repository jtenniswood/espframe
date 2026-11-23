#pragma once

#include <string_view>

struct cJSON;

namespace espframe::native {

class NativeNetwork;

void start_native_release_updates(NativeNetwork &network);
cJSON *native_release_status();
bool request_native_release(std::string_view version);

}  // namespace espframe::native
