#!/usr/bin/env python3
"""
Concurrent-load test for cynamoDB: hammers the live server from many client
threads simultaneously to validate thread-safety under real parallel CRUD.

Each worker thread owns a disjoint key range, so the expected final state is
deterministic: a correct, race-free engine must end with exactly every written
record present and accurate (no lost, duplicated, or corrupted items, no crash,
no deadlock). Then half are deleted concurrently and the result re-validated.

Usage: concurrent_load_test.py [path-to-cynamodb-binary] [--keep]
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
from concurrent.futures import ThreadPoolExecutor

TABLE = "ConcTable"
THREADS = 8
PER_THREAD = 500          # 4000 records total
TOTAL = THREADS * PER_THREAD
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


def call(port, target, payload):
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/", data=json.dumps(payload).encode(), method="POST",
        headers={"X-Amz-Target": "DynamoDB_20120810." + target})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode() or "{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode() or "{}")


def rec_id(t, i):
    return f"t{t:02d}#{i:05d}"


def item(t, i):
    return {"id": {"S": rec_id(t, i)}, "thread": {"N": str(t)}, "seq": {"N": str(i)}}


def key(rid):
    return {"id": {"S": rid}}


def scan_ids(port):
    ids, start = [], None
    while True:
        p = {"TableName": TABLE, "Limit": 500}
        if start:
            p["ExclusiveStartKey"] = start
        _, body = call(port, "Scan", p)
        ids += [it["id"]["S"] for it in body.get("Items", [])]
        start = body.get("LastEvaluatedKey")
        if not start:
            return ids


def start_server(binary, data_dir, port, log_path):
    env = dict(os.environ)
    env.update(CYNAMODB_DATA_DIR=data_dir, CYNAMODB_PORT=str(port), CYNAMODB_BIND_ADDR="127.0.0.1")
    log = open(log_path, "ab")
    proc = subprocess.Popen([binary], env=env, stdout=log, stderr=subprocess.STDOUT)
    deadline = time.time() + 15
    while time.time() < deadline:
        if proc.poll() is not None:
            raise Fail(f"server exited early (code {proc.returncode})")
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=1) as r:
                if r.status == 200:
                    return proc, log
        except (urllib.error.URLError, OSError):
            time.sleep(0.1)
    raise Fail("server never became healthy")


def main():
    args = [a for a in sys.argv[1:] if a != "--keep"]
    keep = "--keep" in sys.argv
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    binary = args[0] if args else os.path.join(repo, "build", "cynamodb")
    if not os.path.isfile(binary):
        print(f"ERROR: binary not found: {binary}", file=sys.stderr)
        return 2

    data_dir = tempfile.mkdtemp(prefix="cynamodb_conc_")
    log_path = os.path.join(data_dir, "server.log")
    port = free_port()
    print(f"concurrent-load test  binary={binary}\n  data_dir={data_dir} port={port}"
          f"  threads={THREADS} total={TOTAL}")

    proc = None
    try:
        proc, _log = start_server(binary, data_dir, port, log_path)
        st, _ = call(port, "CreateTable", {
            "TableName": TABLE,
            "KeySchema": [{"AttributeName": "id", "KeyType": "HASH"}],
            "AttributeDefinitions": [{"AttributeName": "id", "AttributeType": "S"}]})
        check(st == 200, "CreateTable")

        # ---- Concurrent inserts: every thread writes its own disjoint range ----
        def insert_range(t):
            for i in range(PER_THREAD):
                st, _ = call(port, "PutItem", {"TableName": TABLE, "Item": item(t, i)})
                if st != 200:
                    return f"thread {t} PutItem {i} -> {st}"
            return None

        with ThreadPoolExecutor(max_workers=THREADS) as ex:
            errors = [e for e in ex.map(insert_range, range(THREADS)) if e]
        check(not errors, f"insert errors: {errors[:3]}")
        print("  concurrent insert phase complete")

        # ---- Concurrent reads validate every record is present and accurate ----
        def verify_range(t):
            for i in range(PER_THREAD):
                st, body = call(port, "GetItem", {"TableName": TABLE, "Key": key(rec_id(t, i))})
                if body.get("Item") != item(t, i):
                    return f"thread {t} record {i} mismatch: {body.get('Item')}"
            return None

        with ThreadPoolExecutor(max_workers=THREADS) as ex:
            errors = [e for e in ex.map(verify_range, range(THREADS)) if e]
        check(not errors, f"verify errors: {errors[:3]}")

        # Global consistency: exactly TOTAL distinct ids, no loss/dup under contention.
        ids = scan_ids(port)
        check(len(ids) == TOTAL, f"expected {TOTAL} records, scan saw {len(ids)}")
        check(len(set(ids)) == TOTAL, "scan returned duplicate ids under concurrency")
        print("  concurrent read/scan consistency verified")

        # ---- Concurrent deletes of even seqs, then re-validate ----
        def delete_evens(t):
            for i in range(0, PER_THREAD, 2):
                st, _ = call(port, "DeleteItem", {"TableName": TABLE, "Key": key(rec_id(t, i))})
                if st != 200:
                    return f"thread {t} DeleteItem {i} -> {st}"
            return None

        with ThreadPoolExecutor(max_workers=THREADS) as ex:
            errors = [e for e in ex.map(delete_evens, range(THREADS)) if e]
        check(not errors, f"delete errors: {errors[:3]}")

        ids = scan_ids(port)
        remaining = THREADS * (PER_THREAD // 2)
        check(len(ids) == remaining, f"after concurrent deletes expected {remaining}, saw {len(ids)}")
        # No even-seq id should remain; all odd-seq ids should.
        bad = [rid for rid in ids if int(rid.split("#")[1]) % 2 == 0]
        check(not bad, f"deleted (even) ids still present: {bad[:5]}")
        print("  concurrent delete consistency verified")

    except Fail as e:
        print(f"\nFAILED after {checks} checks: {e}", file=sys.stderr)
        if os.path.isfile(log_path):
            print("---- server log tail ----", file=sys.stderr)
            with open(log_path) as f:
                sys.stderr.write("".join(f.readlines()[-40:]))
        return 1
    finally:
        if proc is not None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
        if not keep:
            shutil.rmtree(data_dir, ignore_errors=True)

    print(f"\nALL CHECKS PASSED ({checks} assertions; {TOTAL} records, {THREADS} concurrent clients)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
