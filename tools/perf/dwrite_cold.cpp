// Measures DirectWrite font-path cost in a fresh process, mirroring WeaselUI/d2d.cpp:
//   DWriteCreateFactory -> CreateTextFormat("_InvalidFontName_") -> SetFontFallback(builder)
//   -> CreateTextLayout + GetMetrics (this is where real fonts get resolved/loaded).
// Step 1 is the "first composition in this process"; step 2 repeats the same work, which is
// what every later composition pays because D2D (and its textFormatCache) is rebuilt per panel.
// Usage: dwrite_cold.exe [font_face=Microsoft YaHei] [point=14]
// Output (CSV, ms): factory,fmt1,fallback1,layout1,fmt2,fallback2,layout2
#include <windows.h>
#include <dwrite_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#pragma comment(lib, "dwrite.lib")
using Microsoft::WRL::ComPtr;

static double Ms(LONGLONG a, LONGLONG b) {
  static LONGLONG f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x.QuadPart; }();
  return (b - a) * 1000.0 / f;
}
static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

struct Steps { double fmt, fb, layout; };

static bool Round(IDWriteFactory2* f, const wchar_t* face, float pt, Steps& s) {
  LONGLONG a = Now();
  ComPtr<IDWriteTextFormat> base;
  if (FAILED(f->CreateTextFormat(L"_InvalidFontName_", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, pt, L"",
                                 base.GetAddressOf())))
    return false;
  ComPtr<IDWriteTextFormat1> fmt;
  base.As(&fmt);
  LONGLONG b = Now();
  ComPtr<IDWriteFontFallback> sys;
  ComPtr<IDWriteFontFallbackBuilder> bld;
  f->GetSystemFontFallback(sys.GetAddressOf());
  f->CreateFontFallbackBuilder(bld.GetAddressOf());
  DWRITE_UNICODE_RANGE range = {0, 0x10ffff};
  const WCHAR* fam = face;
  bld->AddMapping(&range, 1, &fam, 1);
  bld->AddMappings(sys.Get());
  ComPtr<IDWriteFontFallback> fb;
  bld->CreateFontFallback(fb.GetAddressOf());
  fmt->SetFontFallback(fb.Get());
  LONGLONG c = Now();
  const wchar_t* text = L"ni hao shi jie\x4f60\x597d\x4e16\x754c 1.\x4f60\x597d 2.\x62df\x597d 3.\x59ae\x597d";
  ComPtr<IDWriteTextLayout> lay;
  if (FAILED(f->CreateTextLayout(text, (UINT32)wcslen(text), fmt.Get(), 1000, 1000,
                                 lay.GetAddressOf())))
    return false;
  DWRITE_TEXT_METRICS m;
  lay->GetMetrics(&m);
  LONGLONG d = Now();
  s = {Ms(a, b), Ms(b, c), Ms(c, d)};
  return true;
}

int wmain(int argc, wchar_t** argv) {
  const wchar_t* face = argc > 1 ? argv[1] : L"Microsoft YaHei";
  float pt = argc > 2 ? (float)_wtof(argv[2]) : 14.0f;
  LONGLONG t0 = Now();
  ComPtr<IDWriteFactory2> f;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
                                 reinterpret_cast<IUnknown**>(f.GetAddressOf()))))
    return 1;
  double fac = Ms(t0, Now());
  Steps s1{}, s2{};
  if (!Round(f.Get(), face, pt, s1) || !Round(f.Get(), face, pt, s2)) return 2;
  printf("%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", fac, s1.fmt, s1.fb, s1.layout, s2.fmt, s2.fb,
         s2.layout);
  return 0;
}
