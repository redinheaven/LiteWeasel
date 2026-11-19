// Automated offscreen WARP + DirectWrite benchmark for WeaselUI hot paths:
//   1. Shadow / GaussianBlur cost (FillGeometry to_blur=true) vs no shadow
//   2. GetTextSize cost: current double-CreateTextLayout vs SetMaxWidth/Height reuse vs cached static items
// Also verifies pixel-exact width/height equivalence between old and new GetTextSize.
#include <windows.h>
#include <d3d11.h>
#include <d2d1_2.h>
#include <d2d1effects.h>
#include <dwrite_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxguid.lib")

static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double Us(LONGLONG a, LONGLONG b) {
  static LONGLONG freq = []() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
  return (b - a) * 1e6 / freq;
}
static double Pct(std::vector<double> v, double p) {
  std::sort(v.begin(), v.end());
  return v[static_cast<size_t>((v.size() - 1) * p)];
}

static SIZE GetTextSizeOld(IDWriteFactory2* wf, const std::wstring& text, IDWriteTextFormat* fmt, int maxW) {
  SIZE out{0, 0};
  ComPtr<IDWriteTextLayout> tl;
  wf->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, (float)maxW, 0.0f, &tl);
  DWRITE_TEXT_METRICS m{};
  tl->GetMetrics(&m);
  out.cx = (LONG)ceil(m.widthIncludingTrailingWhitespace);
  out.cy = (LONG)ceil(m.height);
  float mw = maxW == 0 ? m.widthIncludingTrailingWhitespace : (float)maxW;
  wf->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, mw, m.height, tl.ReleaseAndGetAddressOf());
  DWRITE_OVERHANG_METRICS ov{};
  tl->GetOverhangMetrics(&ov);
  if (ov.left > 0) out.cx += (LONG)(ov.left + 1);
  if (ov.right > 0) out.cx += (LONG)(ov.right + 1);
  if (ov.top > 0) out.cy += (LONG)(ov.top + 1);
  if (ov.bottom > 0) out.cy += (LONG)(ov.bottom + 1);
  return out;
}

static SIZE GetTextSizeReuse(IDWriteFactory2* wf, const std::wstring& text, IDWriteTextFormat* fmt, int maxW) {
  SIZE out{0, 0};
  ComPtr<IDWriteTextLayout> tl;
  wf->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, (float)maxW, 0.0f, &tl);
  DWRITE_TEXT_METRICS m{};
  tl->GetMetrics(&m);
  out.cx = (LONG)ceil(m.widthIncludingTrailingWhitespace);
  out.cy = (LONG)ceil(m.height);
  float mw = maxW == 0 ? m.widthIncludingTrailingWhitespace : (float)maxW;
  tl->SetMaxWidth(mw);
  tl->SetMaxHeight(m.height);
  DWRITE_OVERHANG_METRICS ov{};
  tl->GetOverhangMetrics(&ov);
  if (ov.left > 0) out.cx += (LONG)(ov.left + 1);
  if (ov.right > 0) out.cx += (LONG)(ov.right + 1);
  if (ov.top > 0) out.cy += (LONG)(ov.top + 1);
  if (ov.bottom > 0) out.cy += (LONG)(ov.bottom + 1);
  return out;
}

int main() {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ComPtr<ID3D11Device> d3d;
  ComPtr<IDXGIDevice> dxgi;
  static const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, _countof(fl), D3D11_SDK_VERSION, &d3d, nullptr, nullptr)))
    return 2;
  d3d.As(&dxgi);

  D2D1_FACTORY_OPTIONS opt{D2D1_DEBUG_LEVEL_NONE};
  ComPtr<ID2D1Factory2> fac;
  ComPtr<ID2D1Device1> dev;
  ComPtr<ID2D1DeviceContext> dc;
  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, opt, fac.GetAddressOf());
  fac->CreateDevice(dxgi.Get(), &dev);
  dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);

  // Typical 7-candidate horizontal panel at 150% DPI (~560x110)
  ComPtr<ID2D1Bitmap1> target;
  D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  dc->CreateBitmap(D2D1::SizeU(560, 110), nullptr, 0, &bp, &target);
  dc->SetTarget(target.Get());

  ComPtr<ID2D1SolidColorBrush> br;
  dc->CreateSolidColorBrush(D2D1::ColorF(0x202020, 1.0f), &br);

  ComPtr<IDWriteFactory2> wf;
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), (IUnknown**)wf.GetAddressOf());
  ComPtr<IDWriteTextFormat> tfBase;
  wf->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                       DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"zh-cn", &tfBase);
  tfBase->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

  // --- Part 1: Verify equivalence of GetTextSizeOld vs GetTextSizeReuse ---
  const std::vector<std::wstring> samples = {
      L"", L"<", L">", L"1.", L"2.", L"3.", L"4.", L"5.", L"6.", L"7.",
      L"ni hao", L"你好", L"你号", L"拟好", L"尼豪", L"泥嚎", L"逆昊", L"Hello World!",
      L"中华人民共和国", L"a", L"gjpqy", L"　"
  };
  int mismatches = 0;
  for (int maxW : {0, 200, 500}) {
    tfBase->SetWordWrapping(maxW == 0 ? DWRITE_WORD_WRAPPING_NO_WRAP : DWRITE_WORD_WRAPPING_WHOLE_WORD);
    for (const auto& s : samples) {
      SIZE a = GetTextSizeOld(wf.Get(), s, tfBase.Get(), maxW);
      SIZE b = GetTextSizeReuse(wf.Get(), s, tfBase.Get(), maxW);
      if (a.cx != b.cx || a.cy != b.cy) {
        printf("MISMATCH maxW=%d len=%zu old=(%ld,%ld) new=(%ld,%ld)\n", maxW, s.size(), a.cx, a.cy, b.cx, b.cy);
        ++mismatches;
      }
    }
  }
  tfBase->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  printf("=== Part 1: GetTextSize equivalence: %d mismatches across %zu cases ===\n",
         mismatches, samples.size() * 3);

  // --- Part 2: Benchmark RecalculateSizes (1 preedit + < + > + 7 labels + 7 cands + 7 empty comments) ---
  const std::vector<std::wstring> labels = {L"1.", L"2.", L"3.", L"4.", L"5.", L"6.", L"7."};
  const std::vector<std::wstring> cands = {L"你好", L"你号", L"拟好", L"尼豪", L"泥嚎", L"逆昊", L"倪浩"};
  const std::wstring preedit = L"ni hao";
  const std::wstring empty = L"";
  const int kIters = 200;

  std::vector<double> t_old, t_reuse, t_reuse_cached;
  for (int i = 0; i < kIters; ++i) {
    LONGLONG t0 = Now();
    GetTextSizeOld(wf.Get(), preedit, tfBase.Get(), 0);
    GetTextSizeOld(wf.Get(), L"<", tfBase.Get(), 0);
    GetTextSizeOld(wf.Get(), L">", tfBase.Get(), 0);
    for (int k = 0; k < 7; ++k) {
      GetTextSizeOld(wf.Get(), labels[k], tfBase.Get(), 0);
      GetTextSizeOld(wf.Get(), cands[k], tfBase.Get(), 0);
      GetTextSizeOld(wf.Get(), empty, tfBase.Get(), 0);
    }
    t_old.push_back(Us(t0, Now()));
  }

  for (int i = 0; i < kIters; ++i) {
    LONGLONG t0 = Now();
    GetTextSizeReuse(wf.Get(), preedit, tfBase.Get(), 0);
    GetTextSizeReuse(wf.Get(), L"<", tfBase.Get(), 0);
    GetTextSizeReuse(wf.Get(), L">", tfBase.Get(), 0);
    for (int k = 0; k < 7; ++k) {
      GetTextSizeReuse(wf.Get(), labels[k], tfBase.Get(), 0);
      GetTextSizeReuse(wf.Get(), cands[k], tfBase.Get(), 0);
      GetTextSizeReuse(wf.Get(), empty, tfBase.Get(), 0);
    }
    t_reuse.push_back(Us(t0, Now()));
  }

  std::unordered_map<std::wstring, SIZE> cache;
  auto cachedSize = [&](const std::wstring& s) -> SIZE {
    auto it = cache.find(s);
    if (it != cache.end()) return it->second;
    SIZE sz = GetTextSizeReuse(wf.Get(), s, tfBase.Get(), 0);
    cache[s] = sz;
    return sz;
  };
  for (int i = 0; i < kIters; ++i) {
    LONGLONG t0 = Now();
    GetTextSizeReuse(wf.Get(), preedit, tfBase.Get(), 0);
    cachedSize(L"<");
    cachedSize(L">");
    for (int k = 0; k < 7; ++k) {
      cachedSize(labels[k]);
      GetTextSizeReuse(wf.Get(), cands[k], tfBase.Get(), 0);
      cachedSize(empty);
    }
    t_reuse_cached.push_back(Us(t0, Now()));
  }

  printf("=== Part 2: Layout text measurement (7 cands, %d iters) ===\n", kIters);
  printf("  old (double CreateTextLayout):     p50=%6.1f us  p95=%6.1f us\n", Pct(t_old, 0.5), Pct(t_old, 0.95));
  printf("  reuse (SetMaxWidth/Height):        p50=%6.1f us  p95=%6.1f us\n", Pct(t_reuse, 0.5), Pct(t_reuse, 0.95));
  printf("  reuse + static/empty cache:        p50=%6.1f us  p95=%6.1f us\n", Pct(t_reuse_cached, 0.5), Pct(t_reuse_cached, 0.95));

  // --- Part 3: Benchmark WARP shadow/GaussianBlur cost ---
  printf("=== Part 3: WARP frame paint with GaussianBlur shadow (560x110, 100 frames) ===\n");
  for (float radius : {0.0f, 4.0f, 8.0f, 12.0f}) {
    std::vector<double> fr;
    for (int i = 0; i < 100; ++i) {
      LONGLONG t0 = Now();
      dc->BeginDraw();
      dc->Clear(D2D1::ColorF(0, 0, 0, 0));
      D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(16, 16, 544, 94), 8, 8);
      if (radius > 0.0f) {
        ComPtr<ID2D1BitmapRenderTarget> brt;
        dc->CreateCompatibleRenderTarget(&brt);
        brt->BeginDraw();
        brt->Clear(D2D1::ColorF(0, 0, 0, 0));
        brt->FillRoundedRectangle(rr, br.Get());
        brt->EndDraw();
        ComPtr<ID2D1Bitmap> sbmp;
        brt->GetBitmap(&sbmp);
        ComPtr<ID2D1Effect> blur;
        dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur);
        blur->SetInput(0, sbmp.Get());
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, radius);
        dc->DrawImage(blur.Get());
      }
      dc->FillRoundedRectangle(rr, br.Get());
      for (int k = 0; k < 7; ++k) {
        ComPtr<IDWriteTextLayout> tl;
        wf->CreateTextLayout(cands[k].c_str(), (UINT32)cands[k].size(), tfBase.Get(), 80.0f, 30.0f, &tl);
        dc->DrawTextLayout(D2D1::Point2F(24.0f + k * 72.0f, 48.0f), tl.Get(), br.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
      }
      dc->EndDraw();
      fr.push_back(Us(t0, Now()));
    }
    printf("  shadow_radius=%4.1f:  p50=%7.1f us (%5.2f ms)  p95=%7.1f us\n",
           radius, Pct(fr, 0.5), Pct(fr, 0.5) / 1000.0, Pct(fr, 0.95));
  }
  return 0;
}
