"""A/B test for the server-side FlushFileBuffers removal.

Client A starts a session, sends a key event and then deliberately does NOT read
the reply (like a host process that was suspended right after sending).
Client B asks the same server for an Echo and measures how long it takes.
With FlushFileBuffers in the server's reply path, the server thread answering A
blocks (holding the global request lock) until A reads or closes, so B is
stuck too. Without it, B is answered at once.
"""
import ctypes, struct, sys, threading, time
from ctypes import wintypes

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype = wintypes.HANDLE
k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                            wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
k32.CloseHandle.argtypes = [wintypes.HANDLE]
INVALID = wintypes.HANDLE(-1).value
WM_APP = 0x8000
ECHO, START, END, KEY = WM_APP + 1, WM_APP + 2, WM_APP + 3, WM_APP + 4
B = chr(92)

user = ctypes.create_unicode_buffer(256); n = wintypes.DWORD(256)
ctypes.WinDLL('advapi32').GetUserNameW(user, ctypes.byref(n))
NAME = B * 2 + '.' + B + 'pipe' + B + user.value + B + 'WeaselNamedPipe'

def open_pipe():
    h = k32.CreateFileW(NAME, 0xC0000000, 0, None, 3, 0, None)
    if h == INVALID:
        sys.exit('cannot open pipe (err %d)' % ctypes.get_last_error())
    mode = wintypes.DWORD(2); k32.SetNamedPipeHandleState(h, ctypes.byref(mode), None, None)
    return h

def send(h, msg, w, l, body=''):
    data = struct.pack('<III', msg, w, l) + body.encode('utf-16-le')
    wr = wintypes.DWORD()
    if not k32.WriteFile(h, data, len(data), ctypes.byref(wr), None):
        sys.exit('write failed %d' % ctypes.get_last_error())

def recv(h):
    buf = ctypes.create_string_buffer(8192); rd = wintypes.DWORD()
    ok = k32.ReadFile(h, buf, 8192, ctypes.byref(rd), None)
    if not ok and ctypes.get_last_error() != 234:
        return None
    return struct.unpack('<I', buf.raw[:4])[0]

HOLD = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0

a = open_pipe()
send(a, START, 0, 0, 'action=session\nsession.client_app=lwstall.exe\nsession.client_type=tsf\n.\n')
sid = recv(a)
print('A: session id =', sid)
if not sid: sys.exit('no session')

result = {}
def client_b():
    time.sleep(0.5)  # let A's request reach the server first
    b = open_pipe()
    t0 = time.perf_counter()
    send(b, ECHO, 0, sid)
    r = recv(b)
    result['ms'] = (time.perf_counter() - t0) * 1000
    result['ok'] = (r == sid)
    k32.CloseHandle(b)

tb = threading.Thread(target=client_b); tb.start()
t_send = time.perf_counter()
send(a, KEY, ord('n'), sid)           # request sent ... reply intentionally never read
print('A: key request sent, now not reading the reply for %.1f s' % HOLD)
time.sleep(HOLD)
k32.CloseHandle(a)                    # A goes away (host closed / killed)
tb.join(30)
print('B: Echo answered in %.1f ms (echo matched session: %s)' % (result.get('ms', -1), result.get('ok')))
# clean up the session we created
c = open_pipe(); send(c, END, 0, sid); recv(c); k32.CloseHandle(c)