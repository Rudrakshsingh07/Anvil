from __future__ import annotations

import json
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from dsa_editor.bridge import (
    StateClient,
    StateHub,
    StateServer,
    read_state_file,
    write_state_file,
)


class StateHubTests(unittest.TestCase):
    def test_publish_and_latest(self) -> None:
        hub = StateHub()
        first = hub.publish({"value": 1})
        self.assertEqual(first["seq"], 1)
        self.assertEqual(first["schema"], "dsa-editor/state")
        self.assertEqual(hub.latest()["value"], 1)
        second = hub.publish({"value": 2})
        self.assertEqual(second["seq"], 2)
        self.assertEqual(hub.latest()["seq"], 2)

    def test_wait_for_new_state(self) -> None:
        hub = StateHub()
        hub.publish({"value": 1})

        def publish_later() -> None:
            time.sleep(0.15)
            hub.publish({"value": 2})

        thread = threading.Thread(target=publish_later)
        thread.start()
        started = time.monotonic()
        state = hub.latest(wait_since=1, timeout=2.0)
        elapsed = time.monotonic() - started
        thread.join()
        self.assertEqual(state["value"], 2)
        self.assertGreaterEqual(elapsed, 0.1)

    def test_close(self) -> None:
        hub = StateHub()
        hub.close()
        self.assertTrue(hub.closed)
        self.assertEqual(hub.latest()["status"], "closed")


class StateFileTests(unittest.TestCase):
    def test_write_and_read(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "state.json"
            state = {"seq": 3, "hello": "wörld"}
            written = write_state_file(path, state)
            self.assertGreater(written, 0)
            self.assertEqual(read_state_file(path), state)


class StateServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.hub = StateHub()
        self.hub.publish(
            {
                "file": {"name": "demo.py", "dirty": False},
                "stats": {"words": 3},
                "undo": {
                    "undo_stack": {"depth": 2, "entries": []},
                    "redo_stack": {"depth": 0, "entries": []},
                },
                "clipboard": {"size": 0, "entries": []},
                "trie": {"stats": {"nodes": 10}},
            }
        )
        self.server = StateServer(
            self.hub,
            port=0,
            trie_provider=lambda depth, limit: {
                "stats": {"nodes": 42},
                "requested": {"depth": depth, "limit": limit},
            },
        )
        self.port = self.server.start()
        self.base = f"http://127.0.0.1:{self.port}"

    def tearDown(self) -> None:
        self.server.stop()
        self.hub.close()

    def get(self, path: str) -> dict:
        with urllib.request.urlopen(self.base + path, timeout=2) as response:
            return json.loads(response.read().decode("utf-8"))

    def test_health(self) -> None:
        health = self.get("/health")
        self.assertTrue(health["ok"])
        self.assertEqual(health["port"], self.port)

    def test_state(self) -> None:
        state = self.get("/state")
        self.assertEqual(state["file"]["name"], "demo.py")
        self.assertEqual(state["stats"]["words"], 3)

    def test_subsets(self) -> None:
        undo = self.get("/undo")
        self.assertEqual(undo["undo"]["undo_stack"]["depth"], 2)
        redo = self.get("/redo")
        self.assertEqual(redo["redo"]["depth"], 0)
        stats = self.get("/stats")
        self.assertEqual(stats["stats"]["words"], 3)

    def test_trie_provider(self) -> None:
        trie = self.get("/trie?depth=6&limit=123")
        self.assertEqual(trie["stats"]["nodes"], 42)
        self.assertEqual(trie["requested"], {"depth": 6, "limit": 123})

    def test_client(self) -> None:
        client = StateClient(port=self.port)
        self.assertTrue(client.health()["ok"])
        self.assertEqual(client.state()["stats"]["words"], 3)
        self.assertEqual(client.trie(depth=4)["stats"]["nodes"], 42)

    def test_not_found(self) -> None:
        with self.assertRaises(urllib.error.HTTPError) as context:
            urllib.request.urlopen(self.base + "/nope", timeout=2)
        error = context.exception
        self.assertEqual(error.code, 404)
        error.close()


if __name__ == "__main__":
    unittest.main()
