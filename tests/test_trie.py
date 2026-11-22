from __future__ import annotations

import unittest

from dsa_editor.structures.trie import Trie, current_prefix


class TrieTests(unittest.TestCase):
    def test_insert_contains_prefix(self) -> None:
        trie = Trie()
        self.assertTrue(trie.insert("print", weight=10, kind="keyword"))
        self.assertFalse(trie.insert("print", weight=20, kind="keyword"))
        self.assertTrue(trie.contains("print"))
        self.assertFalse(trie.contains("prin"))
        self.assertTrue(trie.starts_with("pri"))
        self.assertFalse(trie.starts_with("xyz"))
        self.assertEqual(trie.word_count, 1)
        self.assertGreater(trie.node_count, 1)

    def test_completion_ranking(self) -> None:
        trie = Trie()
        trie.insert("print", weight=80, kind="keyword")
        trie.insert("private", weight=45, kind="common")
        trie.insert("priority", weight=10, kind="word")
        words = trie.complete("pri", limit=10)
        self.assertEqual([word for word, _, _ in words], ["print", "private", "priority"])
        self.assertEqual(words[0][2], "keyword")

    def test_learn_text_only_adds_new_words(self) -> None:
        trie = Trie()
        added = trie.learn_text("alpha beta alpha gamma_1 _hidden 123")
        self.assertEqual(added, 4)  # alpha, beta, gamma_1, _hidden
        self.assertTrue(trie.contains("alpha"))
        self.assertFalse(trie.contains("123"))
        self.assertEqual(trie.learn_text("alpha beta"), 0)
        self.assertEqual(trie.word_count, 4)

    def test_bump(self) -> None:
        trie = Trie()
        trie.insert("print", weight=5)
        self.assertTrue(trie.bump("print", 20))
        self.assertEqual(trie.weight_of("print"), 25)
        self.assertFalse(trie.bump("missing"))

    def test_remove_prunes_branches(self) -> None:
        trie = Trie()
        trie.insert("cat")
        trie.insert("car")
        self.assertTrue(trie.remove("cat"))
        self.assertFalse(trie.contains("cat"))
        self.assertTrue(trie.contains("car"))
        self.assertEqual(trie.word_count, 1)
        self.assertFalse(trie.remove("cat"))

    def test_invalid_words_are_ignored(self) -> None:
        trie = Trie()
        self.assertFalse(trie.insert(""))
        self.assertFalse(trie.insert("has space"))
        self.assertFalse(trie.insert("x" * 100))
        self.assertEqual(trie.word_count, 0)

    def test_to_dict(self) -> None:
        trie = Trie()
        trie.insert("print", weight=80, kind="keyword")
        trie.insert("private", weight=45, kind="common")
        trie.insert("priority", weight=10, kind="word")
        data = trie.to_dict(prefix="pri", max_depth=10, max_nodes=50)
        self.assertEqual(data["stats"]["words"], 3)
        self.assertGreater(data["stats"]["nodes"], 3)
        self.assertEqual(data["prefix"], "pri")
        self.assertEqual(len(data["suggestions"]), 3)
        self.assertEqual(data["suggestions"][0]["word"], "print")
        self.assertIn("p", data["tree"]["children"])
        self.assertIn("top_words", data)
        self.assertFalse(data["tree_meta"]["truncated"])

    def test_to_dict_truncation(self) -> None:
        trie = Trie()
        for index in range(100):
            trie.insert(f"word{index:03d}", weight=index)
        data = trie.to_dict(max_depth=5, max_nodes=10)
        self.assertTrue(data["tree_meta"]["truncated"])
        self.assertEqual(data["stats"]["words"], 100)

    def test_current_prefix(self) -> None:
        self.assertEqual(current_prefix("hello pri"), "pri")
        self.assertEqual(current_prefix("hello."), "")
        self.assertEqual(current_prefix("x1 + fo"), "fo")
        self.assertEqual(current_prefix("_private"), "_private")
        self.assertEqual(current_prefix("123"), "")


if __name__ == "__main__":
    unittest.main()
