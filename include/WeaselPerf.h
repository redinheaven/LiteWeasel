#pragma once
// LiteWeasel: key-to-screen latency tracing (ETW / TraceLogging).
//
// Disabled by default: events are only produced while an ETW session is
// listening to the provider, so the cost when nobody listens is one inlined
// check per instrumentation point. No file I/O, no allocation, no exceptions,
// so it is safe to use inside host processes (WeaselTSF / WeaselUI).
//
// Provider name : LiteWeasel-Perf
// Provider GUID : {5974258d-005e-4490-adef-9e265b26abf5}
// Event         : "Stage" { stage: string, us: int64 duration in microseconds }
// pid / tid / timestamp come from the ETW event header. Timestamps are QPC
// based and comparable across processes. Key codes are deliberately NOT
// logged.
//
// The provider is defined (and registered) once in WeaselUI/WeaselUI.cpp,
// which is linked into weasel.dll, WeaselServer.exe and WeaselDeployer.exe.
#include <windows.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>

TRACELOGGING_DECLARE_PROVIDER(g_hWeaselPerf);

namespace weasel {
namespace perf {

inline bool Enabled() {
  return TraceLoggingProviderEnabled(g_hWeaselPerf, 0, 0) != 0;
}

inline LONGLONG Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

// Writes one "Stage" event whose duration is (now - t0).
inline void Emit(const char* stage, LONGLONG t0) {
  static const LONGLONG freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart ? f.QuadPart : 1;
  }();
  const INT64 us = (Now() - t0) * 1000000 / freq;
  TraceLoggingWrite(g_hWeaselPerf, "Stage", TraceLoggingString(stage, "stage"),
                    TraceLoggingInt64(us, "us"));
}

// Writes a "Stage" event carrying a value instead of a measured duration (for
// state changes such as the IPC circuit breaker; the value is in microseconds
// to keep the unit of the "us" field).
inline void EmitValue(const char* stage, INT64 us) {
  TraceLoggingWrite(g_hWeaselPerf, "Stage", TraceLoggingString(stage, "stage"),
                    TraceLoggingInt64(us, "us"));
}

// Sequential laps inside one function: take a mark, then call Lap() after each
// step. Lap() emits (now - mark) under the given name and restarts the mark.
// When tracing is disabled the mark is 0 and Lap() does nothing.
inline LONGLONG Mark() {
  return Enabled() ? Now() : 0;
}
inline void Lap(const char* stage, LONGLONG& mark) {
  if (mark) {
    Emit(stage, mark);
    mark = Now();
  }
}

// Measures the time between construction and destruction.
class Scope {
 public:
  explicit Scope(const char* stage)
      : stage_(stage), t0_(Enabled() ? Now() : 0) {}
  ~Scope() {
    if (t0_)
      Emit(stage_, t0_);
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const char* stage_;
  LONGLONG t0_;
};

// QPC taken when an edit session is requested (per thread); consumed by
// DoEditSession to measure how long TSF made us wait.
inline thread_local LONGLONG g_editRequestQpc = 0;

}  // namespace perf
}  // namespace weasel

#define WEASEL_PERF_CONCAT2(a, b) a##b
#define WEASEL_PERF_CONCAT(a, b) WEASEL_PERF_CONCAT2(a, b)
#define WEASEL_PERF_SCOPE(stage) \
  ::weasel::perf::Scope WEASEL_PERF_CONCAT(weasel_perf_scope_, __LINE__)(stage)
