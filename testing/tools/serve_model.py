#!/usr/bin/env python3
"""Serve local model and audio files for speech recognition mochitests.

Acts as a stand-in model hub: the test points browser.ml.modelHubRootUrl at this
server, and ModelHub fetches <modelName>/<revision>/<file>. Adds CORS headers so
test pages at mochi.test:8888 can fetch from it.

Model lookup (resolved by basename, so a flat directory satisfies the nested hub
path):
  - CI: $MOZ_FETCHES_DIR holds the .gguf (the parakeet-models fetch tasks). The
    network is never touched.
  - dev: $MOZ_ML_LOCAL_DIR if set, else a local cache; a missing model is
    downloaded once from the public bucket so there is nothing to fetch by hand.

Everything else (the audio clip, reference transcript) is served from the tree.

Prerequisites (Linux headless): PipeWire + pipewire-pulse + wireplumber running
with XDG_RUNTIME_DIR set and a null audio sink active.

Usage:
  python3 testing/tools/serve_model.py                       # dev: auto-downloads
  MOZ_ML_LOCAL_DIR=/path/to/ggufs python3 .../serve_model.py # dev: use local dir
"""

import http.server
import os
import socketserver
import urllib.request
from pathlib import Path

PORT = int(os.environ.get("PARAKEET_MODEL_SERVER_PORT", "8766"))
SOURCE_ROOT = Path(__file__).resolve().parents[2]

# Public bucket the CI fetch tasks pull from; also the dev auto-download source.
BUCKET = (
    "https://storage.googleapis.com/moz-model-hub/speech-recognition/parakeet/main/"
)

# In CI the models are pre-fetched into MOZ_FETCHES_DIR and we must not hit the
# network. Outside CI, use MOZ_ML_LOCAL_DIR if given, else a cache we populate
# from the bucket on demand.
FETCHES_DIR = os.environ.get("MOZ_FETCHES_DIR")
DEV_DIR = (
    Path(os.environ.get("MOZ_ML_LOCAL_DIR"))
    if os.environ.get("MOZ_ML_LOCAL_DIR")
    else Path.home() / ".cache" / "mozilla-parakeet-models"
)


def _log(msg):
    print(f"[serve_model] {msg}", flush=True)


def resolve_model(name):
    """Return a local path to the model `name`, downloading it from the bucket
    if necessary (dev only). Returns None if it can't be provided."""
    if FETCHES_DIR:
        # CI: must already be present; never download.
        path = Path(FETCHES_DIR) / name
        if path.is_file():
            _log(
                f"resolve {name} -> {path} ({path.stat().st_size} bytes, MOZ_FETCHES_DIR)"
            )
            return str(path)
        _log(f"resolve {name} -> MISSING in MOZ_FETCHES_DIR={FETCHES_DIR}; serving 404")
        return None
    path = DEV_DIR / name
    if path.is_file():
        _log(f"resolve {name} -> {path} ({path.stat().st_size} bytes, dev cache)")
        return str(path)
    DEV_DIR.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".part")
    _log(f"downloading {name} from {BUCKET} ...")
    try:
        urllib.request.urlretrieve(BUCKET + name, tmp)
        tmp.replace(path)
    except Exception as e:
        _log(f"download FAILED for {name} from {BUCKET}{name}: {e}; serving 404")
        return None
    _log(f"downloaded {name} -> {path} ({path.stat().st_size} bytes)")
    return str(path)


class CORSHandler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        rel = path.lstrip("/").split("?")[0]
        name = os.path.basename(rel)
        if name.endswith(".gguf"):
            resolved = resolve_model(name)
            if resolved:
                return resolved
            _log(f"gguf {name} unresolved; falling through to source tree (will 404)")
        # Audio / transcript / dev in-tree files: serve from the source tree.
        return str(SOURCE_ROOT / rel)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        # Lets runtests.py tell this server apart from an unrelated process
        # that happens to already be listening on the port (e.g. a server
        # left over from a previous, ungracefully-terminated test run).
        self.send_header("X-Parakeet-Model-Server", "1")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

    def log_message(self, format, *args):
        _log("request: " + (format % args))


print(
    f"Serving models from {FETCHES_DIR or DEV_DIR}, other files from "
    f"{SOURCE_ROOT} on port {PORT}",
    flush=True,
)


class Server(http.server.ThreadingHTTPServer):
    def server_bind(self):
        # HTTPServer.server_bind() resolves socket.getfqdn(host), a reverse DNS
        # lookup we don't need. It takes ~36s to time out on the macOS VMs.
        socketserver.TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.server_address[1]


httpd = Server(("localhost", PORT), CORSHandler)
httpd.serve_forever()
