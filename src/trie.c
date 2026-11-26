/* Prefix trie used for completion and the visualizer. */
#include "anvil.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TRIE_MAX_WORD 64

static TrieNode *node_new(char ch) {
    TrieNode *n = xmalloc(sizeof(TrieNode));
    memset(n, 0, sizeof(*n));
    n->ch = ch;
    return n;
}

static TrieNode *node_child(TrieNode *n, char ch) {
    for (int i = 0; i < n->nchild; i++) {
        if (n->children[i]->ch == ch) return n->children[i];
    }
    return NULL;
}

static TrieNode *node_add_child(TrieNode *n, char ch) {
    if (n->nchild == n->cap) {
        n->cap = n->cap ? n->cap * 2 : 4;
        n->children = xrealloc(n->children, sizeof(TrieNode *) * (size_t)n->cap);
    }
    TrieNode *child = node_new(ch);
    n->children[n->nchild++] = child;
    return child;
}

static void node_free(TrieNode *n) {
    if (!n) return;
    for (int i = 0; i < n->nchild; i++) node_free(n->children[i]);
    free(n->children);
    free(n);
}

void trie_init(Trie *t) {
    memset(t, 0, sizeof(*t));
    t->root = node_new('\0');
    t->node_count = 1;
    pthread_mutex_init(&t->lock, NULL);
}

void trie_free(Trie *t) {
    node_free(t->root);
    pthread_mutex_destroy(&t->lock);
    memset(t, 0, sizeof(*t));
}

static bool valid_word(const char *word) {
    if (!word || !*word) return false;
    size_t len = strlen(word);
    if (len > TRIE_MAX_WORD) return false;
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char)word[i])) return false;
    }
    return true;
}

bool trie_insert(Trie *t, const char *word, int weight, const char *kind,
                 bool only_if_new) {
    if (!valid_word(word)) return false;
    pthread_mutex_lock(&t->lock);
    TrieNode *node = t->root;
    for (const char *p = word; *p; p++) {
        TrieNode *child = node_child(node, *p);
        if (!child) {
            child = node_add_child(node, *p);
            t->node_count++;
        }
        node = child;
    }
    bool is_new = !node->terminal;
    if (is_new) {
        node->terminal = 1;
        t->word_count++;
    }
    if (only_if_new && !is_new) {
        pthread_mutex_unlock(&t->lock);
        return false;
    }
    if (weight > node->weight) node->weight = weight;
    if (kind && *kind) snprintf(node->kind, sizeof(node->kind), "%s", kind);
    pthread_mutex_unlock(&t->lock);
    return is_new;
}

bool trie_bump(Trie *t, const char *word, int amount) {
    if (!word) return false;
    if (amount < 0) amount = 0;
    pthread_mutex_lock(&t->lock);
    TrieNode *node = t->root;
    for (const char *p = word; *p; p++) {
        node = node_child(node, *p);
        if (!node) {
            pthread_mutex_unlock(&t->lock);
            return false;
        }
    }
    if (!node->terminal) {
        pthread_mutex_unlock(&t->lock);
        return false;
    }
    node->weight += amount;
    pthread_mutex_unlock(&t->lock);
    return true;
}

int trie_learn_text(Trie *t, const char *text, int weight, const char *kind) {
    if (!text) return 0;
    int added = 0;
    size_t i = 0;
    size_t len = strlen(text);
    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        if (!(isalpha(c) || c == '_')) { i++; continue; }
        size_t start = i;
        i++;
        while (i < len) {
            unsigned char d = (unsigned char)text[i];
            if (!(isalnum(d) || d == '_')) break;
            i++;
        }
        size_t n = i - start;
        if (n >= 2) {
            char word[TRIE_MAX_WORD + 1];
            if (n > TRIE_MAX_WORD) n = TRIE_MAX_WORD;
            memcpy(word, text + start, n);
            word[n] = '\0';
            if (trie_insert(t, word, weight, kind, true)) added++;
        }
    }
    return added;
}

typedef struct {
    WordEntry *items;
    int len;
    int cap;
} WordList;

static void wordlist_add(WordList *wl, const char *word, int weight, const char *kind) {
    if (wl->len == wl->cap) {
        wl->cap = wl->cap ? wl->cap * 2 : 64;
        wl->items = xrealloc(wl->items, sizeof(WordEntry) * (size_t)wl->cap);
    }
    WordEntry *e = &wl->items[wl->len++];
    snprintf(e->word, sizeof(e->word), "%s", word);
    e->weight = weight;
    snprintf(e->kind, sizeof(e->kind), "%s", kind && *kind ? kind : "word");
}

static int word_cmp(const void *a, const void *b) {
    const WordEntry *x = a;
    const WordEntry *y = b;
    if (x->weight != y->weight) return y->weight - x->weight;
    size_t lx = strlen(x->word), ly = strlen(y->word);
    if (lx != ly) return lx < ly ? -1 : 1;
    int c = strcasecmp(x->word, y->word);
    if (c) return c;
    return strcmp(x->word, y->word);
}

static void collect_node(TrieNode *node, char *prefix, size_t plen, size_t cap,
                         WordList *out) {
    if (node->terminal) wordlist_add(out, prefix, node->weight, node->kind);
    for (int i = 0; i < node->nchild; i++) {
        if (plen + 1 >= cap) continue;
        prefix[plen] = node->children[i]->ch;
        prefix[plen + 1] = '\0';
        collect_node(node->children[i], prefix, plen + 1, cap, out);
    }
}

static TrieNode *find_node(Trie *t, const char *prefix) {
    TrieNode *node = t->root;
    for (const char *p = prefix; p && *p; p++) {
        node = node_child(node, *p);
        if (!node) return NULL;
    }
    return node;
}

int trie_complete(Trie *t, const char *prefix, int limit, WordEntry *out) {
    if (!prefix || !*prefix || limit <= 0) return 0;
    pthread_mutex_lock(&t->lock);
    TrieNode *node = find_node(t, prefix);
    if (!node) {
        pthread_mutex_unlock(&t->lock);
        return 0;
    }
    WordList wl;
    memset(&wl, 0, sizeof(wl));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", prefix);
    collect_node(node, buf, strlen(buf), sizeof(buf), &wl);
    qsort(wl.items, (size_t)wl.len, sizeof(WordEntry), word_cmp);
    int n = wl.len < limit ? wl.len : limit;
    for (int i = 0; i < n; i++) out[i] = wl.items[i];
    free(wl.items);
    pthread_mutex_unlock(&t->lock);
    return n;
}

int trie_node_count(Trie *t) {
    pthread_mutex_lock(&t->lock);
    int n = t->node_count;
    pthread_mutex_unlock(&t->lock);
    return n;
}

int trie_word_count(Trie *t) {
    pthread_mutex_lock(&t->lock);
    int n = t->word_count;
    pthread_mutex_unlock(&t->lock);
    return n;
}

typedef struct {
    int nodes;
    int words;
    int max_depth;
    struct { char kind[16]; int count; } kinds[16];
    int nkinds;
} TrieStats;

static void collect_stats(TrieNode *node, int depth, TrieStats *st) {
    st->nodes++;
    if (depth > st->max_depth) st->max_depth = depth;
    if (node->terminal) {
        st->words++;
        const char *kind = node->kind[0] ? node->kind : "word";
        int found = -1;
        for (int i = 0; i < st->nkinds; i++) {
            if (strcmp(st->kinds[i].kind, kind) == 0) { found = i; break; }
        }
        if (found < 0 && st->nkinds < 16) {
            found = st->nkinds++;
            snprintf(st->kinds[found].kind, sizeof(st->kinds[found].kind), "%s", kind);
            st->kinds[found].count = 0;
        }
        if (found >= 0) st->kinds[found].count++;
    }
    for (int i = 0; i < node->nchild; i++) {
        collect_stats(node->children[i], depth + 1, st);
    }
}

static void sort_children(TrieNode *node, TrieNode **out) {
    for (int i = 0; i < node->nchild; i++) out[i] = node->children[i];
    for (int i = 1; i < node->nchild; i++) {
        TrieNode *key = out[i];
        int j = i - 1;
        while (j >= 0) {
            bool before;
            if (out[j]->weight != key->weight) before = out[j]->weight > key->weight;
            else before = (unsigned char)out[j]->ch < (unsigned char)key->ch;
            if (before) break;
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
}

static void emit_tree(TrieNode *node, const char *ch, int depth,
                      int max_depth, int *budget, bool *truncated, StrBuf *sb) {
    if (*budget > 0) (*budget)--;
    char one[2] = { ch ? ch[0] : '\0', '\0' };
    sb_adds(sb, "{\"char\":");
    sb_json_str(sb, ch ? one : "");
    sb_addf(sb, ",\"terminal\":%s,\"weight\":%d,\"kind\":",
            node->terminal ? "true" : "false", node->weight);
    if (node->terminal && node->kind[0]) sb_json_str(sb, node->kind);
    else sb_adds(sb, "null");
    sb_adds(sb, ",\"children\":{");
    bool first = true;
    if (depth < max_depth) {
        TrieNode **ordered = NULL;
        if (node->nchild > 0) {
            ordered = xmalloc(sizeof(TrieNode *) * (size_t)node->nchild);
            sort_children(node, ordered);
        }
        for (int i = 0; i < node->nchild; i++) {
            if (*budget <= 0) { *truncated = true; break; }
            TrieNode *child = ordered[i];
            if (!first) sb_addc(sb, ',');
            first = false;
            char key[2] = { child->ch, '\0' };
            sb_json_str(sb, key);
            sb_addc(sb, ':');
            char cbuf[2] = { child->ch, '\0' };
            emit_tree(child, cbuf, depth + 1, max_depth, budget, truncated, sb);
        }
        free(ordered);
    } else if (node->nchild > 0) {
        *truncated = true;
    }
    sb_adds(sb, "}}");
}

void trie_to_json(Trie *t, const char *prefix, int max_depth, int max_nodes,
                  int top_words, int suggestions, StrBuf *sb) {
    if (max_depth < 0) max_depth = 0;
    if (max_nodes < 1) max_nodes = 1;
    if (top_words < 0) top_words = 0;
    if (suggestions < 0) suggestions = 0;

    pthread_mutex_lock(&t->lock);

    TrieStats st;
    memset(&st, 0, sizeof(st));
    collect_stats(t->root, 0, &st);

    WordList all;
    memset(&all, 0, sizeof(all));
    char buf[256] = "";
    collect_node(t->root, buf, 0, sizeof(buf), &all);
    qsort(all.items, (size_t)all.len, sizeof(WordEntry), word_cmp);

    WordList sugs;
    memset(&sugs, 0, sizeof(sugs));
    if (prefix && *prefix) {
        TrieNode *node = find_node(t, prefix);
        if (node) {
            char pbuf[256];
            snprintf(pbuf, sizeof(pbuf), "%s", prefix);
            collect_node(node, pbuf, strlen(pbuf), sizeof(pbuf), &sugs);
            qsort(sugs.items, (size_t)sugs.len, sizeof(WordEntry), word_cmp);
        }
    }

    sb_adds(sb, "{\"stats\":{\"nodes\":");
    sb_addf(sb, "%d,\"words\":%d,\"max_depth\":%d,\"kind_counts\":{",
            st.nodes, st.words, st.max_depth);
    for (int i = 0; i < st.nkinds; i++) {
        if (i) sb_addc(sb, ',');
        sb_json_str(sb, st.kinds[i].kind);
        sb_addf(sb, ":%d", st.kinds[i].count);
    }
    sb_adds(sb, "}},\"tree\":");
    int budget = max_nodes;
    bool truncated = false;
    emit_tree(t->root, "", 0, max_depth, &budget, &truncated, sb);
    sb_addf(sb, ",\"tree_meta\":{\"max_depth\":%d,\"max_nodes\":%d,\"truncated\":%s}",
            max_depth, max_nodes, truncated ? "true" : "false");

    sb_adds(sb, ",\"top_words\":[");
    int topn = all.len < top_words ? all.len : top_words;
    for (int i = 0; i < topn; i++) {
        if (i) sb_addc(sb, ',');
        sb_adds(sb, "{\"word\":");
        sb_json_str(sb, all.items[i].word);
        sb_addf(sb, ",\"weight\":%d,\"kind\":", all.items[i].weight);
        sb_json_str(sb, all.items[i].kind);
        sb_addc(sb, '}');
    }
    sb_adds(sb, "],\"prefix\":");
    sb_json_str(sb, prefix ? prefix : "");
    sb_adds(sb, ",\"suggestions\":[");
    int sn = sugs.len < suggestions ? sugs.len : suggestions;
    for (int i = 0; i < sn; i++) {
        if (i) sb_addc(sb, ',');
        sb_adds(sb, "{\"word\":");
        sb_json_str(sb, sugs.items[i].word);
        sb_addf(sb, ",\"weight\":%d,\"kind\":", sugs.items[i].weight);
        sb_json_str(sb, sugs.items[i].kind);
        sb_addc(sb, '}');
    }
    sb_adds(sb, "]}");

    free(all.items);
    free(sugs.items);
    pthread_mutex_unlock(&t->lock);
}
