#!/usr/bin/env python3
from __future__ import annotations

import argparse
import functools
import gzip
import io
import os
import shutil
import ssl
import threading
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Handler(SimpleHTTPRequestHandler):
    # wasm ローダーは分割片を通常のリソースとして取得する。ローカルサーバー
    # で応答を圧縮し、fetch() が Content-Encoding を透
    # 過的に復号した後で wasm_parts.js が元のハッシュを検証する。
    _gzip_suffixes = (".wasm", ".part")
    _private_root_entries = frozenset(
        {
            "_headers",
            "_routes.json",
            "functions",
            "functions.zip",
            "memo.txt",
            "serve.py",
            "start_local_server.ps1",
            "start_web_chrome.cmd",
            "test",
            "tools",
            "wrangler.jsonc",
        }
    )

    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript; charset=utf-8",
        ".mjs": "text/javascript; charset=utf-8",
        ".html": "text/html; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".txt": "text/plain; charset=utf-8",
        ".xml": "application/xml; charset=utf-8",
        ".svg": "image/svg+xml; charset=utf-8",
        ".wasm": "application/wasm",
        ".json": "application/json; charset=utf-8",
    }

    def _relative_request_path(self, request_path: str) -> Path | None:
        root = Path(self.directory).resolve()
        requested = Path(self.translate_path(request_path)).resolve()
        try:
            relative = requested.relative_to(root)
        except ValueError:
            return None
        if any(part.startswith(".") for part in relative.parts):
            return None
        if relative.parts and relative.parts[0].casefold() in self._private_root_entries:
            return None
        return relative

    def send_head(self):
        if self._relative_request_path(self.path) is None:
            self.send_error(404, "File not found")
            return None
        return super().send_head()

    def list_directory(self, path: str):
        self.send_error(404, "Directory listing is disabled")
        return None

    @staticmethod
    def _accepts_gzip(header: str) -> bool:
        wildcard = None
        for item in header.split(","):
            pieces = [piece.strip() for piece in item.split(";")]
            encoding = pieces[0].lower()
            quality = 1.0
            for parameter in pieces[1:]:
                name, separator, value = parameter.partition("=")
                if name.strip().lower() != "q" or not separator:
                    continue
                try:
                    quality = float(value.strip())
                except ValueError:
                    quality = 0.0
                break
            if encoding == "gzip":
                return quality > 0.0
            if encoding == "*":
                wildcard = quality
        return wildcard is not None and wildcard > 0.0

    def _gzip_path(self) -> str | None:
        if self._relative_request_path(self.path) is None:
            return None
        if "Range" in self.headers:
            return None
        if not self._accepts_gzip(self.headers.get("Accept-Encoding", "")):
            return None
        path = self.translate_path(self.path)
        if not path.lower().endswith(self._gzip_suffixes):
            return None
        try:
            if not os.path.isfile(path):
                return None
        except OSError:
            return None
        return path

    def _serve_gzip(self, send_body: bool) -> bool:
        path = self._gzip_path()
        if path is None:
            return False
        try:
            stat_result = os.stat(path)
            payload = io.BytesIO()
            with gzip.GzipFile(fileobj=payload, mode="wb", compresslevel=6, mtime=0) as encoded:
                with open(path, "rb") as source:
                    shutil.copyfileobj(source, encoded, length=1024 * 1024)
            compressed = payload.getvalue()
        except OSError:
            self.send_error(404, "File not found")
            return True
        self.send_response(200)
        self.send_header("Content-type", self.guess_type(path))
        self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(compressed)))
        self.send_header("Last-Modified", self.date_time_string(stat_result.st_mtime))
        self.end_headers()
        if send_body:
            self.wfile.write(compressed)
        return True

    def do_GET(self) -> None:
        if self._serve_gzip(True):
            return
        super().do_GET()

    def do_HEAD(self) -> None:
        if self._serve_gzip(False):
            return
        super().do_HEAD()

    def end_headers(self) -> None:
        self.send_header("Vary", "Accept-Encoding")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("X-Content-Type-Options", "nosniff")
        # ラッパー・一覧の変更を再検証しつつ、未変更の JS/Wasm
        # は再ダウンロードせず 304 で応答できるようにする。
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


def main() -> int:
    parser = argparse.ArgumentParser(description="Serve CardWirthPy_C Web locally")
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--certfile", type=Path)
    parser.add_argument("--keyfile", type=Path)
    parser.add_argument("--open", action="store_true")
    args = parser.parse_args()
    if (args.certfile is None) != (args.keyfile is None):
        parser.error("--certfile and --keyfile must be specified together")
    root = args.root.resolve()
    if not (root / "index.html").is_file():
        parser.error(f"index.html not found under: {root}")
    handler = functools.partial(Handler, directory=str(root))
    with ThreadingHTTPServer((args.host, args.port), handler) as server:
        scheme = "http"
        if args.certfile is not None:
            certfile = args.certfile.expanduser().resolve()
            keyfile = args.keyfile.expanduser().resolve()
            if not certfile.is_file():
                parser.error(f"certificate file not found: {certfile}")
            if not keyfile.is_file():
                parser.error(f"private key file not found: {keyfile}")
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(certfile=str(certfile), keyfile=str(keyfile))
            server.socket = context.wrap_socket(server.socket, server_side=True)
            scheme = "https"
        url_host = args.host
        if url_host in ("", "0.0.0.0", "::"):
            url_host = "127.0.0.1"
        url = f"{scheme}://{url_host}:{args.port}/"
        print(url)
        if args.open:
            threading.Timer(0.4, lambda: webbrowser.open(url)).start()
        server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
