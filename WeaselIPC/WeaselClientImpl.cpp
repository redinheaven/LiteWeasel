#include "stdafx.h"
#include "WeaselClientImpl.h"
#include <StringAlgorithm.hpp>
#include <WeaselPerf.h>
#include <atomic>

using namespace weasel;

ClientImpl::ClientImpl()
    : session_id(0), channel(GetPipeName()), is_ime(false) {
  _InitializeClientInfo();
}

ClientImpl::~ClientImpl() {
  if (channel.Connected())
    Disconnect();
}

// http://stackoverflow.com/questions/557081/how-do-i-get-the-hmodule-for-the-currently-executing-code
HMODULE GetCurrentModule() {  // NB: XP+ solution!
  HMODULE hModule = NULL;
  GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    (LPCTSTR)GetCurrentModule, &hModule);

  return hModule;
}

void ClientImpl::_InitializeClientInfo() {
  // get app name
  WCHAR exe_path[MAX_PATH] = {0};
  GetModuleFileName(NULL, exe_path, MAX_PATH);
  std::wstring path = exe_path;
  size_t separator_pos = path.find_last_of(L"\\/");
  if (separator_pos < path.size())
    app_name = path.substr(separator_pos + 1);
  else
    app_name = path;
  to_lower(app_name);
  // determine client type
  GetModuleFileName(GetCurrentModule(), exe_path, MAX_PATH);
  path = exe_path;
  to_lower(path);
  is_ime = ends_with(path, L".ime");
}

bool ClientImpl::Connect(ServerLauncher const& launcher) {
  return channel.Connect();
}

void ClientImpl::Disconnect() {
  if (_Active())
    EndSession();
  channel.Disconnect();
}

void ClientImpl::ShutdownServer() {
  _SendMessage(WEASEL_IPC_SHUTDOWN_SERVER, 0, 0);
}

bool ClientImpl::ProcessKeyEvent(KeyEvent const& keyEvent) {
  if (!_Active())
    return false;

  LRESULT ret =
      _SendMessage(WEASEL_IPC_PROCESS_KEY_EVENT, keyEvent, session_id);
  return ret != 0;
}

bool ClientImpl::CommitComposition() {
  if (!_Active())
    return false;

  LRESULT ret = _SendMessage(WEASEL_IPC_COMMIT_COMPOSITION, 0, session_id);
  return ret != 0;
}

bool ClientImpl::ClearComposition() {
  if (!_Active())
    return false;

  LRESULT ret = _SendMessage(WEASEL_IPC_CLEAR_COMPOSITION, 0, session_id);
  return ret != 0;
}

bool ClientImpl::SelectCandidateOnCurrentPage(size_t index) {
  if (!_Active())
    return false;
  LRESULT ret = _SendMessage(WEASEL_IPC_SELECT_CANDIDATE_ON_CURRENT_PAGE, index,
                             session_id);
  return ret != 0;
}

bool ClientImpl::HighlightCandidateOnCurrentPage(size_t index) {
  if (!_Active())
    return false;
  LRESULT ret = _SendMessage(WEASEL_IPC_HIGHLIGHT_CANDIDATE_ON_CURRENT_PAGE,
                             index, session_id);
  return ret != 0;
}

bool ClientImpl::ChangePage(bool backward) {
  if (!_Active())
    return false;
  LRESULT ret = _SendMessage(WEASEL_IPC_CHANGE_PAGE, backward, session_id);
  return ret != 0;
}

void ClientImpl::UpdateInputPosition(RECT const& rc) {
  if (!_Active())
    return;
  /*
  移位标志 = 1bit == 0
  height:0~127 = 7bit
  top:-2048~2047 = 12bit（有符号）
  left:-2048~2047 = 12bit（有符号）

  高解析度下：
  移位标志 = 1bit == 1
  height:0~254 = 7bit（舍弃低1位）
  top:-4096~4094 = 12bit（有符号，舍弃低1位）
  left:-4096~4094 = 12bit（有符号，舍弃低1位）
  */
  int hi_res =
      static_cast<int>(rc.bottom - rc.top >= 128 || rc.left < -2048 ||
                       rc.left >= 2048 || rc.top < -2048 || rc.top >= 2048);
  int left = max(-2048, min(2047, rc.left >> hi_res));
  int top = max(-2048, min(2047, rc.top >> hi_res));
  int height = max(0, min(127, (rc.bottom - rc.top) >> hi_res));
  DWORD compressed_rect = ((hi_res & 0x01) << 31) | ((height & 0x7f) << 24) |
                          ((top & 0xfff) << 12) | (left & 0xfff);
  _SendMessage(WEASEL_IPC_UPDATE_INPUT_POS, compressed_rect, session_id);
}

void ClientImpl::FocusIn() {
  DWORD client_caps = 0; /* TODO */
  _SendMessage(WEASEL_IPC_FOCUS_IN, client_caps, session_id);
}

void ClientImpl::FocusOut() {
  _SendMessage(WEASEL_IPC_FOCUS_OUT, 0, session_id);
}

void ClientImpl::TrayCommand(UINT menuId) {
  _SendMessage(WEASEL_IPC_TRAY_COMMAND, menuId, session_id);
}

void ClientImpl::StartSession() {
  if (_Active() && Echo())
    return;

  _WriteClientInfo();
  UINT ret = _SendMessage(WEASEL_IPC_START_SESSION, 0, 0);
  session_id = ret;
}

void ClientImpl::EndSession() {
  _SendMessage(WEASEL_IPC_END_SESSION, 0, session_id);
  session_id = 0;
}

void ClientImpl::StartMaintenance() {
  _SendMessage(WEASEL_IPC_START_MAINTENANCE, 0, 0);
  session_id = 0;
}

void ClientImpl::EndMaintenance() {
  _SendMessage(WEASEL_IPC_END_MAINTENANCE, 0, 0);
  session_id = 0;
}

bool ClientImpl::Echo() {
  // Only a session id is required here, not an open pipe: after a timeout the
  // pipe is closed, and Echo is the probe that reconnects it (Transact does
  // that) and checks that the server still knows this session.
  if (session_id == 0)
    return false;

  UINT serverEcho = _SendMessage(WEASEL_IPC_ECHO, 0, session_id);
  return (serverEcho == session_id);
}

bool ClientImpl::GetResponseData(ResponseHandler const& handler) {
  if (!handler) {
    return false;
  }

  return channel.HandleResponseData(handler);
}

bool ClientImpl::_WriteClientInfo() {
  channel << L"action=session\n";
  channel << L"session.client_app=" << app_name.c_str() << L"\n";
  channel << L"session.client_type=" << (is_ime ? L"ime" : L"tsf") << L"\n";
  channel << L".\n";
  return true;
}

// How long a request may take before the client gives up on the server. This
// runs on host-application threads (often the UI thread), so it must be
// bounded. The values are well above what the server needs normally (a key
// event takes about 1 ms, p95 about 5 ms, 97 ms at worst in our traces).
static DWORD TimeoutFor(WEASEL_IPC_COMMAND msg) {
  switch (msg) {
    // These can legitimately take a long time (the server redeploys / shuts
    // down while handling them), and their callers (WeaselDeployer, the
    // tray) are not input-method hosts.
    case WEASEL_IPC_START_MAINTENANCE:
    case WEASEL_IPC_END_MAINTENANCE:
    case WEASEL_IPC_SHUTDOWN_SERVER:
    case WEASEL_IPC_TRAY_COMMAND:
      return INFINITE;
    // Creating a session can involve loading a schema.
    case WEASEL_IPC_START_SESSION:
      return 1000;
    default:
      return 250;
  }
}

static const char* PerfNameFor(WEASEL_IPC_COMMAND msg) {
  switch (msg) {
    case WEASEL_IPC_ECHO:
      return "ipc.echo";
    case WEASEL_IPC_PROCESS_KEY_EVENT:
      return "ipc.key";
    case WEASEL_IPC_START_SESSION:
      return "ipc.start_session";
    default:
      return "ipc.other";
  }
}

// Process-wide circuit breaker for the server connection.
//
// A hung server makes every request wait for its full timeout, and a host
// types many keys per second, so without this each key would stall the host
// thread again. After kTrip consecutive "unresponsive" failures (a timeout, or
// all pipe instances staying busy) within kWindowMs the breaker opens: for
// the open period every guarded request fails immediately and the keys go
// straight to the application. After the period one request is let through as
// a probe with a short timeout; success closes the breaker, failure doubles
// the open period (up to kMaxOpenMs). A server that is simply not running is
// NOT a hang; it does not trip the breaker, so the TSF's own relaunch logic
// keeps working.
namespace {
constexpr DWORD kBaseOpenMs = 3000;
constexpr DWORD kMaxOpenMs = 30000;
constexpr ULONGLONG kWindowMs = 2000;
constexpr int kTrip = 2;
constexpr DWORD kProbeTimeoutMs = 100;

class CircuitBreaker {
 public:
  enum class Verdict { Pass, Probe, Block };

  Verdict Enter() {
    const ULONGLONG until = open_until_.load();
    if (until == 0)
      return Verdict::Pass;
    if (::GetTickCount64() < until)
      return Verdict::Block;
    // Open period over: exactly one caller becomes the probe.
    return probe_in_flight_.exchange(true) ? Verdict::Block : Verdict::Probe;
  }

  // True while requests would be refused (open, or waiting for the probe).
  bool Suspended() const {
    const ULONGLONG until = open_until_.load();
    return until != 0 &&
           (::GetTickCount64() < until || probe_in_flight_.load());
  }

  void Success() {
    if (open_until_.load() == 0 && failures_.load() == 0)
      return;  // common case: nothing to reset
    const bool was_open = open_until_.exchange(0) != 0;
    failures_ = 0;
    backoff_ms_ = kBaseOpenMs;
    probe_in_flight_ = false;
    if (was_open)
      weasel::perf::EmitValue("ipc.breaker.close", 0);
  }

  // The server did not answer in time.
  void Unresponsive(bool probe) {
    const ULONGLONG now = ::GetTickCount64();
    if (probe) {
      DWORD next = backoff_ms_.load() * 2;
      if (next > kMaxOpenMs)
        next = kMaxOpenMs;
      backoff_ms_ = next;
      open_until_ = now + next;
      probe_in_flight_ = false;
      weasel::perf::EmitValue("ipc.breaker.probe_failed",
                              static_cast<INT64>(next) * 1000);
      return;
    }
    if (failures_.load() == 0 || now - first_failure_.load() > kWindowMs) {
      failures_ = 1;
      first_failure_ = now;
    } else {
      ++failures_;
    }
    if (failures_.load() >= kTrip) {
      failures_ = 0;
      open_until_ = now + backoff_ms_.load();
      probe_in_flight_ = false;
      weasel::perf::EmitValue("ipc.breaker.open",
                              static_cast<INT64>(backoff_ms_.load()) * 1000);
    }
  }

  // The request failed for another reason (no server, broken pipe, ...). That
  // is not a hang: leave the decision to the caller's reconnect logic.
  void Gone() {
    failures_ = 0;
    const bool was_open = open_until_.exchange(0) != 0;
    backoff_ms_ = kBaseOpenMs;
    probe_in_flight_ = false;
    if (was_open)
      weasel::perf::EmitValue("ipc.breaker.close", 0);
  }

 private:
  std::atomic<ULONGLONG> open_until_{0};  // 0 = closed
  std::atomic<bool> probe_in_flight_{false};
  std::atomic<DWORD> backoff_ms_{kBaseOpenMs};
  std::atomic<int> failures_{0};
  std::atomic<ULONGLONG> first_failure_{0};
};

CircuitBreaker g_breaker;
}  // namespace

bool ClientImpl::Suspended() {
  return g_breaker.Suspended();
}

LRESULT ClientImpl::_SendMessage(WEASEL_IPC_COMMAND Msg,
                                 DWORD wParam,
                                 DWORD lParam) {
  WEASEL_PERF_SCOPE(PerfNameFor(Msg));
  DWORD timeout = TimeoutFor(Msg);
  // Only requests with a bounded timeout (the input-method path) are guarded.
  const bool guarded = timeout != INFINITE;
  CircuitBreaker::Verdict verdict = CircuitBreaker::Verdict::Pass;
  if (guarded) {
    verdict = g_breaker.Enter();
    if (verdict == CircuitBreaker::Verdict::Block)
      return 0;
    if (verdict == CircuitBreaker::Verdict::Probe && timeout > kProbeTimeoutMs)
      timeout = kProbeTimeoutMs;
  }
  const bool probe = verdict == CircuitBreaker::Verdict::Probe;
  try {
    PipeMessage req{Msg, wParam, lParam};
    LRESULT result = channel.Transact(req, timeout);
    if (guarded)
      g_breaker.Success();
    return result;
  } catch (DWORD err) {
    if (guarded) {
      if (err == ERROR_TIMEOUT)
        g_breaker.Unresponsive(probe);
      else
        g_breaker.Gone();
    }
    return 0;
  } catch (...) {
    if (guarded)
      g_breaker.Gone();  // never leave a probe flag set
    throw;
  }
}

Client::Client() : m_pImpl(new ClientImpl()) {}

Client::~Client() {
  if (m_pImpl)
    delete m_pImpl;
}

bool Client::Connect(ServerLauncher launcher) {
  return m_pImpl->Connect(launcher);
}

void Client::Disconnect() {
  m_pImpl->Disconnect();
}

void Client::ShutdownServer() {
  m_pImpl->ShutdownServer();
}

bool Client::ProcessKeyEvent(KeyEvent const& keyEvent) {
  return m_pImpl->ProcessKeyEvent(keyEvent);
}

bool Client::CommitComposition() {
  return m_pImpl->CommitComposition();
}

bool Client::ClearComposition() {
  return m_pImpl->ClearComposition();
}

bool Client::SelectCandidateOnCurrentPage(size_t index) {
  return m_pImpl->SelectCandidateOnCurrentPage(index);
}

bool Client::HighlightCandidateOnCurrentPage(size_t index) {
  return m_pImpl->HighlightCandidateOnCurrentPage(index);
}

bool Client::ChangePage(bool backward) {
  return m_pImpl->ChangePage(backward);
}

void Client::UpdateInputPosition(RECT const& rc) {
  m_pImpl->UpdateInputPosition(rc);
}

void Client::FocusIn() {
  m_pImpl->FocusIn();
}

void Client::FocusOut() {
  m_pImpl->FocusOut();
}

void Client::StartSession() {
  m_pImpl->StartSession();
}

void Client::EndSession() {
  m_pImpl->EndSession();
}

void Client::StartMaintenance() {
  m_pImpl->StartMaintenance();
}

void Client::EndMaintenance() {
  m_pImpl->EndMaintenance();
}

void Client::TrayCommand(UINT menuId) {
  m_pImpl->TrayCommand(menuId);
}

bool Client::Echo() {
  return m_pImpl->Echo();
}

bool Client::GetResponseData(ResponseHandler handler) {
  return m_pImpl->GetResponseData(handler);
}

bool Client::Suspended() const {
  return ClientImpl::Suspended();
}
