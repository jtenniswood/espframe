#pragma once

namespace espframe::native {

#if NATIVE_MEMORY_TRACE
void trace_memory(const char *stage);
#else
inline void trace_memory(const char *) {}
#endif

class MemoryTraceScope {
 public:
  MemoryTraceScope(const char *start, const char *end) : end_(end) { trace_memory(start); }
  ~MemoryTraceScope() { trace_memory(end_); }

 private:
  const char *end_;
};

}  // namespace espframe::native
