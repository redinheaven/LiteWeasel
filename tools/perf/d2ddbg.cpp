#include <windows.h>
#include <d3d11.h>
#include <d2d1_2.h>
#include <d2d1_1.h>
#include <dwrite_2.h>
#include <wrl/client.h>
#include <psapi.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using Microsoft::WRL::ComPtr;
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

static double Ms(LONGLONG a, LONGLONG b) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (b - a) * 1000.0 / f.QuadPart; }
static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double Ws() { PROCESS_MEMORY_COUNTERS c{}; GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c)); return c.WorkingSetSize / 1048576.0; }
static bool HasMod(const wchar_t* n) { return GetModuleHandleW(n) != nullptr; }

int main(int argc, char** argv) {
  int level = argc > 1 ? atoi(argv[1]) : 0;  // 0 NONE, 1 INFORMATION
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ComPtr<ID3D11Device> d3d; ComPtr<IDXGIDevice> dxgi;
  static const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, _countof(fl), D3D11_SDK_VERSION, &d3d, nullptr, nullptr))) return 2;
  d3d.As(&dxgi);
  double ws0 = Ws();
  LONGLONG t0 = Now();
  D2D1_FACTORY_OPTIONS o = {level ? D2D1_DEBUG_LEVEL_INFORMATION : D2D1_DEBUG_LEVEL_NONE};
  ComPtr<ID2D1Factory2> fac; ComPtr<ID2D1Device1> dev; ComPtr<ID2D1DeviceContext> dc;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, o, fac.GetAddressOf()))) return 3;
  LONGLONG t1 = Now();
  if (FAILED(fac->CreateDevice(dxgi.Get(), &dev))) return 4;
  if (FAILED(dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc))) return 5;
  LONGLONG t2 = Now();
  ComPtr<ID2D1Bitmap1> bmp;
  D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (FAILED(dc->CreateBitmap(D2D1::SizeU(420, 220), nullptr, 0, &bp, &bmp))) return 6;
  dc->SetTarget(bmp.Get());
  ComPtr<IDWriteFactory2> dw; DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), (IUnknown**)dw.GetAddressOf());
  ComPtr<IDWriteTextFormat> tf; dw->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.f, L"zh-cn", &tf);
  ComPtr<ID2D1SolidColorBrush> br; dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &br);
  double ws1 = Ws();
  bool dbg = HasMod(L"D2D1Debug3.dll");
  const wchar_t* cands[] = {L"1 你好", L"2 中国", L"3 世界", L"4 我们", L"5 朋友", L"6 谢谢"};
  std::vector<double> fr;
  for (int i = 0; i < 400; ++i) {
    LONGLONG a = Now();
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(4, 4, 410, 210), 8, 8), br.Get());
    for (int k = 0; k < 6; ++k) {
      ComPtr<IDWriteTextLayout> tl; dw->CreateTextLayout(cands[k], (UINT32)wcslen(cands[k]), tf.Get(), 400.f, 30.f, &tl);
      DWRITE_TEXT_METRICS m; tl->GetMetrics(&m);
      dc->DrawTextLayout(D2D1::Point2F(10, 8.f + k * 32), tl.Get(), br.Get());
    }
    dc->EndDraw();
    fr.push_back(Ms(a, Now()));
  }
  std::sort(fr.begin() + 0, fr.end());
  printf("level=%d factory=%.2fms dev+dc=%.2fms ws_delta=%.2fMB debugdll=%d frame_p50=%.3fms p95=%.3fms\n",
         level, Ms(t0, t1), Ms(t1, t2), ws1 - ws0, dbg ? 1 : 0, fr[fr.size() / 2], fr[(size_t)(fr.size() * 0.95)]);
}
