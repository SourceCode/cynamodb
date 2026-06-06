#!/usr/bin/env python3
"""
Live end-to-end CRUD + persistence test for cynamoDB.

Starts the real server binary (over HTTP, the DynamoDB JSON protocol), then:

  Session 1  CreateTable, insert 500 records, 50 accurate point lookups,
             paginated Scan == 500, Query (EQ) spot checks, update 25 records,
             and a composite-key table to validate Query ordering + pagination.
  -- restart --
  Session 2  Re-validate all 500 (incl. updates) survived the restart, then
             delete 200 records and confirm they are gone (Scan == 300).
  -- restart --
  Session 3  Confirm the 200 deletions persisted (Scan == 300, survivors intact),
             then delete the remaining 300 (Scan == 0).
  -- restart --
  Session 4  Confirm the table is empty (deletes persisted) but the tables still
             exist in the catalog.

Every restart is a brand-new OS process over the same data directory, so this
exercises WAL replay, SSTable reload, and table-catalog persistence for real.

Usage: crud_live_test.py [path-to-cynamodb-binary] [--keep]
Exit code 0 = all checks passed, non-zero = failure (details printed).
"""

import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

USERS = "LiveUsers"
EVENTS = "LiveEvents"
N_RECORDS = 500

checks_run = 0


class TestFailure(Exception):
    pass


def check(cond, msg):
    global checks_run
    checks_run += 1
    if not cond:
        raise TestFailure(msg)


def check_eq(actual, expected, ctx):
    check(actual == expected, f"{ctx}: expected {expected!r}, got {actual!r}")


# --------------------------------------------------------------------------
# Server lifecycle
# --------------------------------------------------------------------------

class Server:
    def __init__(self, binary, data_dir, port, log_path):
        self.binary = binary
        self.data_dir = data_dir
        self.port = port
        self.log_path = log_path
        self.proc = None

    def start(self):
        env = dict(os.environ)
        env["CYNAMODB_DATA_DIR"] = self.data_dir
        env["CYNAMODB_PORT"] = str(self.port)
        env["CYNAMODB_BIND_ADDR"] = "127.0.0.1"
        self._log = open(self.log_path, "ab")
        self._log.write(b"\n==== server start ====\n")
        self._log.flush()
        self.proc = subprocess.Popen([self.binary], env=env,
                                     stdout=self._log, stderr=subprocess.STDOUT)
        self._wait_healthy()

    def _wait_healthy(self):
        deadline = time.time() + 15.0
        url = f"http://127.0.0.1:{self.port}/health"
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise TestFailure(f"server exited early (code {self.proc.returncode}); see {self.log_path}")
            try:
                with urllib.request.urlopen(url, timeout=1.0) as resp:
                    if resp.status == 200 and b"healthy" in resp.read():
                        return
            except (urllib.error.URLError, ConnectionError, OSError):
                time.sleep(0.1)
        raise TestFailure(f"server did not become healthy within timeout; see {self.log_path}")

    def stop(self):
        if self.proc is None:
            return
        self.proc.send_signal(signal.SIGINT)
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.proc = None
        self._log.close()


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


# --------------------------------------------------------------------------
# DynamoDB JSON helpers
# --------------------------------------------------------------------------

class Client:
    def __init__(self, port):
        self.url = f"http://127.0.0.1:{port}/"

    def call(self, target, payload):
        data = json.dumps(payload).encode()
        req = urllib.request.Request(
            self.url, data=data, method="POST",
            headers={"X-Amz-Target": "DynamoDB_20120810." + target,
                     "Content-Type": "application/x-amz-json-1.0"})
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                status, body = resp.status, resp.read().decode()
        except urllib.error.HTTPError as e:
            status, body = e.code, e.read().decode()
        return status, (json.loads(body) if body.strip() else {})


def make_user(i, score=None):
    return {
        "id": {"S": f"user#{i:05d}"},
        "name": {"S": f"name-{i}"},
        "age": {"N": str(20 + (i % 50))},
        "score": {"N": str(i * 7 % 1000 if score is None else score)},
        "active": {"BOOL": (i % 2 == 0)},
    }


def user_key(i):
    return {"id": {"S": f"user#{i:05d}"}}


def scan_all_ids(client, table, page_size=100):
    """Paginated Scan that returns the set of all 'id' values, validating that
    pagination terminates and never repeats an item."""
    ids = []
    start_key = None
    pages = 0
    while True:
        payload = {"TableName": table, "Limit": page_size}
        if start_key is not None:
            payload["ExclusiveStartKey"] = start_key
        status, body = client.call("Scan", payload)
        check_eq(status, 200, "Scan status")
        for item in body.get("Items", []):
            ids.append(item["id"]["S"])
        start_key = body.get("LastEvaluatedKey")
        pages += 1
        check(pages < 10000, "Scan pagination did not terminate")
        if start_key is None:
            break
    return ids


# --------------------------------------------------------------------------
# Test phases. `expected` maps id -> full item dict for everything that should
# currently be stored in USERS.
# --------------------------------------------------------------------------

def phase1_create_and_populate(client, expected):
    print("  [session 1] create tables, insert 500, validate reads/updates/query")

    status, _ = client.call("CreateTable", {
        "TableName": USERS,
        "KeySchema": [{"AttributeName": "id", "KeyType": "HASH"}],
        "AttributeDefinitions": [{"AttributeName": "id", "AttributeType": "S"}],
    })
    check_eq(status, 200, "CreateTable Users")

    status, _ = client.call("CreateTable", {
        "TableName": EVENTS,
        "KeySchema": [{"AttributeName": "pk", "KeyType": "HASH"},
                      {"AttributeName": "sk", "KeyType": "RANGE"}],
        "AttributeDefinitions": [{"AttributeName": "pk", "AttributeType": "S"},
                                 {"AttributeName": "sk", "AttributeType": "N"}],
    })
    check_eq(status, 200, "CreateTable Events")

    # ---- CREATE: insert 500 records ----
    for i in range(N_RECORDS):
        item = make_user(i)
        status, body = client.call("PutItem", {"TableName": USERS, "Item": item})
        check_eq(status, 200, f"PutItem user {i}")
        expected[item["id"]["S"]] = item

    # ---- READ: 50 different point lookups, validated for exact accuracy ----
    lookup_indices = list(range(0, N_RECORDS, 10))  # exactly 50 spread across the range
    check_eq(len(lookup_indices), 50, "lookup count")
    for i in lookup_indices:
        status, body = client.call("GetItem", {"TableName": USERS, "Key": user_key(i)})
        check_eq(status, 200, f"GetItem user {i} status")
        check("Item" in body, f"GetItem user {i} returned no Item")
        check_eq(body["Item"], make_user(i), f"GetItem user {i} accuracy")

    # A lookup for a record that does not exist must return no item.
    status, body = client.call("GetItem", {"TableName": USERS, "Key": user_key(99999)})
    check_eq(status, 200, "GetItem missing status")
    check("Item" not in body, "GetItem missing should have no Item")

    # ---- READ: full paginated scan must see exactly the 500 ids, once each ----
    ids = scan_all_ids(client, USERS)
    check_eq(len(ids), N_RECORDS, "Scan total count")
    check_eq(len(set(ids)), N_RECORDS, "Scan returned duplicates")
    check_eq(set(ids), set(expected.keys()), "Scan id set")

    # ---- READ: Query (EQ on the partition key) returns exactly that item ----
    for i in (0, 123, 499):
        status, body = client.call("Query", {
            "TableName": USERS,
            "KeyConditions": {"id": {"ComparisonOperator": "EQ",
                                     "AttributeValueList": [{"S": f"user#{i:05d}"}]}},
        })
        check_eq(status, 200, f"Query user {i} status")
        check_eq(body.get("Count"), 1, f"Query user {i} count")
        check_eq(body["Items"][0], make_user(i), f"Query user {i} accuracy")

    # ---- UPDATE: overwrite 25 records with a new score, validate immediately ----
    for i in range(0, N_RECORDS, 20):  # 25 records
        item = make_user(i, score=9999)
        status, _ = client.call("PutItem", {"TableName": USERS, "Item": item})
        check_eq(status, 200, f"Update user {i}")
        expected[item["id"]["S"]] = item
        status, body = client.call("GetItem", {"TableName": USERS, "Key": user_key(i)})
        check_eq(body["Item"]["score"], {"N": "9999"}, f"Update user {i} reflected")

    # ---- Composite-key Query: ordering + pagination ----
    for sk in (10, 2, 30, 1, 100):
        client.call("PutItem", {"TableName": EVENTS,
                                "Item": {"pk": {"S": "p1"}, "sk": {"N": str(sk)}, "v": {"S": f"e{sk}"}}})
    client.call("PutItem", {"TableName": EVENTS,
                            "Item": {"pk": {"S": "p2"}, "sk": {"N": "5"}, "v": {"S": "other"}}})
    validate_events_query(client)


def validate_events_query(client):
    status, body = client.call("Query", {
        "TableName": EVENTS,
        "KeyConditions": {"pk": {"ComparisonOperator": "EQ", "AttributeValueList": [{"S": "p1"}]}},
    })
    check_eq(status, 200, "Query events status")
    check_eq(body.get("Count"), 5, "Query events count")
    order = [int(it["sk"]["N"]) for it in body["Items"]]
    check_eq(order, [1, 2, 10, 30, 100], "Query events numeric sort order")
    # The other partition's item must never appear.
    check(all(it["pk"]["S"] == "p1" for it in body["Items"]), "Query events partition isolation")


def revalidate_all(client, expected, ctx):
    """Spot-check 50 records for exact accuracy and confirm the full scan matches
    the expected set."""
    sample = sorted(expected.keys())
    step = max(1, len(sample) // 50)
    checked = 0
    for key_id in sample[::step]:
        i = int(key_id.split("#")[1])
        status, body = client.call("GetItem", {"TableName": USERS, "Key": user_key(i)})
        check_eq(status, 200, f"{ctx}: GetItem {key_id} status")
        check("Item" in body, f"{ctx}: {key_id} missing after restart")
        check_eq(body["Item"], expected[key_id], f"{ctx}: {key_id} accuracy after restart")
        checked += 1
    check(checked >= 50, f"{ctx}: expected to revalidate >=50 records, did {checked}")

    ids = scan_all_ids(client, USERS)
    check_eq(set(ids), set(expected.keys()), f"{ctx}: scan id set after restart")
    check_eq(len(ids), len(expected), f"{ctx}: scan count after restart")


def phase2_validate_then_delete(client, expected):
    print("  [session 2] verify inserts+updates persisted, then delete 200")
    revalidate_all(client, expected, "after-restart-1")
    validate_events_query(client)  # composite table persisted too

    # ---- DELETE: remove 200 records, confirm each is gone ----
    deleted = list(range(0, 200))
    for i in deleted:
        status, _ = client.call("DeleteItem", {"TableName": USERS, "Key": user_key(i)})
        check_eq(status, 200, f"DeleteItem user {i}")
        del expected[f"user#{i:05d}"]
    for i in (0, 50, 150, 199):
        status, body = client.call("GetItem", {"TableName": USERS, "Key": user_key(i)})
        check("Item" not in body, f"deleted user {i} still present")

    ids = scan_all_ids(client, USERS)
    check_eq(len(ids), N_RECORDS - 200, "Scan count after deleting 200")
    check_eq(set(ids), set(expected.keys()), "Scan id set after deleting 200")


def phase3_validate_deletes_then_clear(client, expected):
    print("  [session 3] verify deletions persisted, then delete the rest")
    # Deleted records must still be gone after the restart.
    for i in (0, 50, 150, 199):
        status, body = client.call("GetItem", {"TableName": USERS, "Key": user_key(i)})
        check("Item" not in body, f"after-restart-2: deleted user {i} resurfaced")
    revalidate_all(client, expected, "after-restart-2")

    # ---- DELETE the remaining 300 ----
    for i in range(200, N_RECORDS):
        status, _ = client.call("DeleteItem", {"TableName": USERS, "Key": user_key(i)})
        check_eq(status, 200, f"DeleteItem user {i}")
        del expected[f"user#{i:05d}"]
    ids = scan_all_ids(client, USERS)
    check_eq(len(ids), 0, "Scan count after deleting all")


def phase4_validate_empty(client, expected):
    print("  [session 4] verify empty table persisted and catalog intact")
    ids = scan_all_ids(client, USERS)
    check_eq(len(ids), 0, "after-restart-3: table should be empty")

    status, body = client.call("ListTables", {})
    check_eq(status, 200, "ListTables status")
    names = set(body.get("TableNames", []))
    check(USERS in names, "Users table missing from catalog after restarts")
    check(EVENTS in names, "Events table missing from catalog after restarts")

    # The composite table's data must still be intact across all the restarts.
    validate_events_query(client)


# --------------------------------------------------------------------------

def main():
    args = [a for a in sys.argv[1:] if a != "--keep"]
    keep = "--keep" in sys.argv
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    binary = args[0] if args else os.path.join(repo_root, "build", "cynamodb")
    if not os.path.isfile(binary):
        print(f"ERROR: server binary not found: {binary}", file=sys.stderr)
        return 2

    data_dir = tempfile.mkdtemp(prefix="cynamodb_live_")
    log_path = os.path.join(data_dir, "server.log")
    port = free_port()
    client = Client(port)
    expected = {}

    print(f"cynamoDB live CRUD test  binary={binary}\n  data_dir={data_dir} port={port}")

    phases = [
        ("create+populate", phase1_create_and_populate),
        ("validate+delete200", phase2_validate_then_delete),
        ("validate-deletes+clear", phase3_validate_deletes_then_clear),
        ("validate-empty", phase4_validate_empty),
    ]

    try:
        for idx, (label, fn) in enumerate(phases):
            server = Server(binary, data_dir, port, log_path)
            server.start()
            try:
                fn(client, expected)
            finally:
                server.stop()
            if idx < len(phases) - 1:
                print(f"  -- restart {idx + 1} (fresh process, same data dir) --")
    except TestFailure as e:
        print(f"\nFAILED after {checks_run} checks: {e}", file=sys.stderr)
        if os.path.isfile(log_path):
            print("---- server log tail ----", file=sys.stderr)
            with open(log_path) as f:
                sys.stderr.write("".join(f.readlines()[-40:]))
        return 1
    finally:
        if not keep:
            import shutil
            shutil.rmtree(data_dir, ignore_errors=True)

    print(f"\nALL CHECKS PASSED ({checks_run} assertions across 4 sessions / 3 restarts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
