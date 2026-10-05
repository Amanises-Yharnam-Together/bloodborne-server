"""Service-level tests for bb_dummy_server: start it on a free port and talk HTTP to it."""

import json
import os
import socket
import subprocess
import sys
import time
import unittest
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "..", "bb_dummy_server.py")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class ServiceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = free_port()
        cls.proc = subprocess.Popen(
            [sys.executable, "-u", SERVER, "--address", "127.0.0.1", "--port", str(cls.port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 10
        while time.time() < deadline:
            try:
                urllib.request.urlopen("http://127.0.0.1:{}/status".format(cls.port), timeout=1)
                return
            except OSError:
                time.sleep(0.1)
        cls.proc.kill()
        raise RuntimeError("server did not start")

    @classmethod
    def tearDownClass(cls):
        cls.proc.kill()
        cls.proc.wait()

    def post(self, path, body):
        req = urllib.request.Request("http://127.0.0.1:{}{}".format(self.port, path),
                                     data=json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())

    def sync(self, user_id, count):
        return self.post("/basic_utils/sync_chara_id?user_id={}".format(user_id),
                         {"CharaIdNum": count, "MessageId": "SyncCharaIdRequest",
                          "SessionId": "s", "UserId": user_id})

    # A save that meets a new account asks for fresh character ids. Without them in
    # PublishCharacterIdList the game shows "Failed to reset online character data" forever.
    def test_sync_chara_id_publishes_requested_ids(self):
        resp = self.sync(3841171, 1)
        self.assertEqual(resp["MessageId"], "SyncCharaIdResponse")
        self.assertEqual(resp["ResKind"], 0)
        ids = resp["PublishCharacterIdList"]
        self.assertEqual(len(ids), 1)
        self.assertIsInstance(ids[0]["PublishCharaId"], int)

    def test_sync_chara_id_is_stable_and_unique_per_user(self):
        a1 = [e["PublishCharaId"] for e in self.sync(1001, 2)["PublishCharacterIdList"]]
        a2 = [e["PublishCharaId"] for e in self.sync(1001, 2)["PublishCharacterIdList"]]
        b = [e["PublishCharaId"] for e in self.sync(1002, 2)["PublishCharacterIdList"]]
        self.assertEqual(a1, a2)
        self.assertEqual(len(set(a1)), 2)
        self.assertFalse(set(a1) & set(b))


if __name__ == "__main__":
    unittest.main()
