// Types a key string (gibberish) into one rime session, timing process_key and get_context per key.
// Usage: gib_bench.exe <shared_dir> <user_dir> <keys> [deploy=0|1] [input_cap=0]
#define RIME_IMPORTS
#include <windows.h>
#include <rime_api.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <cstdio>
#include <cstdlib>
#include <cstring>

static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double Ms(LONGLONG a, LONGLONG b) {
  static LONGLONG f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x.QuadPart; }();
  return (b - a) * 1000.0 / f;
}

int main(int argc, char** argv) {
  if (argc < 4) return 2;
  RimeApi* api = rime_get_api();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = argv[1];
  traits.user_data_dir = argv[2];
  traits.prebuilt_data_dir = argv[1];
  traits.distribution_name = "LiteWeaselBench";
  traits.distribution_code_name = "Weasel";
  traits.distribution_version = "0.17.4";
  traits.app_name = "rime.bench";
  traits.min_log_level = 3;
  traits.log_dir = "";
  api->setup(&traits);
  api->initialize(&traits);
  if (api->start_maintenance(argc > 4 && atoi(argv[4]) ? True : False)) api->join_maintenance_thread();

  // keep one warm session open, as the real server usually has
  RimeSessionId warm = api->create_session();
  RimeSessionId sid = api->create_session();
  const char* keys = argv[3];
  printf("keys=%s\n#  key  process_ms  ctx_ms  cands  preedit_len\n", keys);
  double tot = 0, worst = 0;
  // argv[7]=1: warm up with one word, then trim the working set (simulates the OS
  // paging out an idle server), then type the keys. Only soft faults are
  // simulated here (pages come back from the standby list, not from disk).
  // argv[7]=2: same warm-up but no trim (control).
  if (argc > 7 && atoi(argv[7])) {
    for (const char* k = "nihao "; *k; ++k) api->process_key(warm, *k, 0);
    api->destroy_session(warm);
    warm = api->create_session();
    if (atoi(argv[7]) == 1) {
      EmptyWorkingSet(GetCurrentProcess());
      printf("(working set trimmed before typing)\n");
    }
  }
  int cap = argc > 5 ? atoi(argv[5]) : 0;  // mimics RimeWithWeaselHandler::m_max_input_length
  double slow_ms = argc > 6 ? atof(argv[6]) : 0;  // mimics m_slow_key_ms
  bool slow = false;
  for (int i = 0; keys[i]; ++i) {
    LONGLONG a = Now();
    if (cap > 0 || slow_ms > 0) {
      const char* in = api->get_input(sid);
      size_t n = in ? strlen(in) : 0;
      if (n == 0) slow = false;
      else if ((cap > 0 && n >= (size_t)cap) || slow) {
        printf("%2d   %c   (ignored: len %d, %s)\n", i + 1, keys[i], (int)n, slow ? "slow-key guard" : "cap");
        continue;
      }
    }
    api->process_key(sid, keys[i], 0);
    LONGLONG b = Now();
    if (slow_ms > 0 && Ms(a, b) > slow_ms) slow = true;
    RIME_STRUCT(RimeContext, ctx);
    int nc = 0, pl = 0;
    if (api->get_context(sid, &ctx)) {
      nc = ctx.menu.num_candidates;
      pl = ctx.composition.preedit ? (int)strlen(ctx.composition.preedit) : 0;
      api->free_context(&ctx);
    }
    LONGLONG c = Now();
    double p = Ms(a, b), g = Ms(b, c);
    tot += p + g;
    if (p + g > worst) worst = p + g;
    printf("%2d   %c   %8.2f  %7.2f  %4d  %4d\n", i + 1, keys[i], p, g, nc, pl);
  }
  printf("total=%.2fms worst_key=%.2fms\n", tot, worst);
  api->destroy_session(sid);
  api->destroy_session(warm);
  api->finalize();
  return 0;
}
