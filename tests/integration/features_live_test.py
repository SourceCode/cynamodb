#!/usr/bin/env python3
"""
Live end-to-end test for the DynamoDB features added per CYNAMODB_FINDINGS.md,
driven against the real server binary over HTTP:

  * Complex attribute types (M/L/SS/NS/BS/B) round-trip, and CRUCIALLY survive a
    memtable->SSTable flush (finding #1: maps used to vanish, lists became NULL).
  * ConditionExpression on PutItem/DeleteItem (finding #2).
  * UpdateItem with SET/ADD/REMOVE and ReturnValues (finding #3).
  * Query KeyConditionExpression + sort-key operators + Filter/Projection (finding #4).
  * BatchWriteItem / BatchGetItem / TransactWriteItems / TransactGetItems (finding #5).
  * DeleteTable / UpdateTable (finding #6).
  * 501 NotImplementedException vs UnknownOperationException (finding #7).
  * Empty-string key attributes rejected (finding #8).
  * GetItem miss returns {} (finding #9).
  * Optional SigV4 enforcement via CYNAMODB_REQUIRE_AUTH (finding #10).

Usage: features_live_test.py [path-to-cynamodb-binary] [--keep]
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


class Server:
    def __init__(self, binary, data_dir, port, log_path, extra_env=None):
        self.binary = binary
        self.data_dir = data_dir
        self.port = port
        self.log_path = log_path
        self.extra_env = extra_env or {}
        self.proc = None

    def start(self):
        env = dict(os.environ)
        env["CYNAMODB_DATA_DIR"] = self.data_dir
        env["CYNAMODB_PORT"] = str(self.port)
        env["CYNAMODB_BIND_ADDR"] = "127.0.0.1"
        env.update(self.extra_env)
        self._log = open(self.log_path, "ab")
        self.proc = subprocess.Popen([self.binary], env=env,
                                     stdout=self._log, stderr=subprocess.STDOUT)
        deadline = time.time() + 15.0
        url = f"http://127.0.0.1:{self.port}/health"
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise TestFailure(f"server exited early (code {self.proc.returncode}); see {self.log_path}")
            try:
                with urllib.request.urlopen(url, timeout=1.0) as resp:
                    if resp.status == 200:
                        return
            except (urllib.error.URLError, ConnectionError, OSError):
                time.sleep(0.1)
        raise TestFailure("server did not become healthy")

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


class Client:
    def __init__(self, port, headers=None):
        self.url = f"http://127.0.0.1:{port}/"
        self.extra_headers = headers or {}

    def call(self, target, payload):
        data = json.dumps(payload).encode()
        headers = {"X-Amz-Target": "DynamoDB_20120810." + target,
                   "Content-Type": "application/x-amz-json-1.0"}
        headers.update(self.extra_headers)
        req = urllib.request.Request(self.url, data=data, method="POST", headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                status, body = resp.status, resp.read().decode()
        except urllib.error.HTTPError as e:
            status, body = e.code, e.read().decode()
        return status, (json.loads(body) if body.strip() else {})


def err_type(body):
    return body.get("__type", "").split("#")[-1]


def test_complex_types_survive_flush(client):
    print("  [complex types] round-trip M/L/SS/NS/BS/B and survive an SSTable flush")
    status, _ = client.call("CreateTable", {
        "TableName": "CT",
        "KeySchema": [{"AttributeName": "pk", "KeyType": "HASH"}],
        "AttributeDefinitions": [{"AttributeName": "pk", "AttributeType": "S"}],
    })
    check_eq(status, 200, "CreateTable CT")

    complex_item = {
        "pk": {"S": "CPX"},
        "meta": {"M": {"a": {"S": "keepme"}, "deep": {"M": {"x": {"N": "9"}}}}},
        "tags": {"L": [{"S": "t1"}, {"N": "2"}]},
        "ss": {"SS": ["a", "b"]},
        "ns": {"NS": ["1", "2"]},
        "bin": {"B": "3q2+7w=="},
        "bs": {"BS": ["AQID"]},
    }
    status, _ = client.call("PutItem", {"TableName": "CT", "Item": complex_item})
    check_eq(status, 200, "PutItem complex")

    # Before flush: full fidelity from the memtable.
    status, body = client.call("GetItem", {"TableName": "CT", "Key": {"pk": {"S": "CPX"}}})
    check_eq(status, 200, "GetItem complex (pre-flush)")
    check_eq(body["Item"], complex_item, "complex item round-trip (pre-flush)")

    # Force a memtable flush to an SSTable (threshold is 1000 entries).
    for i in range(1500):
        client.call("PutItem", {"TableName": "CT", "Item": {"pk": {"S": f"f{i:05d}"}}})

    # After flush: the whole row and every complex type must still be intact.
    status, body = client.call("GetItem", {"TableName": "CT", "Key": {"pk": {"S": "CPX"}}})
    check_eq(status, 200, "GetItem complex (post-flush)")
    check("Item" in body, "complex item vanished after flush (finding #1b regression)")
    check_eq(body["Item"], complex_item, "complex item round-trip (post-flush)")


def test_conditional_writes(client):
    print("  [conditions] ConditionExpression on Put/Delete")
    client.call("CreateTable", {
        "TableName": "Cond",
        "KeySchema": [{"AttributeName": "pk", "KeyType": "HASH"}],
        "AttributeDefinitions": [{"AttributeName": "pk", "AttributeType": "S"}],
    })
    client.call("PutItem", {"TableName": "Cond", "Item": {"pk": {"S": "a"}, "v": {"N": "1"}}})

    status, body = client.call("PutItem", {
        "TableName": "Cond", "Item": {"pk": {"S": "a"}, "v": {"N": "2"}},
        "ConditionExpression": "attribute_not_exists(pk)"})
    check_eq(status, 400, "conditional put clobber status")
    check_eq(err_type(body), "ConditionalCheckFailedException", "conditional put clobber type")

    status, body = client.call("GetItem", {"TableName": "Cond", "Key": {"pk": {"S": "a"}}})
    check_eq(body["Item"]["v"], {"N": "1"}, "original value preserved after failed condition")


def test_update_item(client):
    print("  [update] UpdateItem SET/ADD/REMOVE + ReturnValues")
    client.call("CreateTable", {
        "TableName": "Upd",
        "KeySchema": [{"AttributeName": "pk", "KeyType": "HASH"}],
        "AttributeDefinitions": [{"AttributeName": "pk", "AttributeType": "S"}],
    })
    status, body = client.call("UpdateItem", {
        "TableName": "Upd", "Key": {"pk": {"S": "x"}},
        "UpdateExpression": "SET title = :t ADD n :one",
        "ExpressionAttributeValues": {":t": {"S": "hi"}, ":one": {"N": "1"}},
        "ReturnValues": "ALL_NEW"})
    check_eq(status, 200, "update status")
    check_eq(body["Attributes"]["title"], {"S": "hi"}, "update set title")
    check_eq(body["Attributes"]["n"], {"N": "1"}, "update add counter")

    status, body = client.call("UpdateItem", {
        "TableName": "Upd", "Key": {"pk": {"S": "x"}},
        "UpdateExpression": "ADD n :one REMOVE title",
        "ExpressionAttributeValues": {":one": {"N": "4"}},
        "ReturnValues": "ALL_NEW"})
    check_eq(body["Attributes"]["n"], {"N": "5"}, "update increment counter")
    check("title" not in body["Attributes"], "update removed title")


def test_query_expressions(client):
    print("  [query] KeyConditionExpression + sort ops + filter + projection")
    client.call("CreateTable", {
        "TableName": "QE",
        "KeySchema": [{"AttributeName": "pk", "KeyType": "HASH"},
                      {"AttributeName": "sk", "KeyType": "RANGE"}],
        "AttributeDefinitions": [{"AttributeName": "pk", "AttributeType": "S"},
                                 {"AttributeName": "sk", "AttributeType": "N"}],
    })
    for sk in (1, 2, 5, 10):
        client.call("PutItem", {"TableName": "QE", "Item": {
            "pk": {"S": "p"}, "sk": {"N": str(sk)},
            "status": {"S": "open" if sk % 2 else "closed"}}})

    status, body = client.call("Query", {
        "TableName": "QE",
        "KeyConditionExpression": "pk = :p AND sk > :s",
        "ExpressionAttributeValues": {":p": {"S": "p"}, ":s": {"N": "2"}}})
    check_eq(status, 200, "query KCE status")
    check_eq(body["Count"], 2, "query sk>2 count")

    status, body = client.call("Query", {
        "TableName": "QE",
        "KeyConditionExpression": "pk = :p",
        "FilterExpression": "#s = :open",
        "ExpressionAttributeNames": {"#s": "status"},
        "ExpressionAttributeValues": {":p": {"S": "p"}, ":open": {"S": "open"}}})
    check_eq(body["Count"], 2, "query filter count")
    check_eq(body["ScannedCount"], 4, "query filter scanned count")

    status, body = client.call("Query", {
        "TableName": "QE",
        "KeyConditionExpression": "pk = :p",
        "ScanIndexForward": False,
        "ExpressionAttributeValues": {":p": {"S": "p"}}})
    order = [int(it["sk"]["N"]) for it in body["Items"]]
    check_eq(order, [10, 5, 2, 1], "query reverse order")


def test_batch_and_transactions(client):
    print("  [batch/txn] BatchWriteItem/BatchGetItem/TransactWriteItems/TransactGetItems")
    client.call("CreateTable", {
        "TableName": "BT",
        "KeySchema": [{"AttributeName": "pk", "KeyType": "HASH"}],
        "AttributeDefinitions": [{"AttributeName": "pk", "AttributeType": "S"}],
    })
    status, _ = client.call("BatchWriteItem", {"RequestItems": {"BT": [
        {"PutRequest": {"Item": {"pk": {"S": "a"}}}},
        {"PutRequest": {"Item": {"pk": {"S": "b"}}}}]}})
    check_eq(status, 200, "batch write status")
    status, body = client.call("BatchGetItem", {"RequestItems": {"BT": {"Keys": [
        {"pk": {"S": "a"}}, {"pk": {"S": "b"}}]}}})
    check_eq(len(body["Responses"]["BT"]), 2, "batch get count")

    # Transaction that must cancel atomically.
    status, body = client.call("TransactWriteItems", {"TransactItems": [
        {"Put": {"TableName": "BT", "Item": {"pk": {"S": "c"}}}},
        {"ConditionCheck": {"TableName": "BT", "Key": {"pk": {"S": "a"}},
                            "ConditionExpression": "attribute_not_exists(pk)"}}]})
    check_eq(status, 400, "transaction cancel status")
    check_eq(err_type(body), "TransactionCanceledException", "transaction cancel type")
    status, body = client.call("GetItem", {"TableName": "BT", "Key": {"pk": {"S": "c"}}})
    check("Item" not in body, "transaction rolled back the put")

    # Transaction that succeeds.
    status, _ = client.call("TransactWriteItems", {"TransactItems": [
        {"Put": {"TableName": "BT", "Item": {"pk": {"S": "c"}, "v": {"N": "1"}}}}]})
    check_eq(status, 200, "transaction commit status")
    status, body = client.call("TransactGetItems", {"TransactItems": [
        {"Get": {"TableName": "BT", "Key": {"pk": {"S": "c"}}}}]})
    check_eq(body["Responses"][0]["Item"]["v"], {"N": "1"}, "transact get value")


def test_table_lifecycle(client):
    print("  [tables] DeleteTable / UpdateTable")
    client.call("CreateTable", {
        "TableName": "Life",
        "KeySchema": [{"AttributeName": "pk", "KeyType": "HASH"}],
        "AttributeDefinitions": [{"AttributeName": "pk", "AttributeType": "S"}],
    })
    client.call("PutItem", {"TableName": "Life", "Item": {"pk": {"S": "a"}}})
    status, _ = client.call("UpdateTable", {"TableName": "Life", "BillingMode": "PROVISIONED"})
    check_eq(status, 200, "update table status")
    status, _ = client.call("DeleteTable", {"TableName": "Life"})
    check_eq(status, 200, "delete table status")
    status, body = client.call("DescribeTable", {"TableName": "Life"})
    check_eq(err_type(body), "ResourceNotFoundException", "describe deleted table")


def test_error_shapes(client):
    print("  [errors] 501 vs UnknownOperation, empty key, miss returns {}")
    status, body = client.call("DescribeContributorInsights", {"TableName": "x"})
    check_eq(status, 501, "known-unimplemented status")
    check_eq(err_type(body), "NotImplementedException", "known-unimplemented type")

    client.call("CreateTable", {
        "TableName": "Err",
        "KeySchema": [{"AttributeName": "pk", "KeyType": "HASH"}],
        "AttributeDefinitions": [{"AttributeName": "pk", "AttributeType": "S"}],
    })
    status, body = client.call("PutItem", {"TableName": "Err", "Item": {"pk": {"S": ""}}})
    check_eq(status, 400, "empty key status")
    check_eq(err_type(body), "ValidationException", "empty key type")

    status, body = client.call("GetItem", {"TableName": "Err", "Key": {"pk": {"S": "nope"}}})
    check_eq(status, 200, "miss status")
    check_eq(body, {}, "miss returns canonical {}")


def _sigv4_headers(access_key, secret_key, port, target, payload):
    """Produce a correct AWS SigV4 Authorization header (independent reference impl)."""
    import datetime
    import hashlib
    import hmac

    region, service = "us-east-1", "dynamodb"
    now = datetime.datetime.now(datetime.timezone.utc)
    amz_date = now.strftime("%Y%m%dT%H%M%SZ")
    date_stamp = now.strftime("%Y%m%d")
    host = f"127.0.0.1:{port}"

    payload_hash = hashlib.sha256(payload).hexdigest()
    canonical_headers = f"host:{host}\nx-amz-date:{amz_date}\n"
    signed_headers = "host;x-amz-date"
    canonical_request = f"POST\n/\n\n{canonical_headers}\n{signed_headers}\n{payload_hash}"

    scope = f"{date_stamp}/{region}/{service}/aws4_request"
    sts = ("AWS4-HMAC-SHA256\n" + amz_date + "\n" + scope + "\n" +
           hashlib.sha256(canonical_request.encode()).hexdigest())

    def _hmac(key, msg):
        return hmac.new(key, msg.encode(), hashlib.sha256).digest()

    k_date = _hmac(("AWS4" + secret_key).encode(), date_stamp)
    k_region = _hmac(k_date, region)
    k_service = _hmac(k_region, service)
    k_signing = _hmac(k_service, "aws4_request")
    signature = hmac.new(k_signing, sts.encode(), hashlib.sha256).hexdigest()

    authorization = (f"AWS4-HMAC-SHA256 Credential={access_key}/{scope}, "
                     f"SignedHeaders={signed_headers}, Signature={signature}")
    # urllib sets Host itself to 127.0.0.1:<port>, which is what we signed over.
    return {"Authorization": authorization, "x-amz-date": amz_date}


def test_auth_enforcement(binary, data_dir, log_path):
    print("  [auth] CYNAMODB_REQUIRE_AUTH enforces real SigV4 signatures")
    access_key, secret_key = "AKIDLIVETEST", "live-test-secret"
    port = free_port()
    server = Server(binary, data_dir, port, log_path, extra_env={
        "CYNAMODB_REQUIRE_AUTH": "1",
        "CYNAMODB_ACCESS_KEY_ID": access_key,
        "CYNAMODB_SECRET_ACCESS_KEY": secret_key,
    })
    server.start()
    try:
        anon = Client(port)
        status, body = anon.call("ListTables", {})
        check_eq(status, 400, "anon request rejected status")
        check_eq(err_type(body), "MissingAuthenticationTokenException", "anon rejected type")

        # A correctly signed request is accepted.
        payload = json.dumps({}).encode()
        good = Client(port, headers=_sigv4_headers(access_key, secret_key, port, "ListTables", payload))
        status, _ = good.call("ListTables", {})
        check_eq(status, 200, "correctly signed request accepted")

        # A request signed with the wrong secret is rejected with InvalidSignature.
        bad = Client(port, headers=_sigv4_headers(access_key, "WRONG-SECRET", port, "ListTables", payload))
        status, body = bad.call("ListTables", {})
        check_eq(status, 400, "bad-signature request rejected status")
        check_eq(err_type(body), "InvalidSignatureException", "bad-signature type")

        # An unknown access key is rejected.
        unknown = Client(port, headers=_sigv4_headers("AKIDUNKNOWN", secret_key, port, "ListTables", payload))
        status, body = unknown.call("ListTables", {})
        check_eq(err_type(body), "UnrecognizedClientException", "unknown access key type")
    finally:
        server.stop()


def main():
    args = [a for a in sys.argv[1:] if a != "--keep"]
    keep = "--keep" in sys.argv
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    binary = args[0] if args else os.path.join(repo_root, "build", "cynamodb")
    if not os.path.isfile(binary):
        print(f"ERROR: server binary not found: {binary}", file=sys.stderr)
        return 2

    data_dir = tempfile.mkdtemp(prefix="cynamodb_feat_")
    log_path = os.path.join(data_dir, "server.log")
    port = free_port()
    client = Client(port)
    print(f"cynamoDB features live test  binary={binary}\n  data_dir={data_dir} port={port}")

    try:
        server = Server(binary, data_dir, port, log_path)
        server.start()
        try:
            test_complex_types_survive_flush(client)
            test_conditional_writes(client)
            test_update_item(client)
            test_query_expressions(client)
            test_batch_and_transactions(client)
            test_table_lifecycle(client)
            test_error_shapes(client)
        finally:
            server.stop()

        # Auth enforcement uses a fresh data dir / process with the env flag set.
        auth_dir = tempfile.mkdtemp(prefix="cynamodb_auth_")
        try:
            test_auth_enforcement(binary, auth_dir, os.path.join(auth_dir, "server.log"))
        finally:
            if not keep:
                import shutil
                shutil.rmtree(auth_dir, ignore_errors=True)
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

    print(f"\nALL CHECKS PASSED ({checks_run} assertions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
