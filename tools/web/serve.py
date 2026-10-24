#!/usr/bin/env python3
"""Serves a browser build locally with the cross-origin isolation headers it needs.

    python3 tools/web/serve.py [directory] [--port 8765]
"""
import argparse
import functools
import http.server


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      '.wasm': 'application/wasm', '.js': 'text/javascript', '.mjs': 'text/javascript'}

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', nargs='?', default='.')
    parser.add_argument('--port', type=int, default=8765)
    options = parser.parse_args()
    handler = functools.partial(Handler, directory=options.directory)
    with http.server.ThreadingHTTPServer(('127.0.0.1', options.port), handler) as server:
        print(f'http://127.0.0.1:{options.port}/', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()
