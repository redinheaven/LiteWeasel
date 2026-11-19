#pragma once
#include <string>
#include <memory>
#include <windows.h>
#include <boost/interprocess/streams/bufferstream.hpp>
#include <boost/thread.hpp>
#include <boost/thread/tss.hpp>

namespace weasel {

class PipeChannelBase {
 public:
  using Stream = boost::interprocess::wbufferstream;

  struct ChannelContext {
    std::unique_ptr<char[]> buffer;
    std::unique_ptr<Stream> write_stream;
    bool has_body;
    // False from the start of a transaction until its response has been fully
    // received, so a failed or timed-out transaction cannot make the caller
    // parse the previous response again.
    bool response_valid;
    // Manual-reset event used for this thread's overlapped client I/O.
    HANDLE io_event;

    ChannelContext(size_t bs)
        : buffer(std::make_unique<char[]>(bs)),
          has_body(false),
          response_valid(true),
          io_event(::CreateEventW(NULL, TRUE, FALSE, NULL)) {}
    ~ChannelContext() {
      if (io_event)
        ::CloseHandle(io_event);
    }
    ChannelContext(const ChannelContext&) = delete;
    ChannelContext& operator=(const ChannelContext&) = delete;
  };

  // Timeout value meaning "plain synchronous I/O" (used by the server side,
  // whose pipe handles are not overlapped). Client I/O always uses a timeout
  // (INFINITE included), because client handles are opened overlapped.
  static constexpr DWORD kSyncIo = 0;

  PipeChannelBase(std::wstring&& pn_cmd, size_t bs, SECURITY_ATTRIBUTES* s);
  ~PipeChannelBase();

 protected:
  /* To ensure connection before operation */
  bool _Ensure();
  /* Connect pipe as client */
  HANDLE _Connect(const wchar_t* name);
  /* To reconnect message pipe */
  void _Reconnect();
  /* Try to connect for one time */
  HANDLE _TryConnect();
  size_t _WritePipe(HANDLE p, size_t s, char* b);
  /* Overlapped write / read with a timeout (client side). On timeout the I/O
   * is cancelled, and ERROR_TIMEOUT is thrown. */
  size_t _WritePipeTimed(HANDLE p, size_t s, char* b, DWORD timeoutMs);
  void _ReceiveTimed(HANDLE pipe, LPVOID msg, size_t rec_len, DWORD timeoutMs);
  /* Wait for a pending overlapped operation; returns an error code. */
  DWORD _WaitOverlapped(HANDLE pipe,
                        OVERLAPPED& ov,
                        DWORD timeoutMs,
                        DWORD* transferred);
  void _FinalizePipe(HANDLE& p);
  void _Receive(HANDLE pipe, LPVOID msg, size_t rec_len);
  /* Try to get a connection from client */
  HANDLE _ConnectServerPipe(std::wstring& pn);
  inline bool _Invalid(HANDLE p) const { return p == INVALID_HANDLE_VALUE; }

  HANDLE* _GetPipeHandle() const {
    if (!hpipe_ptr.get()) {
      hpipe_ptr.reset(new HANDLE(INVALID_HANDLE_VALUE));
    }
    return hpipe_ptr.get();
  }

  ChannelContext* _GetContext() const {
    if (!context.get()) {
      context.reset(new ChannelContext(buff_size));
    }
    return context.get();
  }

 protected:
  std::wstring pname;
  // Thread-local pipe handle for isolation
  mutable boost::thread_specific_ptr<HANDLE> hpipe_ptr;
  const size_t buff_size;
  // Thread-local context for buffer and state
  mutable boost::thread_specific_ptr<ChannelContext> context;

 private:
  /* Security attributes */
  SECURITY_ATTRIBUTES* sa;
};

/* Pipe based IPC channel */
template <typename _TyMsg,
          typename _TyRes = DWORD,
          size_t _MsgSize = sizeof(_TyMsg),
          size_t _ResSize = sizeof(_TyRes)>
class PipeChannel : public PipeChannelBase {
 public:
  /* Type definitions */

  using Ptr = std::shared_ptr<PipeChannel>;
  using UPtr = std::unique_ptr<PipeChannel>;
  using Msg = _TyMsg;
  using Res = _TyRes;

  enum class ChannalCommand { NEW_MSG_PIPE, REFRESH };

 public:
  PipeChannel(std::wstring&& pn_cmd,
              SECURITY_ATTRIBUTES* s = NULL,
              size_t bs = 64 * 1024)
      : PipeChannelBase(std::move(pn_cmd), bs, s) {}

 public:
  /* Common pipe operations */

  bool Connect() { return _Ensure(); }
  bool Connected() const {
    HANDLE* phandle = _GetPipeHandle();
    return !_Invalid(*phandle);
  }
  void Disconnect() {
    HANDLE* phandle = _GetPipeHandle();
    _FinalizePipe(*phandle);
  }

  /* Write data to buffer */

  template <typename _TyWrite>
  void Write(_TyWrite cnt) {
    _GetContext()->has_body = true;
    _BufferWriteStream() << cnt;
  }

  /* Write data to buffer */
  template <typename _TyWrite>
  PipeChannel& operator<<(_TyWrite cnt) {
    Write(cnt);
    return *this;
  }

  /* Send a request and wait for the response (client side). timeoutMs is the
   * total budget for writing the request and reading the response; INFINITE
   * waits forever. On timeout the pipe is closed (a late response is then
   * discarded by the system and cannot be mistaken for the next one) and
   * ERROR_TIMEOUT is thrown. Any failure leaves the response buffer invalid. */
  _TyRes Transact(Msg& msg, DWORD timeoutMs = INFINITE) {
    if (timeoutMs == kSyncIo)
      timeoutMs = INFINITE;
    _GetContext()->response_valid = false;
    // Fail fast when there is no connection (e.g. the connect budget ran out)
    // instead of trying to write to an invalid handle and reconnecting again.
    if (!_Ensure())
      throw static_cast<DWORD>(ERROR_PIPE_NOT_CONNECTED);
    HANDLE* phandle = _GetPipeHandle();
    const ULONGLONG start = ::GetTickCount64();
    try {
      _Send(*phandle, msg, timeoutMs);
      DWORD remaining = timeoutMs;
      if (timeoutMs != INFINITE) {
        const ULONGLONG elapsed = ::GetTickCount64() - start;
        remaining = elapsed >= timeoutMs
                        ? 1
                        : static_cast<DWORD>(timeoutMs - elapsed);
      }
      _TyRes result = _ReceiveResponse(remaining);
      _GetContext()->response_valid = true;
      return result;
    } catch (DWORD err) {
      // A request body that was staged but not delivered must not be sent
      // along with the next request.
      ClearBufferStream();
      if (err == ERROR_TIMEOUT)
        _FinalizePipe(*phandle);
      throw;
    }
  }

  void ClearBufferStream() {
    auto ctx = _GetContext();
    ctx->has_body = false;
    if (ctx->write_stream != nullptr) {
      ctx->write_stream.reset(nullptr);
    }
  }

  char* SendBuffer() const { return _GetContext()->buffer.get() + _MsgSize; }

  char* ReceiveBuffer() const { return _GetContext()->buffer.get() + _ResSize; }

  template <typename _TyHandler>
  bool HandleResponseData(_TyHandler const& handler) {
    if (!handler) {
      return false;
    }
    // The last transaction failed or timed out: the buffer holds a stale
    // response, do not hand it to the caller again.
    if (!_GetContext()->response_valid) {
      return false;
    }

    // Use whole buffer to receive data in client
    return handler((LPWSTR)_GetContext()->buffer.get(),
                   (UINT)(buff_size * sizeof(char) / sizeof(wchar_t)));
  }

 protected:
  void _Send(HANDLE pipe, Msg& msg, DWORD timeoutMs = kSyncIo) {
    auto ctx = _GetContext();
    char* pbuff = ctx->buffer.get();
    DWORD lwritten = 0;

    *reinterpret_cast<Msg*>(pbuff) = msg;
    size_t body_bytes = 0;
    if (ctx->has_body && ctx->write_stream) {
      std::streampos pos = ctx->write_stream->tellp();
      if (pos != std::streampos(-1)) {
        body_bytes = static_cast<size_t>(pos) * sizeof(wchar_t);
      }
    }
    size_t data_sz = ctx->has_body ? (_MsgSize + body_bytes) : _MsgSize;
    if (data_sz > buff_size)
      data_sz = buff_size;

    auto write = [&](HANDLE h) {
      if (timeoutMs == kSyncIo)
        _WritePipe(h, data_sz, pbuff);
      else
        _WritePipeTimed(h, data_sz, pbuff, timeoutMs);
    };
    try {
      write(pipe);
    } catch (DWORD err) {
      // A timeout is final: do not spend the budget a second time.
      if (err == ERROR_TIMEOUT)
        throw;
      // Stale handle (e.g. the server restarted): reconnect once and retry.
      // (The retry used to pass the address of the handle variable instead of
      // the handle, so it could never succeed.)
      _Reconnect();
      write(*_GetPipeHandle());
    } catch (...) {
      _Reconnect();
      write(*_GetPipeHandle());
    }
    ClearBufferStream();
  }

  _TyRes _ReceiveResponse(DWORD timeoutMs = kSyncIo) {
    HANDLE* phandle = _GetPipeHandle();
    _TyRes result;
    if (timeoutMs == kSyncIo)
      _Receive(*phandle, &result, sizeof(result));
    else
      _ReceiveTimed(*phandle, &result, sizeof(result), timeoutMs);
    return result;
  }

  Stream& _BufferWriteStream() {
    auto ctx = _GetContext();
    if (ctx->write_stream == nullptr) {
      char* pbuff = (char*)ctx->buffer.get() + _MsgSize;
      memset(pbuff, 0, buff_size - _MsgSize);
      ctx->write_stream =
          std::make_unique<Stream>((wchar_t*)pbuff, _SendBufferSizeW());
    }
    return *ctx->write_stream;
  }

 private:
  inline size_t _SendBufferSizeW() const {
    return (buff_size - _MsgSize) * sizeof(char) / sizeof(wchar_t);
  }

  inline size_t _ReceiveBufferSizeW() const {
    return (buff_size - _ResSize) * sizeof(char) / sizeof(wchar_t);
  }
};
};  // namespace weasel
