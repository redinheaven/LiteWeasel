// Standalone librime first-key & commit latency benchmark.
// Matches WeaselServer's real flow: keeps sessions open, calls get_context after
// every key (which drives lazy candidate translation/filtering), and checks commit.
// Usage: rime_firstkey_bench.exe <shared_dir> <user_dir> [full_deploy=0|1]
#define RIME_IMPORTS
#include <windows.h>
#include <rime_api.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static double Us(LONGLONG a, LONGLONG b) {
  static LONGLONG freq = []() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  return (b - a) * 1e6 / freq;
}

static LONGLONG Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

static double Median(std::vector<double> v) {
  if (v.empty())
    return 0.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <shared_dir> <user_dir> [full_deploy=0|1]\n", argv[0]);
    return 2;
  }
  const char* shared_dir = argv[1];
  const char* user_dir = argv[2];
  int full_deploy = argc > 3 ? atoi(argv[3]) : 0;

  RimeApi* api = rime_get_api();
  if (!api) return 3;

  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = shared_dir;
  traits.user_data_dir = user_dir;
  traits.prebuilt_data_dir = shared_dir;
  traits.distribution_name = "LiteWeaselBench";
  traits.distribution_code_name = "Weasel";
  traits.distribution_version = "0.17.4";
  traits.app_name = "rime.bench";
  traits.min_log_level = 3;
  traits.log_dir = "";

  api->setup(&traits);
  LONGLONG t_init0 = Now();
  api->initialize(&traits);
  if (api->start_maintenance(full_deploy ? True : False)) {
    api->join_maintenance_thread();
  }
  double init_ms = Us(t_init0, Now()) / 1000.0;

  const int kSessions = 6;  // s0 = server first session, s1..5 = subsequent new app sessions
  const int keys[] = {'n', 'i', 'h', 'a', 'o', ' '};
  std::vector<RimeSessionId> open_sids;
  std::vector<double> s_create, w1_first, w1_mid, w1_commit, w2_first, w2_commit, ctx_us_all;
  char schema_name[128] = {0};

  for (int s = 0; s < kSessions; ++s) {
    LONGLONG tc0 = Now();
    RimeSessionId sid = api->create_session();
    double create_us = Us(tc0, Now());
    if (!sid) return 4;
    open_sids.push_back(sid);
    if (s == 0) api->get_current_schema(sid, schema_name, sizeof(schema_name));

    auto run_word = [&](double& first_us, std::vector<double>& mids, double& commit_us, int& num_cands, bool& committed) {
      committed = false;
      num_cands = 0;
      for (int i = 0; i < 6; ++i) {
        LONGLONG tk0 = Now();
        api->process_key(sid, keys[i], 0);
        double dt = Us(tk0, Now());
        if (i == 0) first_us = dt;
        else if (i < 5) mids.push_back(dt);
        else commit_us = dt;

        LONGLONG tctx0 = Now();
        RIME_STRUCT(RimeContext, ctx);
        if (api->get_context(sid, &ctx)) {
          if (i == 4) num_cands = ctx.menu.num_candidates;
          api->free_context(&ctx);
        }
        if (s > 0) ctx_us_all.push_back(Us(tctx0, Now()));
      }
      RIME_STRUCT(RimeCommit, commit);
      if (api->get_commit(sid, &commit)) {
        committed = (commit.text && commit.text[0] != '\0');
        api->free_commit(&commit);
      }
    };

    double f1 = 0, c1 = 0, f2 = 0, c2 = 0;
    std::vector<double> m1, m2;
    int nc1 = 0, nc2 = 0;
    bool ok1 = false, ok2 = false;
    run_word(f1, m1, c1, nc1, ok1);
    run_word(f2, m2, c2, nc2, ok2);

    if (s == 0) {
      printf("  schema=%s init=%.1fms | s0: create=%.2fms w1_first=%.2fms w1_commit=%.2fms w2_first=%.2fms cands=%d commit=%d\n",
             schema_name, init_ms, create_us / 1000.0, f1 / 1000.0, c1 / 1000.0, f2 / 1000.0, nc1, ok1 ? 1 : 0);
    } else {
      s_create.push_back(create_us);
      w1_first.push_back(f1);
      for (double v : m1) w1_mid.push_back(v);
      w1_commit.push_back(c1);
      w2_first.push_back(f2);
      w2_commit.push_back(c2);
    }
  }

  printf("  s1..5 median: create=%.2fms | w1_first=%.2fms | w1_mid=%.2fms | w1_commit=%.2fms | w2_first=%.2fms | w2_commit=%.2fms | get_ctx=%.2fms\n",
         Median(s_create) / 1000.0, Median(w1_first) / 1000.0, Median(w1_mid) / 1000.0,
         Median(w1_commit) / 1000.0, Median(w2_first) / 1000.0, Median(w2_commit) / 1000.0,
         Median(ctx_us_all) / 1000.0);
  fflush(stdout);
  for (auto sid : open_sids) api->destroy_session(sid);
  api->finalize();
  return 0;
}
