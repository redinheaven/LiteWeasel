#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include <WeaselUI.h>
#pragma comment(lib, "psapi.lib")

static std::set<std::wstring> Mods() {
  std::set<std::wstring> s; HMODULE m[2048]; DWORD n = 0;
  if (EnumProcessModules(GetCurrentProcess(), m, sizeof(m), &n))
    for (DWORD i = 0; i < n / sizeof(HMODULE); ++i) { wchar_t p[MAX_PATH]; if (GetModuleFileNameW(m[i], p, MAX_PATH)) s.insert(p); }
  return s;
}
static void Mem(const char* tag, PROCESS_MEMORY_COUNTERS_EX& c) {
  GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&c, sizeof(c));
  printf("%-8s WorkingSet=%7.2f MB  Private=%7.2f MB\n", tag, c.WorkingSetSize/1048576.0, c.PrivateUsage/1048576.0);
}
int main() {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  PROCESS_MEMORY_COUNTERS_EX a{}, b{};
  auto m0 = Mods(); Mem("before", a);
  LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
  HRESULT hr = weasel::PrewarmGraphics();
  QueryPerformanceCounter(&t1);
  Mem("after", b);
  printf("hr=0x%08lx  time=%.1f ms\n", (unsigned long)hr, (t1.QuadPart-t0.QuadPart)*1000.0/f.QuadPart);
  printf("delta    WorkingSet=%+7.2f MB  Private=%+7.2f MB\n", (b.WorkingSetSize-a.WorkingSetSize)/1048576.0, ((double)b.PrivateUsage-(double)a.PrivateUsage)/1048576.0);
  auto m1 = Mods(); printf("new modules: %zu\n", m1.size()-m0.size());
  for (auto& s : m1) if (!m0.count(s)) { auto p = s.substr(s.find_last_of(L'\\')+1); wprintf(L"  %s\n", p.c_str()); }
}
