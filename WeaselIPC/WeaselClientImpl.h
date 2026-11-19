#pragma once
#include <WeaselIPC.h>
#include <PipeChannel.h>

namespace weasel {

class ClientImpl {
 public:
  ClientImpl();
  ~ClientImpl();

  bool Connect(ServerLauncher const& launcher);
  void Disconnect();
  void ShutdownServer();
  void StartSession();
  void EndSession();
  void StartMaintenance();
  void EndMaintenance();
  bool Echo();
  bool ProcessKeyEvent(KeyEvent const& keyEvent);
  bool CommitComposition();
  bool ClearComposition();
  bool SelectCandidateOnCurrentPage(size_t index);
  bool HighlightCandidateOnCurrentPage(size_t index);
  bool ChangePage(bool backward);
  void UpdateInputPosition(RECT const& rc);
  void FocusIn();
  void FocusOut();
  void TrayCommand(UINT menuId);
  bool GetResponseData(ResponseHandler const& handler);
  // True while the process-wide circuit breaker refuses requests.
  static bool Suspended();
  bool IsRecentlyAlive(DWORD maxAgeMs) const;
  bool LastRequestFailed() const { return last_ipc_failed; }

 protected:
  void _InitializeClientInfo();
  bool _WriteClientInfo();

  LRESULT _SendMessage(WEASEL_IPC_COMMAND Msg, DWORD wParam, DWORD lParam);

  bool _Connected() const { return channel.Connected(); }
  bool _Active() const { return channel.Connected() && session_id != 0; }

 private:
  UINT session_id;
  std::wstring app_name;
  bool is_ime;
  ULONGLONG last_verified_tick;
  bool last_ipc_failed;

  PipeChannel<PipeMessage> channel;
};

}  // namespace weasel