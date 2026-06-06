#!/usr/bin/env python3
"""
Crash-recovery (durability) test for cynamoDB.

Unlike the graceful-restart test, this hard-kills the server with SIGKILL so no
destructor, flush, or clean shutdown runs. It then restarts a fresh process over
the same data directory and asserts that every *acknowledged* write (PutItem that
returned 200) survived -- exercising WAL durability for the unflushed tail,
SSTable + manifest durability for flushed data, and tombstone durability for
deletes.

Usage: crash_recovery_test.py [path-to-cynamodb-binary] [--keep]
"""

import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

TABLE = "CrashTable"
N = 1500  # exceeds the 1000-entry flush threshold: ~1000 flushed, ~500 in WAL tail
checks = 0


class Fail(Exception):
    pass


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise Fail(msg)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Server:
    def __init__(self, binary, data_dir, port, log_path):
        self.binary, self.data_dir, self.port, self.log_path = binary, data_dir, port, log_path
        self.proc = None

    def start(self):
        env = dict(os.environ)
        env.update(CYNAMODB_DATA_DIR=self.data_dir, CYNAMODB_PORT=str(self.port),
                   CYNAMODB_BIND_ADDR="127.0.0.1")
        self._log = open(self.log_path, "ab")
        self.proc = subprocess.Popen([self.binary], env=env, stdout=self._log, stderr=subprocess.STDOUT)
        deadline = time.time() + 15
        url = f"http://127.0.0.1:{self.port}/health"
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise Fail(f"server exited early (code {self.proc.returncode})")
            try:
                with urllib.request.urlopen(url, timeout=1) as r:
                    if r.status == 200:
                        return
            except (urllib.error.URLError, OSError):
                time.sleep(0.1)
        raise Fail("server never became healthy")

    def kill9(self):
        """Hard crash: no graceful shutdown, no flush, no destructor."""
        self.proc.send_signal(signal.SIGKILL)
        self.proc.wait()
        self.proc = None
        self._log.close()


def call(port, target, payload):
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/", data=json.dumps(payload).encode(), method="POST",
        headers={"X-Amz-Target": "DynamoDB_20120810." + target})
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status, json.loads(r.read().decode() or "{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode() or "{}")


def item(i):
    return {"id": {"S": f"rec#{i:05d}"}, "n": {"N": str(i)}, "tag": {"S": f"t{i % 7}"}}


def key(i):
    return {"id": {"S": f"rec#{i:05d}"}}


def scan_count(port):
    total, start = 0, None
    while True:
        p = {"TableName": TABLE, "Limit": 200}
        if start:
            p["ExclusiveStartKey"] = start
        _, body = call(port, "Scan", p)
        total += len(body.get("Items", []))
        start = body.get("LastEvaluatedKey")
        if not start:
            return total


def main():
    args = [a for a in sys.argv[1:] if a != "--keep"]
    keep = "--keep" in sys.argv
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    binary = args[0] if args else os.path.join(repo, "build", "cynamodb")
    if not os.path.isfile(binary):
        print(f"ERROR: binary not found: {binary}", file=sys.stderr)
        return 2

    data_dir = tempfile.mkdtemp(prefix="cynamodb_crash_")
    log_path = os.path.join(data_dir, "server.log")
    port = free_port()
    print(f"crash-recovery test  binary={binary}\n  data_dir={data_dir} port={port}")

    try:
        # ---- Session 1: create + insert, then HARD KILL (no graceful shutdown) ----
        s = Server(binary, data_dir, port, log_path)
        s.start()
        st, _ = call(port, "CreateTable", {
            "TableName": TABLE,
            "KeySchema": [{"AttributeName": "id", "KeyType": "HASH"}],
            "AttributeDefinitions": [{"AttributeName": "id", "AttributeType": "S"}]})
        check(st == 200, "CreateTable")
        for i in range(N):
            st, _ = call(port, "PutItem", {"TableName": TABLE, "Item": item(i)})
            check(st == 200, f"PutItem {i}")
        # Confirm a couple are readable before the crash.
        st, body = call(port, "GetItem", {"TableName": TABLE, "Key": key(0)})
        check(body.get("Item") == item(0), "pre-crash GetItem 0")
        print(f"  inserted {N} records, now SIGKILL (no graceful shutdown)")
        s.kill9()

        # ---- Session 2: fresh process; every acknowledged write must be present ----
        s = Server(binary, data_dir, port, log_path)
        s.start()
        print("  restarted after crash; verifying all acknowledged writes survived")

        # Table catalog survived the crash.
        st, body = call(port, "ListTables", {})
        check(TABLE in body.get("TableNames", []), "table catalog lost after crash")

        # Spot-check exact accuracy across the whole range (flushed + WAL tail).
        missing = []
        for i in range(0, N, 7):
            st, body = call(port, "GetItem", {"TableName": TABLE, "Key": key(i)})
            if body.get("Item") != item(i):
                missing.append(i)
        check(not missing, f"{len(missing)} records lost/corrupted after crash, e.g. {missing[:5]}")

        # The full count must be exactly N (no write lost, none duplicated).
        cnt = scan_count(port)
        check(cnt == N, f"after crash expected {N} records, found {cnt}")

        # ---- Crash durability of DELETES (tombstones in the WAL) ----
        deleted = list(range(0, 300))
        for i in deleted:
            st, _ = call(port, "DeleteItem", {"TableName": TABLE, "Key": key(i)})
            check(st == 200, f"DeleteItem {i}")
        print(f"  deleted {len(deleted)} records, SIGKILL again")
        s.kill9()

        s = Server(binary, data_dir, port, log_path)
        s.start()
        print("  restarted after second crash; verifying deletions persisted")
        for i in (0, 100, 299):
            st, body = call(port, "GetItem", {"TableName": TABLE, "Key": key(i)})
            check("Item" not in body, f"deleted record {i} resurfaced after crash")
        for i in (300, 700, N - 1):  # survivors
            st, body = call(port, "GetItem", {"TableName": TABLE, "Key": key(i)})
            check(body.get("Item") == item(i), f"survivor {i} lost after crash")
        cnt = scan_count(port)
        check(cnt == N - len(deleted), f"after delete+crash expected {N - len(deleted)}, found {cnt}")
        s.kill9()

    except Fail as e:
        print(f"\nFAILED after {checks} checks: {e}", file=sys.stderr)
        if os.path.isfile(log_path):
            print("---- server log tail ----", file=sys.stderr)
            with open(log_path) as f:
                sys.stderr.write("".join(f.readlines()[-40:]))
        return 1
    finally:
        if not keep:
            shutil.rmtree(data_dir, ignore_errors=True)

    print(f"\nALL CHECKS PASSED ({checks} assertions; survived 2 hard crashes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
