#!/usr/bin/env python3
"""Package the tested web runtime with a local server that supplies COOP/COEP."""
import argparse
from pathlib import Path
import zipfile

SERVER = '''#!/usr/bin/env python3
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()
os.chdir(Path(__file__).resolve().parent)
print("Open http://127.0.0.1:8000/GSSquared.html")
ThreadingHTTPServer(("127.0.0.1", 8000), Handler).serve_forever()
'''
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
required = ["GSSquared.html", "GSSquared.js", "GSSquared.wasm", "GSSquared.data"]
for name in required:
    if not (args.build / name).is_file(): raise SystemExit(f"Missing browser package file: {name}")
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(args.build.glob("GSSquared.*")):
        if path.is_file(): archive.write(path, path.name)
    archive.writestr("serve.py", SERVER)
    archive.writestr("README.txt", "Run python3 serve.py, then open http://127.0.0.1:8000/GSSquared.html.\\nA production HTTPS server must supply COOP: same-origin and COEP: require-corp headers.\\n".replace("\\n", "\n"))
print(args.output)
