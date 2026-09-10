# gs2debug

Python client for the GSSquared external debug protocol.

Local Unix-domain sockets work on macOS, Linux and Windows 10 or later. When a
Windows Python build lacks `socket.AF_UNIX`, the client connects through
Winsock using `ctypes`, then uses ordinary Python sockets for protocol I/O,
timeouts and cleanup. No additional Python dependencies are required.

- **Agent cookbook:** [Docs/gs2debug.md](../../Docs/gs2debug.md)
- Wire format: [Docs/DebugProtocol.md](../../Docs/DebugProtocol.md)
- Client design notes: [Docs/DebugClient.md](../../Docs/DebugClient.md)

## Install (editable)

```bash
cd clients/python
pip install -e .
```

## Smoke test

With the emulator listening (separate terminals):

```bash
# terminal 1 — IIe
./build/GSSquared --debug /tmp/gs2.sock -p 2

# terminal 2
cd clients/python
PYTHONPATH=src python examples/hello_ping.py /tmp/gs2.sock
```

`hello_ping.py` prints `execution_mode` and `platform_id` from `GET_STATUS`.

## Tests (framing only)

```bash
cd clients/python
pip install -e ".[dev]"
pytest
```
