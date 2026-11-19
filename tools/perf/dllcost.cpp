// Measures load cost (time, private bytes, working set) of DLLs in a fresh process.
// usage: dllcost.exe <mode>   mode=libs  : load dbghelp,dxgi,d3d11,dwrite,d2d1,dcomp one by one
//                             mode=weasel: load weaselx64.dll (pulls all static imports)
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#pragma comment(lib, "psapi.lib")

static void snap(SIZE_T& priv, SIZE_T& ws) {
  PROCESS_MEMORY_COUNTERS_EX m{};
  GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&m, sizeof(m));
  priv = m.PrivateUsage;
  ws = m.WorkingSetSize;
}

int wmain(int argc, wchar_t** argv) {
  const wchar_t* weasel = L"C:\\Program Files\\Rime\\LiteWeasel-0.17.4\\weaselx64.dll";
  const wchar_t* libs[] = {L"dbghelp.dll", L"dxgi.dll", L"d3d11.dll",
                           L"dwrite.dll",  L"d2d1.dll", L"dcomp.dll"};
  bool w = argc > 1 && wcscmp(argv[1], L"weasel") == 0;
  LARGE_INTEGER f, a, b;
  QueryPerformanceFrequency(&f);
  SIZE_T p0, w0, p1, w1;
  if (w) {
    snap(p0, w0);
    QueryPerformanceCounter(&a);
    HMODULE h = LoadLibraryW(weasel);
    QueryPerformanceCounter(&b);
    snap(p1, w1);
    printf("weasel,%.3f,%lld,%lld,%d\n", (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart,
           (long long)(p1 - p0) / 1024, (long long)(w1 - w0) / 1024, h ? 1 : 0);
    return 0;
  }
  for (auto l : libs) {
    snap(p0, w0);
    QueryPerformanceCounter(&a);
    HMODULE h = LoadLibraryW(l);
    QueryPerformanceCounter(&b);
    snap(p1, w1);
    printf("%ls,%.3f,%lld,%lld,%d\n", l, (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart,
           (long long)(p1 - p0) / 1024, (long long)(w1 - w0) / 1024, h ? 1 : 0);
  }
  return 0;
}
