"""Socket ownership and the Windows address bridge, without a Windows host."""

import ctypes
import select
import socket
import tempfile
from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from gs2debug import _transport


@pytest.mark.skipif(not hasattr(socket, "AF_UNIX"), reason="native AF_UNIX unavailable")
def test_native_socket_exchange():
    with tempfile.TemporaryDirectory(prefix="gs2-socket-") as directory:
        path = directory + "/debug.sock"
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
            server.settimeout(2)
            server.bind(path)
            server.listen(1)
            with _transport.connect_unix(path) as client, server.accept()[0] as peer:
                client.settimeout(2)
                peer.settimeout(2)
                client.sendall(b"ping")
                assert peer.recv(4) == b"ping"
                peer.sendall(b"pong")
                assert client.recv(4) == b"pong"


def test_failed_native_connect_closes_socket(monkeypatch):
    sock = Mock()
    sock.connect.side_effect = OSError("connection refused")
    monkeypatch.setattr(_transport, "os", SimpleNamespace(name="posix"))
    monkeypatch.setattr(_transport, "socket", SimpleNamespace(
        AF_UNIX=1, SOCK_STREAM=1, socket=Mock(return_value=sock)))
    with pytest.raises(OSError, match="connection refused"):
        _transport.connect_unix("missing.sock")
    sock.close.assert_called_once()


def test_windows_without_python_address_support_uses_bridge(monkeypatch):
    bridge = Mock()
    monkeypatch.setattr(_transport, "os", SimpleNamespace(name="nt"))
    monkeypatch.setattr(_transport, "socket", SimpleNamespace())
    monkeypatch.setattr(_transport, "_connect_windows_unix", bridge)
    assert _transport.connect_unix("debug.sock") is bridge.return_value
    bridge.assert_called_once_with("debug.sock")


@pytest.mark.parametrize("path", ["", "nul\0byte", "x" * 108, "é" * 54])
def test_windows_rejects_invalid_paths_before_opening_socket(path, monkeypatch):
    factory = Mock()
    monkeypatch.setattr(_transport, "socket", SimpleNamespace(socket=factory))
    with pytest.raises(ValueError):
        _transport._connect_windows_unix(path)
    factory.assert_not_called()


@pytest.mark.parametrize("error,ready,pending_error,expected", [
    (0, True, 0, None),
    (10061, True, 0, OSError),
    (10035, True, 0, None),
    (10035, False, 0, TimeoutError),
    (10035, True, 10061, OSError),
])
def test_windows_connect_ownership_and_timeouts(
        error, ready, pending_error, expected, monkeypatch):
    sock = Mock()
    sock.fileno.return_value = 0x123456789
    sock.gettimeout.return_value = 0.1
    sock.getsockopt.return_value = pending_error
    winsock = SimpleNamespace(connect=Mock(return_value=bool(error)),
                              WSAGetLastError=Mock(return_value=error))
    monkeypatch.setattr(ctypes, "WinDLL", Mock(return_value=winsock), raising=False)
    monkeypatch.setattr(ctypes, "WinError", lambda code: OSError(code, "Winsock"), raising=False)
    monkeypatch.setattr(_transport, "socket", SimpleNamespace(
        socket=Mock(return_value=sock), SOCK_STREAM=1, SOL_SOCKET=1, SO_ERROR=4))
    monkeypatch.setattr(select, "select", lambda *args: ([], [sock] if ready else [], []))
    if expected:
        with pytest.raises(expected):
            _transport._connect_windows_unix("débug.sock")
        sock.close.assert_called_once()
    else:
        assert _transport._connect_windows_unix("débug.sock") is sock
        sock.close.assert_not_called()
    handle, address, size = winsock.connect.call_args.args
    assert handle == 0x123456789
    assert winsock.connect.argtypes[0] is ctypes.c_size_t
    assert size == 110 and address._obj.family == 1
    assert address._obj.path == "débug.sock".encode("utf-8")
