#!/usr/bin/env python3
"""Run the port's texture installer with download progress on piped stdout.

The port still owns the URL, checksum, disk checks and installation. This
adapter counts bytes as its existing downloader reads the HTTP response.
"""

import runpy
import sys
import time
import urllib.request


class ProgressResponse:
    def __init__(self, response):
        self.response = response
        try:
            self.total = max(0, int(response.headers.get("Content-Length") or 0))
        except (TypeError, ValueError):
            self.total = 0
        self.received = 0
        self.reported_at = 0.0

    def report(self):
        print("Texture download: %d/%d bytes" % (self.received, self.total), flush=True)
        self.reported_at = time.monotonic()

    def read(self, *args, **kwargs):
        chunk = self.response.read(*args, **kwargs)
        self.received += len(chunk)
        if not chunk or time.monotonic() - self.reported_at >= 0.5:
            self.report()
        if not chunk:
            print("Checking HD texture download", flush=True)
        return chunk

    def __enter__(self):
        self.response.__enter__()
        self.report()
        return self

    def __exit__(self, *args):
        return self.response.__exit__(*args)

    def __getattr__(self, name):
        return getattr(self.response, name)


def main():
    # texture-progress.py GET_PY [get.py's arguments, "textures" by default]
    installer = sys.argv[1]
    original_argv = sys.argv
    original_urlopen = urllib.request.urlopen
    try:
        urllib.request.urlopen = lambda *args, **kwargs: ProgressResponse(original_urlopen(*args, **kwargs))
        sys.argv = [installer] + (original_argv[2:] or ["textures"])
        runpy.run_path(installer, run_name="__main__")
    finally:
        urllib.request.urlopen = original_urlopen
        sys.argv = original_argv


if __name__ == "__main__":
    main()
