/* Small hand-written syntax highlighter (line at a time). */
#include "anvil.h"

#include <ctype.h>
#include <string.h>

static bool is_c_like(const char *lang) {
    return strcmp(lang, "c") == 0 || strcmp(lang, "cpp") == 0 ||
           strcmp(lang, "java") == 0 || strcmp(lang, "javascript") == 0 ||
           strcmp(lang, "typescript") == 0 || strcmp(lang, "rust") == 0 ||
           strcmp(lang, "go") == 0 || strcmp(lang, "kotlin") == 0 ||
           strcmp(lang, "swift") == 0 || strcmp(lang, "csharp") == 0 ||
           strcmp(lang, "php") == 0;
}

static bool is_hash_comment(const char *lang) {
    return strcmp(lang, "python") == 0 || strcmp(lang, "bash") == 0 ||
           strcmp(lang, "ruby") == 0 || strcmp(lang, "yaml") == 0;
}

static bool ident_start(unsigned char c) { return isalpha(c) || c == '_'; }
static bool ident_char(unsigned char c) { return isalnum(c) || c == '_'; }

static void add_token(HlToken *tokens, int max, int *count, size_t start,
                      size_t end, HlType type) {
    if (*count >= max || end <= start) return;
    tokens[*count].start = start;
    tokens[*count].end = end;
    tokens[*count].type = type;
    (*count)++;
}

int highlight_line(const char *language, const char *line, size_t len,
                   HlToken *tokens, int max_tokens) {
    const char *lang = language_normalize(language ? language : "text");
    if (strcmp(lang, "text") == 0) return 0;

    int count = 0;
    size_t i = 0;

    /* Shebang or C preprocessor. */
    if (len >= 2 && line[0] == '#' && line[1] == '!' &&
        (strcmp(lang, "bash") == 0 || strcmp(lang, "python") == 0)) {
        add_token(tokens, max_tokens, &count, 0, len, HL_COMMENT);
        return count;
    }

    while (i < len) {
        unsigned char c = (unsigned char)line[i];

        if (c == '#' && is_c_like(lang)) {
            size_t j = i;
            while (j > 0 && (line[j - 1] == ' ' || line[j - 1] == '\t')) j--;
            if (j == 0) {
                add_token(tokens, max_tokens, &count, i, len, HL_PREPROC);
                break;
            }
        }

        if (c == '/' && i + 1 < len && line[i + 1] == '/') {
            add_token(tokens, max_tokens, &count, i, len, HL_COMMENT);
            break;
        }
        if (c == '/' && i + 1 < len && line[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < len && !(line[j] == '*' && line[j + 1] == '/')) j++;
            if (j + 1 < len) j += 2;
            else j = len;
            add_token(tokens, max_tokens, &count, i, j, HL_COMMENT);
            i = j;
            continue;
        }
        if (c == '-' && i + 1 < len && line[i + 1] == '-' &&
            strcmp(lang, "sql") == 0) {
            add_token(tokens, max_tokens, &count, i, len, HL_COMMENT);
            break;
        }
        if (c == '<' && i + 3 < len && strncmp(line + i, "<!--", 4) == 0) {
            size_t j = i + 4;
            while (j + 2 < len && !(line[j] == '-' && line[j + 1] == '-' && line[j + 2] == '>')) j++;
            if (j + 2 < len) j += 3;
            else j = len;
            add_token(tokens, max_tokens, &count, i, j, HL_COMMENT);
            i = j;
            continue;
        }
        if (c == '#' && is_hash_comment(lang)) {
            add_token(tokens, max_tokens, &count, i, len, HL_COMMENT);
            break;
        }

        if (c == '"' || c == '\'' || (c == '`' && (strcmp(lang, "javascript") == 0 ||
                                                    strcmp(lang, "typescript") == 0 ||
                                                    strcmp(lang, "bash") == 0 ||
                                                    strcmp(lang, "go") == 0))) {
            char quote = (char)c;
            size_t j = i + 1;
            while (j < len) {
                if (line[j] == '\\' && j + 1 < len) { j += 2; continue; }
                if (line[j] == quote) { j++; break; }
                j++;
            }
            add_token(tokens, max_tokens, &count, i, j, HL_STRING);
            i = j;
            continue;
        }

        if (isdigit(c)) {
            size_t j = i + 1;
            while (j < len) {
                unsigned char d = (unsigned char)line[j];
                if (isalnum(d) || d == '.' || d == '_' || d == '\'') j++;
                else break;
            }
            add_token(tokens, max_tokens, &count, i, j, HL_NUMBER);
            i = j;
            continue;
        }

        if (ident_start(c)) {
            size_t j = i + 1;
            while (j < len && ident_char((unsigned char)line[j])) j++;
            size_t n = j - i;
            char word[128];
            if (n >= sizeof(word)) n = sizeof(word) - 1;
            memcpy(word, line + i, n);
            word[n] = '\0';
            if (keyword_is_keyword(lang, word)) {
                add_token(tokens, max_tokens, &count, i, j, HL_KEYWORD);
            } else {
                size_t k = j;
                while (k < len && (line[k] == ' ' || line[k] == '\t')) k++;
                if (k < len && line[k] == '(') {
                    add_token(tokens, max_tokens, &count, i, j, HL_FUNC);
                }
            }
            i = j;
            continue;
        }

        i++;
    }
    return count;
}
