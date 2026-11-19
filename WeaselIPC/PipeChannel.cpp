#include "stdafx.h"

#include <PipeChannel.h>

using namespace weasel;
using namespace std;
using namespace boost;

#define _ThrowLastError throw ::GetLastError()
#define _ThrowCode(__c) throw __c
#define _ThrowIfNot(__c)                 \
  {                                      \
    DWORD err;                           \
    if ((err = ::GetLastError()) != __c) \
      throw err;                         \
  }

PipeChannelBase::PipeChannelBase(std::wstring&& pn_cmd,
                                 size_t bs = 4 * 1024,
                                 SECURITY_ATTRIBUTES* s = NULL)
    : pname(pn_cmd), buff_size(bs), sa(s) {};

PipeChannelBase::~PipeChannelBase() {
  // Thread-specific pointers are cleaned up automatically
}

bool PipeChannelBase::_Ensure(DWORD connectBudgetMs) {
  try {
    HANDLE* phandle = _GetPipeHandle();
    if (_Invalid(*phandle)) {
      *phandle = _Connect(pname.c_str(), connectBudgetMs);
      return !_Invalid(*phandle);
    }
  } catch (...) {
    return false;
  }

  return true;
}

HANDLE PipeChannelBase::_Connect(const wchar_t* name, DWORD budgetMs) {
  // All pipe instances can be busy for a while (e.g. a stuck server). Wait for
  // a free instance, but never forever: a host UI thread may be calling this.
  const ULONGLONG deadline = ::GetTickCount64() + budgetMs;
  _GetContext()->connect_timed_out = false;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  while (_Invalid(pipe = _TryConnect())) {
    const ULONGLONG now = ::GetTickCount64();
    if (now >= deadline) {
      _GetContext()->connect_timed_out = true;
      return INVALID_HANDLE_VALUE;
    }
    ::WaitNamedPipe(name, static_cast<DWORD>(deadline - now));
  }
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
    _ThrowLastError;
  }
  return pipe;
}

void PipeChannelBase::_Reconnect() {
  HANDLE* phandle = _GetPipeHandle();
  _FinalizePipe(*phandle);
  _Ensure();
}

HANDLE PipeChannelBase::_TryConnect() {
  // Client handles are overlapped so that reads and writes can time out and be
  // cancelled (see _WritePipeTimed / _ReceiveTimed).
  auto pipe = ::CreateFile(pname.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
  if (!_Invalid(pipe)) {
    // connected to the pipe
    return pipe;
  }
  // being busy is not really an error since we just need to wait.
  _ThrowIfNot(ERROR_PIPE_BUSY);
  // All pipe instances are busy
  return INVALID_HANDLE_VALUE;
}

size_t PipeChannelBase::_WritePipe(HANDLE pipe, size_t s, char* b) {
  DWORD lwritten;
  if (!::WriteFile(pipe, b, s, &lwritten, NULL) || lwritten <= 0) {
    _ThrowLastError;
  }
  // No FlushFileBuffers() here: it blocks until the peer has read the data and
  // cannot be given a timeout. The server called it while holding its global
  // request lock, so one stalled client could stall every client. Message-mode
  // pipes keep the data buffered and ordered, and the reader gets it anyway.
  return lwritten;
}

DWORD PipeChannelBase::_WaitOverlapped(HANDLE pipe,
                                       OVERLAPPED& ov,
                                       DWORD timeoutMs,
                                       DWORD* transferred) {
  *transferred = 0;
  if (::WaitForSingleObject(ov.hEvent, timeoutMs) == WAIT_OBJECT_0) {
    if (::GetOverlappedResult(pipe, &ov, transferred, FALSE))
      return ERROR_SUCCESS;
    return ::GetLastError();  // e.g. ERROR_MORE_DATA, ERROR_BROKEN_PIPE
  }
  // Timed out (or the wait itself failed). Cancel, and wait until the kernel
  // is really done with 'ov': it lives on the caller's stack.
  ::CancelIoEx(pipe, &ov);
  if (::GetOverlappedResult(pipe, &ov, transferred, TRUE))
    return ERROR_SUCCESS;  // it completed in the window before the cancel
  const DWORD err = ::GetLastError();
  return err == ERROR_OPERATION_ABORTED ? ERROR_TIMEOUT : err;
}

size_t PipeChannelBase::_WritePipeTimed(HANDLE pipe,
                                        size_t s,
                                        char* b,
                                        DWORD timeoutMs) {
  HANDLE ev = _GetContext()->io_event;
  if (!ev)
    throw static_cast<DWORD>(ERROR_OUTOFMEMORY);
  OVERLAPPED ov = {};
  ov.hEvent = ev;
  ::ResetEvent(ev);
  DWORD written = 0;
  DWORD err = ERROR_SUCCESS;
  if (!::WriteFile(pipe, b, static_cast<DWORD>(s), NULL, &ov)) {
    err = ::GetLastError();
    if (err == ERROR_IO_PENDING)
      err = _WaitOverlapped(pipe, ov, timeoutMs, &written);
  } else if (!::GetOverlappedResult(pipe, &ov, &written, FALSE)) {
    err = ::GetLastError();
  }
  if (err != ERROR_SUCCESS)
    throw err;
  if (written == 0)
    throw static_cast<DWORD>(ERROR_WRITE_FAULT);
  return written;
}

void PipeChannelBase::_ReceiveTimed(HANDLE pipe,
                                    LPVOID msg,
                                    size_t rec_len,
                                    DWORD timeoutMs) {
  auto ctx = _GetContext();
  HANDLE ev = ctx->io_event;
  if (!ev)
    throw static_cast<DWORD>(ERROR_OUTOFMEMORY);
  auto read = [&](LPVOID dst, size_t len) -> DWORD {
    OVERLAPPED ov = {};
    ov.hEvent = ev;
    ::ResetEvent(ev);
    DWORD got = 0;
    if (!::ReadFile(pipe, dst, static_cast<DWORD>(len), NULL, &ov)) {
      const DWORD err = ::GetLastError();
      if (err != ERROR_IO_PENDING)
        return err;  // includes an immediate ERROR_MORE_DATA
      return _WaitOverlapped(pipe, ov, timeoutMs, &got);
    }
    return ::GetOverlappedResult(pipe, &ov, &got, FALSE) ? ERROR_SUCCESS
                                                        : ::GetLastError();
  };
  // Same protocol as _Receive(): the first read takes the fixed-size result;
  // if the message is longer, the rest (the body) is read into the buffer.
  DWORD err = read(msg, rec_len);
  if (err == ERROR_MORE_DATA) {
    memset(ctx->buffer.get(), 0, buff_size);
    err = read(ctx->buffer.get(), buff_size);
  }
  if (err != ERROR_SUCCESS)
    throw err;
  ctx->has_body = false;
}

void PipeChannelBase::_FinalizePipe(HANDLE& p) {
  if (!_Invalid(p)) {
    DisconnectNamedPipe(p);
    CloseHandle(p);
  }
  p = INVALID_HANDLE_VALUE;
}

void PipeChannelBase::_Receive(HANDLE pipe, LPVOID msg, size_t rec_len) {
  DWORD lread;
  BOOL success = ::ReadFile(pipe, msg, rec_len, &lread, NULL);
  if (!success) {
    _ThrowIfNot(ERROR_MORE_DATA);

    auto ctx = _GetContext();
    memset(ctx->buffer.get(), 0, buff_size);
    success = ::ReadFile(pipe, ctx->buffer.get(), buff_size, &lread, NULL);
    if (!success) {
      _ThrowLastError;
    }
  }
  _GetContext()->has_body = false;
}

HANDLE PipeChannelBase::_ConnectServerPipe(std::wstring& pn) {
  HANDLE pipe =
      CreateNamedPipe(pn.c_str(), PIPE_ACCESS_DUPLEX,
                      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                      PIPE_UNLIMITED_INSTANCES, buff_size, buff_size, 0, sa);
  if (pipe == INVALID_HANDLE_VALUE || !::ConnectNamedPipe(pipe, NULL)) {
    _ThrowLastError;
  }
  return pipe;
}
