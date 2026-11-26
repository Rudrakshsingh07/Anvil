/* Bundled keyword lists and language detection. */
#include "anvil.h"
#include "keywords_data.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const KeywordSet *find_set(const char *name) {
    for (int i = 0; i < KEYWORD_SET_COUNT; i++) {
        if (strcmp(keyword_sets[i].name, name) == 0) return &keyword_sets[i];
    }
    return NULL;
}

const char *language_normalize(const char *language) {
    static char buf[32];
    if (!language) return "text";
    size_t j = 0;
    for (const char *p = language; *p && j + 1 < sizeof(buf); p++) {
        if (!isspace((unsigned char)*p)) buf[j++] = (char)tolower((unsigned char)*p);
    }
    buf[j] = '\0';
    if (strcmp(buf, "c++") == 0) return "cpp";
    if (strcmp(buf, "c#") == 0 || strcmp(buf, "cs") == 0) return "csharp";
    if (strcmp(buf, "js") == 0 || strcmp(buf, "node") == 0) return "javascript";
    if (strcmp(buf, "ts") == 0) return "typescript";
    if (strcmp(buf, "py") == 0) return "python";
    if (strcmp(buf, "sh") == 0 || strcmp(buf, "shell") == 0 ||
        strcmp(buf, "zsh") == 0 || strcmp(buf, "console") == 0 ||
        strcmp(buf, "docker") == 0 || strcmp(buf, "dockerfile") == 0) return "bash";
    if (strcmp(buf, "md") == 0) return "markdown";
    if (strcmp(buf, "yml") == 0) return "yaml";
    if (strcmp(buf, "htm") == 0 || strcmp(buf, "xhtml") == 0) return "html";
    if (strcmp(buf, "rs") == 0) return "rust";
    if (strcmp(buf, "rb") == 0) return "ruby";
    if (strcmp(buf, "kt") == 0 || strcmp(buf, "kts") == 0) return "kotlin";
    if (strcmp(buf, "golang") == 0) return "go";
    if (strcmp(buf, "plain") == 0 || strcmp(buf, "none") == 0 ||
        strcmp(buf, "txt") == 0) return "text";
    if (buf[0] == '\0') return "text";
    return buf;
}

const char *language_from_name(const char *name) {
    if (!name) return NULL;
    const char *key = language_normalize(name);
    if (strcmp(key, "text") == 0) return "text";
    const KeywordSet *set = find_set(key);
    return set ? set->name : NULL;
}

const char *language_label(const char *key) {
    if (!key) return "Plain Text";
    if (strcmp(key, "text") == 0) return "Plain Text";
    if (strcmp(key, "python") == 0) return "Python";
    if (strcmp(key, "javascript") == 0) return "JavaScript";
    if (strcmp(key, "typescript") == 0) return "TypeScript";
    if (strcmp(key, "c") == 0) return "C";
    if (strcmp(key, "cpp") == 0) return "C++";
    if (strcmp(key, "csharp") == 0) return "C#";
    if (strcmp(key, "bash") == 0) return "Bash";
    if (strcmp(key, "html") == 0) return "HTML";
    if (strcmp(key, "css") == 0) return "CSS";
    if (strcmp(key, "json") == 0) return "JSON";
    if (strcmp(key, "yaml") == 0) return "YAML";
    if (strcmp(key, "markdown") == 0) return "Markdown";
    if (strcmp(key, "sql") == 0) return "SQL";
    if (strcmp(key, "go") == 0) return "Go";
    if (strcmp(key, "rust") == 0) return "Rust";
    if (strcmp(key, "ruby") == 0) return "Ruby";
    if (strcmp(key, "php") == 0) return "PHP";
    if (strcmp(key, "java") == 0) return "Java";
    if (strcmp(key, "lua") == 0) return "Lua";
    if (strcmp(key, "kotlin") == 0) return "Kotlin";
    if (strcmp(key, "swift") == 0) return "Swift";
    return key;
}

const char *language_from_path(const char *path) {
    if (!path) return "text";
    const char *base = path_basename(path);
    if (strcasecmp(base, "Dockerfile") == 0 || strcasecmp(base, "Makefile") == 0) {
        return "bash";
    }
    const char *dot = strrchr(base, '.');
    if (!dot || !dot[1]) return "text";
    const char *ext = dot + 1;
    struct { const char *ext; const char *lang; } map[] = {
        {"py", "python"}, {"pyw", "python"},
        {"js", "javascript"}, {"mjs", "javascript"}, {"cjs", "javascript"},
        {"ts", "typescript"}, {"tsx", "typescript"},
        {"c", "c"}, {"h", "c"},
        {"cpp", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"}, {"hpp", "cpp"}, {"hh", "cpp"},
        {"java", "java"},
        {"rs", "rust"},
        {"go", "go"},
        {"rb", "ruby"},
        {"php", "php"},
        {"sql", "sql"},
        {"sh", "bash"}, {"bash", "bash"}, {"zsh", "bash"},
        {"html", "html"}, {"htm", "html"}, {"xhtml", "html"},
        {"css", "css"},
        {"json", "json"},
        {"yaml", "yaml"}, {"yml", "yaml"},
        {"md", "markdown"}, {"markdown", "markdown"},
        {"lua", "lua"},
        {"kt", "kotlin"}, {"kts", "kotlin"},
        {"swift", "swift"},
        {"cs", "csharp"},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcasecmp(ext, map[i].ext) == 0) return map[i].lang;
    }
    return "text";
}

void keyword_seed_trie(Trie *t, const char *language) {
    const KeywordSet *common = find_set("common");
    if (common) {
        for (int i = 0; i < common->count; i++) {
            trie_insert(t, common->words[i], 45, "common", false);
        }
    }
    const char *key = language_normalize(language);
    if (strcmp(key, "text") != 0) {
        const KeywordSet *set = find_set(key);
        if (set) {
            for (int i = 0; i < set->count; i++) {
                trie_insert(t, set->words[i], 80, "keyword", false);
            }
        }
    }
}

bool keyword_is_keyword(const char *language, const char *word) {
    if (!language || !word) return false;
    const char *key = language_normalize(language);
    if (strcmp(key, "text") == 0) return false;
    const KeywordSet *set = find_set(key);
    if (!set) return false;
    for (int i = 0; i < set->count; i++) {
        if (strcmp(set->words[i], word) == 0) return true;
    }
    return false;
}

void keyword_languages_json(StrBuf *sb) {
    sb_addc(sb, '[');
    bool first = true;
    for (int i = 0; i < KEYWORD_SET_COUNT; i++) {
        if (strcmp(keyword_sets[i].name, "common") == 0) continue;
        if (!first) sb_addc(sb, ',');
        first = false;
        sb_json_str(sb, keyword_sets[i].name);
    }
    sb_addc(sb, ']');
}
