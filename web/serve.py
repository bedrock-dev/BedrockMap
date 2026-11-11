"""Serve the Qt wasm build with the headers required by pthreads."""

import argparse
import os
from functools import partial
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Handler(SimpleHTTPRequestHandler):
    def send_head(self):
        path = self.translate_path(self.path)
        compressed = path + ".gz"
        if "gzip" not in self.headers.get("Accept-Encoding", "") or not os.path.isfile(compressed):
            return super().send_head()
        file = open(compressed, "rb")
        try:
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", self.guess_type(path))
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(os.fstat(file.fileno()).st_size))
            self.send_header("Vary", "Accept-Encoding")
            self.end_headers()
            return file
        except BaseException:
            file.close()
            raise

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8002)
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if not all((directory / name).is_file() for name in ("index.html", "BedrockMap.js", "BedrockMap.wasm", "qtloader.js")):
        parser.error("Build first: ./scripts/build_wasm.ps1, then run serve.py from the dist output.")
    with ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(directory))) as server:
        print(f"BedrockMap: http://127.0.0.1:{args.port}/", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
