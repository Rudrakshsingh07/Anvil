Short answer: it is not a system dictionary or an online dictionary. The suggestions come from two places:

## 1. A bundled keyword table in the repo

The seed dictionary is generated into:

```text
src/keywords_data.h
```

It is hand-maintained project data. It contains:

- 21 language lists: Python, JavaScript, TypeScript, C, C++, Java, Rust, Go, Ruby, PHP, SQL, Bash, HTML, CSS, JSON, YAML, Markdown, Lua, Kotlin, Swift, C#
- 1 common list of generic programming words (`value`, `result`, `index`, `node`, `queue`, `stack`, `trie`, …)
- 1,603 words total, of which 122 are common

`src/keywords.c` looks the language up in that table and seeds the trie. There
is no file parsing or package resource lookup at runtime, so the editor works
from any directory after `make`.

## 2. Words learned from the open document

At startup, `editor_rebuild_trie()` in `src/editor.c` seeds a fresh trie:

```c
keyword_seed_trie(&ed->trie, ed->language);
trie_learn_text(&ed->trie, ed->buf.text, 8, "word");
```

Then, while you type, `editor_learn()` calls `trie_learn_text()` at most once
per second (and again on save). It extracts identifiers with the same rule as
the C tokenizer:

```text
[A-Za-z_][A-Za-z0-9_]{1,}
```

and inserts only new words (`only_if_new = true`), so repeated text does not
keep inflating weights.

## Ranking weights

| Source | Kind | Initial weight |
|---|---|---|
| Language keywords | `keyword` | 80 |
| Common programming words | `common` | 45 |
| Identifiers learned from the document | `word` | 8 |
| Accepted ghost/completion | — | +15 each time |

Language keywords outrank generic words, which outrank document words.
Accepting a suggestion calls `trie_bump(&ed->trie, word, 15)`.

## What it is not

- No `/usr/share/dict/words`
- No WordNet, spell-checker, corpus download, or network call
- No LSP or semantic analysis
- Not persisted between runs; each editor process builds one trie per document

So the “dictionary” is a small curated programming-keyword table plus
per-document identifiers.

If you want a broader dictionary, the options are: add a large words section to
the generated table, load an OS dictionary file, ship an English/code corpus,
or index project symbols.
