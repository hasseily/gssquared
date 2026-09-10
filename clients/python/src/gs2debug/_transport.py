"""Local debug sockets, including Windows Pythons without AF_UNIX bindings."""

from __future__ import annotations

import os
import socket


def connect_unix(path: str) -> socket.socket:
    if os.name == "nt" and not hasattr(socket, "AF_UNIX"):
        return _connect_windows_unix(path)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(path)
        return sock
    except BaseException:
        sock.close()
        raise


def _connect_windows_unix(path: str) -> socket.socket:
    # Windows 10+ implements AF_UNIX in Winsock even when Python was built
    # without sockaddr_un support. Only address conversion needs this bridge;
    # ordinary Python sockets still own the handle and provide all subsequent IO.
    import ctypes
    import select

    encoded = path.encode("utf-8")
    if not encoded or b"\0" in encoded or len(encoded) >= 108:
        raise ValueError("Unix socket path must contain 1–107 UTF-8 bytes and no NUL")

    class SockaddrUn(ctypes.Structure):
        _fields_ = [("family", ctypes.c_ushort), ("path", ctypes.c_char * 108)]

    winsock = ctypes.WinDLL("Ws2_32.dll")
    connect = winsock.connect
    connect.argtypes = [ctypes.c_size_t, ctypes.POINTER(SockaddrUn), ctypes.c_int]
    connect.restype = ctypes.c_int
    last_error = winsock.WSAGetLastError
    last_error.argtypes = []
    last_error.restype = ctypes.c_int
    address = SockaddrUn(1, encoded)  # AF_UNIX is 1 in the Windows SDK.
    sock = socket.socket(1, socket.SOCK_STREAM)
    try:
        if connect(sock.fileno(), ctypes.byref(address), ctypes.sizeof(address)):
            error = last_error()
            timeout = sock.gettimeout()
            if error == 10035 and timeout is not None and timeout > 0:  # WSAEWOULDBLOCK
                _, writable, exceptional = select.select([], [sock], [sock], timeout)
                if not writable and not exceptional:
                    raise TimeoutError("Unix socket connection timed out")
                error = sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
            if error:
                raise ctypes.WinError(error)
        return sock
    except BaseException:
        sock.close()
        raise
