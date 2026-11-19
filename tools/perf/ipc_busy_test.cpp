#include <windows.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstdio>
#include <WeaselIPC.h>
#include <WeaselPerf.h>
// WeaselIPC.lib reports timings through this provider; the real one lives in WeaselUI.lib.
TRACELOGGING_DEFINE_PROVIDER(g_hWeaselPerf, "LiteWeasel-Perf", (0x5974258d, 0x005e, 0x4490, 0xad, 0xef, 0x9e, 0x26, 0x5b, 0x26, 0xab, 0xf5));


static double Ms(LONGLONG a, LONGLONG b) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (b - a) * 1000.0 / f.QuadPart; }
static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// Creates the Weasel pipe with a single instance and occupies it with one
// connection, so every further client open fails with ERROR_PIPE_BUSY.
int main() {
  std::wstring name = weasel::GetPipeName();
  HANDLE srv = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                1, 4096, 4096, 0, nullptr);
  if (srv == INVALID_HANDLE_VALUE) { printf("CreateNamedPipe failed %lu\n", GetLastError()); return 2; }
  HANDLE occupy = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (occupy == INVALID_HANDLE_VALUE) { printf("first connect failed %lu\n", GetLastError()); return 3; }
  HANDLE probe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  printf("second raw CreateFile: handle=%p error=%lu (231 = ERROR_PIPE_BUSY)\n", probe, probe == INVALID_HANDLE_VALUE ? GetLastError() : 0);
  if (probe != INVALID_HANDLE_VALUE) { printf("pipe is not busy; test invalid\n"); return 4; }

  weasel::Client c;
  LONGLONG t0 = Now();
  bool ok = c.Connect(nullptr);
  printf("Client::Connect         -> %d after %.1f ms\n", ok, Ms(t0, Now()));
  t0 = Now();
  c.StartSession();
  printf("Client::StartSession    -> (void) after %.1f ms (goes through Transact)\n", Ms(t0, Now()));
  t0 = Now();
  bool echo = c.Echo();
  printf("Client::Echo            -> %d after %.1f ms\n", echo, Ms(t0, Now()));
  t0 = Now();
  weasel::KeyEvent ke('n', 0);
  bool eaten = c.ProcessKeyEvent(ke);
  printf("Client::ProcessKeyEvent -> %d after %.1f ms\n", eaten, Ms(t0, Now()));
  return 0;
}