// Automated benchmark for real WeaselUI HorizontalLayout::DoLayout().
// Measures realistic 5-key Chinese typing sequences (7 candidates/page) and prints
// a layout geometry checksum so before/after optimizations prove pixel-exact output.
#include <windows.h>
#include <atlbase.h>
#include <atltypes.h>
#include <WeaselUI.h>
#include <WeaselUtility.h>
#include <d2d.h>
#include <HorizontalLayout.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double Us(LONGLONG a, LONGLONG b) {
  static LONGLONG freq = []() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
  return (b - a) * 1e6 / freq;
}
static double Pct(std::vector<double> v, double p) {
  std::sort(v.begin(), v.end());
  return v[static_cast<size_t>((v.size() - 1) * p)];
}

static weasel::Context MakeStep(const std::wstring& preedit,
                                const std::vector<std::wstring>& cands) {
  weasel::Context ctx;
  ctx.preedit.str = preedit;
  weasel::TextAttribute attr;
  attr.type = weasel::HIGHLIGHTED;
  attr.range.start = 0;
  attr.range.end = (int)preedit.size();
  ctx.preedit.attributes.push_back(attr);
  ctx.cinfo.candies.resize(cands.size());
  ctx.cinfo.labels.resize(cands.size());
  ctx.cinfo.comments.resize(cands.size());
  for (size_t i = 0; i < cands.size(); ++i) {
    ctx.cinfo.candies[i].str = cands[i];
    ctx.cinfo.labels[i].str = std::to_wstring(i + 1);
  }
  ctx.cinfo.highlighted = 0;
  ctx.cinfo.currentPage = 0;
  ctx.cinfo.is_last_page = false;
  return ctx;
}

int main(int argc, char** argv) {
  FILE* out = argc > 1 ? fopen(argv[1], "w") : stdout;
  if (!out) out = stdout;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  weasel::UIStyle style;
  style.font_face = L"Microsoft YaHei, Segoe UI Emoji";
  style.label_font_face = L"Microsoft YaHei";
  style.comment_font_face = L"Microsoft YaHei";
  style.font_point = 13;
  style.label_font_point = 12;
  style.comment_font_point = 12;
  style.label_text_format = L"%s.";
  style.layout_type = weasel::UIStyle::LAYOUT_HORIZONTAL;
  style.inline_preedit = false;
  style.shadow_radius = 0;

  auto d2d = std::make_shared<weasel::D2D>(style);
  d2d->InitDirectWriteResources();
  if (!d2d->pTextFormat) {
    fprintf(out, "ERROR: pTextFormat is null (need interactive session for DeviceResources)\n");
    if (out != stdout) fclose(out);
    return 2;
  }

  weasel::Status status;
  status.composing = true;
  status.ascii_mode = false;

  std::vector<weasel::Context> steps = {
      MakeStep(L"n", {L"你", L"年", L"那", L"能", L"内", L"南", L"女"}),
      MakeStep(L"ni", {L"你", L"拟", L"泥", L"逆", L"尼", L"妮", L"匿"}),
      MakeStep(L"ni h", {L"你", L"你好", L"你会", L"你和", L"你很", L"你还", L"你话"}),
      MakeStep(L"ni ha", {L"你哈", L"你喊", L"你汉", L"你含", L"你汗", L"你寒", L"你韩"}),
      MakeStep(L"ni hao", {L"你好", L"你号", L"拟好", L"尼豪", L"泥嚎", L"逆昊", L"倪浩"}),
  };

  // Warm up 1 pass
  for (auto& ctx : steps) {
    weasel::HorizontalLayout lay(style, ctx, status, d2d);
    lay.DoLayout();
  }

  std::vector<double> times;
  long long checksum = 0;
  const int kRounds = 60;  // 300 layouts (realistic repeated prefixes/words)
  for (int r = 0; r < kRounds; ++r) {
    for (auto& ctx : steps) {
      LONGLONG t0 = Now();
      weasel::HorizontalLayout lay(style, ctx, status, d2d);
      lay.DoLayout();
      times.push_back(Us(t0, Now()));
      CSize sz = lay.GetContentSize();
      checksum += sz.cx * 10007LL + sz.cy * 31LL;
      for (int i = 0; i < 7; ++i) {
        CRect rc = lay.GetCandidateRect(i);
        checksum += rc.left + rc.top + rc.right + rc.bottom;
      }
    }
  }

  // Worst-case pass: brand-new unique candidate strings on every single layout
  // (only static labels / < / > / | / empty comments can hit any text-size cache).
  std::vector<double> fresh_times;
  long long fresh_checksum = 0;
  for (int r = 0; r < 50; ++r) {
    std::vector<std::wstring> uniq;
    uniq.reserve(7);
    for (int i = 0; i < 7; ++i) {
      wchar_t c1 = static_cast<wchar_t>(0x4E00 + ((r * 7 + i) * 37) % 3000);
      wchar_t c2 = static_cast<wchar_t>(0x4E00 + ((r * 7 + i) * 97 + 13) % 3000);
      uniq.push_back(std::wstring{c1, c2});
    }
    weasel::Context ctx = MakeStep(L"pin yin " + std::to_wstring(r), uniq);
    LONGLONG t0 = Now();
    weasel::HorizontalLayout lay(style, ctx, status, d2d);
    lay.DoLayout();
    fresh_times.push_back(Us(t0, Now()));
    CSize sz = lay.GetContentSize();
    fresh_checksum += sz.cx * 10007LL + sz.cy * 31LL;
  }

  fprintf(out, "typing: layouts=%zu p50=%.1f us (%.3f ms) p95=%.1f us (%.3f ms) checksum=%lld\n",
          times.size(), Pct(times, 0.5), Pct(times, 0.5) / 1000.0,
          Pct(times, 0.95), Pct(times, 0.95) / 1000.0, checksum);
  fprintf(out, "fresh:  layouts=%zu p50=%.1f us (%.3f ms) p95=%.1f us (%.3f ms) checksum=%lld\n",
          fresh_times.size(), Pct(fresh_times, 0.5), Pct(fresh_times, 0.5) / 1000.0,
          Pct(fresh_times, 0.95), Pct(fresh_times, 0.95) / 1000.0, fresh_checksum);
  if (out != stdout) fclose(out);
  return 0;
}
