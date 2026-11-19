// Fault-injection test for the client side of the Weasel pipe protocol.
// Runs a fake server on the real pipe name (stop WeaselServer first!) whose
// response delay per message type can be changed, and drives weasel::Client.
#include <windows.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>
#include <cstdio>
#include <functional>
#include <WeaselIPC.h>
#include <WeaselPerf.h>
// WeaselIPC.lib reports timings through this provider; the real one lives in WeaselUI.lib.
TRACELOGGING_DEFINE_PROVIDER(g_hWeaselPerf, "LiteWeasel-Perf", (0x5974258d, 0x005e, 0x4490, 0xad, 0xef, 0x9e, 0x26, 0x5b, 0x26, 0xab, 0xf5));


static double Ms(LONGLONG a, LONGLONG b) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (b - a) * 1000.0 / f.QuadPart; }
static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

static std::atomic<int> g_key_delay{0}, g_echo_delay{0}, g_maint_delay{0};
static std::atomic<int> g_key_counter{0};
static std::atomic<bool> g_stop{false};
static const wchar_t* kBody = L"commit=k\n";

static void Reply(HANDLE pipe, DWORD result, bool body, int tag) {
  char out[512]; memcpy(out, &result, 4); size_t n = 4;
  if (body) {
    wchar_t text[64]; swprintf(text, 64, L"commit=k%d\n", tag);
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    memcpy(out + 4, text, bytes); n += bytes;
  }
  DWORD w = 0; WriteFile(pipe, out, (DWORD)n, &w, nullptr);
}

static void Serve(HANDLE pipe) {
  static char buf[64 * 1024];  // one handler at a time is enough for this test
  char local[64 * 1024];
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(pipe, local, sizeof(local), &got, nullptr) && GetLastError() != ERROR_MORE_DATA) break;
    if (got < sizeof(weasel::PipeMessage)) break;
    weasel::PipeMessage m; memcpy(&m, local, sizeof(m));
    switch (m.Msg) {
      case WEASEL_IPC_START_SESSION: Reply(pipe, 7, true, 0); break;
      case WEASEL_IPC_ECHO: if (g_echo_delay) Sleep(g_echo_delay); Reply(pipe, m.lParam, false, 0); break;
      case WEASEL_IPC_PROCESS_KEY_EVENT: { int tag = ++g_key_counter; if (g_key_delay) Sleep(g_key_delay); Reply(pipe, 1, true, tag); break; }
      case WEASEL_IPC_START_MAINTENANCE: if (g_maint_delay) Sleep(g_maint_delay); Reply(pipe, 0, false, 0); break;
      default: Reply(pipe, 0, false, 0); break;
    }
  }
  DisconnectNamedPipe(pipe); CloseHandle(pipe);
}

static void Accept() {
  std::wstring name = weasel::GetPipeName();
  while (!g_stop) {
    HANDLE p = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);
    if (p == INVALID_HANDLE_VALUE) break;
    if (ConnectNamedPipe(p, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) std::thread(Serve, p).detach();
    else CloseHandle(p);
  }
}

static int g_fail = 0;
static void Check(bool ok, const char* what, double ms) { printf("  [%s] %-58s %7.1f ms\n", ok ? "PASS" : "FAIL", what, ms); if (!ok) ++g_fail; }

int main() {
  std::thread(Accept).detach();
  Sleep(200);
  weasel::Client c;
  if (!c.Connect(nullptr)) { printf("cannot connect to fake server\n"); return 2; }
  c.StartSession();
  std::wstring got; auto grab = [&](LPWSTR s, UINT) { got = s; return true; };
  LONGLONG t; bool r;

  printf("1. normal operation\n");
  t = Now(); r = c.Echo();                 Check(r, "Echo returns true", Ms(t, Now()));
  t = Now(); r = c.ProcessKeyEvent(weasel::KeyEvent('n', 0)); Check(r, "ProcessKeyEvent returns true", Ms(t, Now()));
  got.clear(); r = c.GetResponseData(grab); Check(r && got.find(L"commit=k") == 0, "GetResponseData returns the body", 0);

  printf("2. server stalls on a key event (1500 ms) -> client must give up after ~250 ms\n");
  g_key_delay = 1500;
  t = Now(); r = c.ProcessKeyEvent(weasel::KeyEvent('i', 0)); double ms = Ms(t, Now());
  Check(!r && ms >= 200 && ms < 450, "ProcessKeyEvent times out in 200..450 ms", ms);
  got = L"STALE"; r = c.GetResponseData(grab); Check(!r && got == L"STALE", "GetResponseData is refused after the timeout", 0);
  g_key_delay = 0;

  printf("3. recovery on the very next call\n");
  t = Now(); r = c.Echo();                 Check(r, "Echo works again (new connection, same session)", Ms(t, Now()));

  printf("4. the late reply of the timed-out request must not leak into the next one\n");
  Sleep(1800);
  t = Now(); r = c.ProcessKeyEvent(weasel::KeyEvent('h', 0));
  got.clear(); bool g = c.GetResponseData(grab);
  Check(r && g && got.find(L"commit=k") == 0 && got != L"commit=k2\n", "next response is its own (not the late one)", Ms(t, Now()));
  printf("     body received: %ls", got.c_str());

  printf("5. non-input-method messages must not time out\n");
  g_maint_delay = 700;
  t = Now(); c.StartMaintenance(); ms = Ms(t, Now());
  Check(ms >= 650 && ms < 1500, "StartMaintenance waits for the full 700 ms", ms);
  g_maint_delay = 0;

  printf("6. echo stalls (1000 ms) -> bounded as well\n");
  c.StartSession();  // StartMaintenance() resets the session id, as it always did
  g_echo_delay = 1000;
  t = Now(); r = c.Echo(); ms = Ms(t, Now());
  Check(!r && ms >= 200 && ms < 450, "Echo times out in 200..450 ms", ms);
  g_echo_delay = 0;
  Sleep(1200);
  t = Now(); r = c.Echo(); Check(r, "Echo works again afterwards", Ms(t, Now()));

  printf("7. circuit breaker: two timeouts open it, requests are refused at once, a probe closes it\n");
  g_echo_delay = 5000;
  t = Now(); r = c.Echo(); ms = Ms(t, Now()); Check(!r && ms >= 200 && ms < 450, "1st Echo times out (~250 ms)", ms);
  Check(!c.Suspended(), "breaker still closed after one failure", 0);
  t = Now(); r = c.Echo(); ms = Ms(t, Now()); Check(!r && ms >= 200 && ms < 450, "2nd Echo times out (~250 ms)", ms);
  Check(c.Suspended(), "breaker is open after two consecutive timeouts", 0);
  t = Now(); r = c.Echo(); ms = Ms(t, Now()); Check(!r && ms < 20, "Echo is refused immediately while open", ms);
  t = Now(); r = c.ProcessKeyEvent(weasel::KeyEvent('x', 0)); ms = Ms(t, Now()); Check(!r && ms < 20, "ProcessKeyEvent is refused immediately while open", ms);
  Sleep(3300);
  Check(!c.Suspended(), "open period (3 s) is over: next call is the probe", 0);
  t = Now(); r = c.Echo(); ms = Ms(t, Now()); Check(!r && ms >= 70 && ms < 220, "probe uses the short ~100 ms timeout", ms);
  Check(c.Suspended(), "failed probe re-opens the breaker", 0);
  Sleep(200);
  t = Now(); r = c.Echo(); ms = Ms(t, Now()); Check(!r && ms < 20, "still refused during the doubled (6 s) open period", ms);
  g_echo_delay = 0;
  Sleep(6200);
  t = Now(); r = c.Echo(); ms = Ms(t, Now()); Check(r && ms < 100, "probe succeeds once the server answers: breaker closes", ms);
  Check(!c.Suspended(), "breaker is closed again", 0);
  t = Now(); r = c.ProcessKeyEvent(weasel::KeyEvent('y', 0)); ms = Ms(t, Now()); Check(r && ms < 100, "normal operation resumed", ms);

  printf("8. messages without a timeout are not guarded by the breaker\n");
  g_echo_delay = 5000;
  c.Echo(); c.Echo();
  Check(c.Suspended(), "breaker open again", 0);
  g_maint_delay = 400;
  t = Now(); c.StartMaintenance(); ms = Ms(t, Now());
  Check(ms >= 350 && ms < 1500, "StartMaintenance still goes through and waits 400 ms", ms);
  g_maint_delay = 0; g_echo_delay = 0;
  printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
  g_stop = true;
  return g_fail ? 1 : 0;
}