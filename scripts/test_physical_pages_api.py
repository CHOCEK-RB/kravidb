#!/usr/bin/env python3
"""Smoke test using an API binary and Python running on the same OS."""

import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def fixture_page():
    page = bytearray(8192)
    struct.pack_into("<QHHHHHHI", page, 0, 987654321, 0, 0, 28, 8184, 8192, 8193, 0)
    struct.pack_into("<I", page, 24, 8184 | (1 << 15) | (8 << 17))
    struct.pack_into("<q", page, 8184, 27)
    checksum = 0x811C9DC5
    for byte in page:
        checksum = ((checksum ^ byte) * 0x01000193) & 0xFFFFFFFF
    struct.pack_into("<H", page, 8, ((checksum >> 16) ^ checksum) & 0xFFFF)
    return bytes(page)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]

    def request(path, method="GET"):
        req = urllib.request.Request(
            f"http://127.0.0.1:{port}/api/v1/{path}",
            method=method,
            headers={"Origin": "http://localhost:5173"},
        )
        try:
            response = urllib.request.urlopen(req, timeout=2)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            body = response.read()
            return response.status, json.loads(body) if body else None, response.headers

    with tempfile.TemporaryDirectory(prefix="kravidb-page-api-") as root:
        path = Path(root) / "17000"
        original = fixture_page()
        path.write_bytes(original)
        env = os.environ.copy()
        env["KRAVIDB_PAGE_ROOT"] = root
        env["KRAVIDB_BIND"] = "127.0.0.1"
        process = subprocess.Popen(
            [str(binary), str(port)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT
        )
        try:
            deadline = time.monotonic() + 15
            while True:
                if process.poll() is not None:
                    raise RuntimeError(f"API exited during startup: {process.returncode}")
                try:
                    status, health, _ = request("health")
                    break
                except (urllib.error.URLError, TimeoutError):
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.1)
            assert status == 200 and health["mode"] == "readonly_pages", health
            assert health["page_size"] == 8192 and health["data_dir"], health
            assert request("config")[1] == health
            status, page, headers = request("pages/0?relation=17000")
            assert status == 200, page
            assert page["header"]["pd_lsn"] == 987654321, page["header"]
            assert page["items"] == [{"slot": 0, "offset": 8184, "length": 8, "flags": 1}]
            assert bytes.fromhex(page["raw_hex"]) == original
            assert headers["Access-Control-Allow-Origin"] == "http://localhost:5173"
            assert request("pages/1?relation=17000")[0] == 404
            assert request("pages/0?relation=18000")[0] == 404
            assert request("pages/0?relation=../17000")[0] == 400
            assert request("reset", "POST")[1]["sqlstate"] == "0A000"
            assert request("btree")[1]["sqlstate"] == "0A000"
            assert request("pages/0", "OPTIONS")[0] == 204
            assert path.read_bytes() == original
            assert sorted(entry.name for entry in Path(root).iterdir()) == ["17000"]
            print("Physical page HTTP smoke test passed (real process, file bytes, no writes).")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
