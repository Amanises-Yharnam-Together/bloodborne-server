"""Service-level tests for bb_dummy_server: start it on a free port and talk HTTP to it."""

import base64
import json
import os
import re
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

    # A standard chalice's ritual asks for the one dungeon the server keeps for that chalice
    # (ChannelSearch, FixedOrGeneral 2, GetCount 1) and gives up without one: "Failed to
    # contribute ritual materials", then back to the title (rig 2026-10-17).
    PTHUMERU_SEARCH = {"FixedOrGeneralList": [{"FixedOrGeneral": 2}], "FormDataVersion": 0,
                       "GetCount": 1, "HolyGrailTypeId": 0, "MessageId": "ChannelSearchRequest",
                       "RitualLevel": 1, "SessionId": "s", "Status": 1, "SubFeatureFlag": 256,
                       "UnlockedFlagList": [{"UnlockedFlag": 0}, {"UnlockedFlag": 1},
                                            {"UnlockedFlag": 0}], "UserId": 1}
    # The Pthumeru Chalice's dungeon: depth 1 in Pthumeru, no rites, layout 0, seed 0, the
    # chalice (goods 6109) and its layers; the record the rig's ritual loaded.
    PTHUMERU_FORM = ("1d0a000000000000000000001d0a0000000017dd" + "ff" * 15 + "23" + "ff" * 7 +
                     "32" + "ff" * 48 + "00" * 32)

    def test_standard_chalice_search_finds_its_dungeon(self):
        found = self.post("/channel/search?user_id=1", self.PTHUMERU_SEARCH)["ChannelList"]
        self.assertEqual(len(found), 1)
        dungeon = found[0]
        self.assertEqual(base64.b64decode(dungeon["FormData"]).hex(), self.PTHUMERU_FORM)
        self.assertEqual((dungeon["RitualLevel"], dungeon["HolyGrailTypeId"],
                          dungeon["FixedOrGeneral"], dungeon["Status"]), (1, 0, 2, 1))

    def test_standard_chalice_dungeon_details(self):
        found = self.post("/channel/search?user_id=1", self.PTHUMERU_SEARCH)["ChannelList"]
        details = self.post("/channel/get_details_info?user_id=1",
                            {"ChannelIdList": [{"ChannelId": found[0]["ChannelId"]}],
                             "MessageId": "ChannelGetDetailsInfoRequest", "SessionId": "s",
                             "UserId": 1})
        self.assertEqual(details["ChannelList"], found)
        self.assertEqual(details["LostChannelIdList"], [])

    def test_other_searches_find_no_made_up_dungeon(self):
        # Chalice Dungeon Search (FixedOrGeneral 1, other players' dungeons) and an unknown
        # chalice stay empty.
        other = dict(self.PTHUMERU_SEARCH, FixedOrGeneralList=[{"FixedOrGeneral": 1}])
        self.assertEqual(self.post("/channel/search?user_id=1", other).get("ChannelList", []), [])
        unknown = dict(self.PTHUMERU_SEARCH, RitualLevel=9)
        self.assertEqual(self.post("/channel/search?user_id=1", unknown).get("ChannelList", []), [])


class SsInfoTest(unittest.TestCase):
    """ss.info drives how often the game polls for summons; retail's 30-35 s makes every
    (re)join take a minute or more, which defeats seamless auto-rejoin."""

    def fetch_ss_info(self, extra_args):
        port = free_port()
        proc = subprocess.Popen(
            [sys.executable, "-u", SERVER, "--address", "127.0.0.1", "--port", str(port)] + extra_args,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.time() + 10
            while True:
                try:
                    with urllib.request.urlopen("http://127.0.0.1:{}/bb-eu/ss.info".format(port), timeout=1) as r:
                        return base64.b64decode(r.read()).decode("utf-8")
                except OSError:
                    if time.time() > deadline:
                        raise
                    time.sleep(0.1)
        finally:
            proc.kill()
            proc.wait()

    @staticmethod
    def tag(xml, name):
        match = re.search(r"<{0}>([^<]*)</{0}>".format(name), xml)
        return match.group(1) if match else None

    def test_summon_polling_is_fast_by_default(self):
        xml = self.fetch_ss_info([])
        self.assertEqual(self.tag(xml, "SummonDataCreateInterval2"), "5")
        self.assertEqual(self.tag(xml, "SummonDataGetListInterval2"), "5")

    def test_coop_guard_timers_do_not_stall_rejoin(self):
        # After a session ends the host waits out these guard timers (retail 60 s) before it
        # takes a new sign, so a guest who died could not rejoin for over a minute.
        xml = self.fetch_ss_info([])
        self.assertEqual(self.tag(xml, "SummonDataGetListGuardTimer2"), "5")
        self.assertEqual(self.tag(xml, "SummonDataGetListCoopGuardTimerClampTime2"), "5")
        # Invasion timers are not ours to touch.
        self.assertEqual(self.tag(xml, "SummonDataGetListInvationGuardTimerClampTime2"), "0")

    def test_summon_polling_interval_is_configurable(self):
        xml = self.fetch_ss_info(["--summon-poll", "12"])
        self.assertEqual(self.tag(xml, "SummonDataCreateInterval2"), "12")
        self.assertEqual(self.tag(xml, "SummonDataGetListInterval2"), "12")
        # Everything else in the template is left alone.
        self.assertEqual(self.tag(xml, "ReloadServerStatusInfoInterval2"), "300")


if __name__ == "__main__":
    unittest.main()
