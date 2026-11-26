/* Generated from the original keyword lists.  C-only data. */
#ifndef ANVIL_KEYWORDS_DATA_H
#define ANVIL_KEYWORDS_DATA_H

static const char *const kw_common[] = {
    "value", "result", "data", "index", "item", "items", "node", "nodes",
    "list", "array", "string", "number", "boolean", "object", "function", "method",
    "module", "package", "class", "instance", "self", "this", "true", "false",
    "null", "None", "True", "False", "nil", "NULL", "error", "exception",
    "message", "buffer", "document", "file", "path", "name", "text", "line",
    "word", "count", "size", "length", "capacity", "key", "value", "map",
    "set", "queue", "stack", "trie", "tree", "graph", "undo", "redo",
    "save", "open", "close", "read", "write", "append", "insert", "delete",
    "remove", "update", "search", "sort", "merge", "split", "join", "start",
    "end", "next", "previous", "head", "tail", "push", "pop", "peek",
    "enqueue", "dequeue", "front", "back", "public", "private", "protected", "static",
    "void", "int", "float", "double", "char", "long", "short", "const",
    "let", "var", "async", "await", "yield", "lambda", "return", "import",
    "export", "default", "print", "input", "output", "load", "store", "cache",
    "hash", "sort", "min", "max", "sum", "average", "filter", "reduce",
    "select", "where",
};
#define KW_COMMON_COUNT 122

static const char *const kw_bash[] = {
    "if", "then", "else", "elif", "fi", "for", "while", "until",
    "do", "done", "case", "esac", "function", "in", "select", "time",
    "coproc", "break", "continue", "return", "exit", "export", "local", "readonly",
    "declare", "unset", "shift", "source", "alias", "unalias", "echo", "printf",
    "read", "cd", "pwd", "ls", "cat", "grep", "sed", "awk",
    "find", "xargs", "sort", "uniq", "head", "tail", "cut", "tr",
    "wc", "chmod", "chown", "mkdir", "rmdir", "rm", "cp", "mv",
    "touch", "test", "true", "false", "set", "trap", "wait", "kill",
    "sleep", "eval", "exec",
};
#define KW_BASH_COUNT 67

static const char *const kw_c[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while", "NULL", "true", "false", "stdio", "stdlib", "string",
    "stdint", "stdbool", "printf", "scanf", "malloc", "calloc", "realloc", "free",
    "memcpy", "memset", "strlen", "strcpy", "strcmp", "fopen", "fclose", "fread",
    "fwrite", "fprintf", "FILE", "size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "bool",
};
#define KW_C_COUNT 69

static const char *const kw_cpp[] = {
    "alignas", "alignof", "auto", "bool", "break", "case", "catch", "char",
    "class", "const", "constexpr", "continue", "decltype", "default", "delete", "do",
    "double", "else", "enum", "explicit", "export", "extern", "false", "float",
    "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
    "namespace", "new", "noexcept", "nullptr", "operator", "override", "private", "protected",
    "public", "return", "short", "signed", "sizeof", "static", "static_cast", "struct",
    "switch", "template", "this", "throw", "true", "try", "typedef", "typeid",
    "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while",
    "std", "string", "vector", "map", "unordered_map", "set", "unordered_set", "queue",
    "stack", "deque", "list", "pair", "tuple", "shared_ptr", "unique_ptr", "make_shared",
    "make_unique", "cout", "cin", "cerr", "endl", "include", "define", "ifndef",
    "endif", "pragma",
};
#define KW_CPP_COUNT 90

static const char *const kw_csharp[] = {
    "abstract", "as", "base", "bool", "break", "byte", "case", "catch",
    "char", "checked", "class", "const", "continue", "decimal", "default", "delegate",
    "do", "double", "else", "enum", "event", "explicit", "extern", "false",
    "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit",
    "in", "int", "interface", "internal", "is", "lock", "long", "namespace",
    "new", "null", "object", "operator", "out", "override", "params", "private",
    "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
    "sizeof", "stackalloc", "static", "string", "struct", "switch", "this", "throw",
    "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort",
    "using", "virtual", "void", "volatile", "while", "async", "await", "var",
    "record", "init", "Console", "WriteLine", "ReadLine", "List", "Dictionary",
};
#define KW_CSHARP_COUNT 87

static const char *const kw_css[] = {
    "color", "background", "background-color", "background-image", "border", "border-radius", "margin", "padding",
    "display", "position", "top", "right", "bottom", "left", "width", "height",
    "min-width", "max-width", "min-height", "max-height", "font", "font-family", "font-size", "font-weight",
    "line-height", "text-align", "text-decoration", "letter-spacing", "flex", "flex-direction", "justify-content", "align-items",
    "align-content", "gap", "grid", "grid-template-columns", "grid-template-rows", "grid-column", "grid-row", "overflow",
    "opacity", "box-shadow", "transform", "transition", "animation", "cursor", "z-index", "visibility",
    "float", "clear", "object-fit", "content", "inherit", "initial", "unset", "none",
    "block", "inline", "inline-block", "absolute", "relative", "fixed", "sticky", "hidden",
    "auto", "center", "important",
};
#define KW_CSS_COUNT 67

static const char *const kw_go[] = {
    "break", "case", "chan", "const", "continue", "default", "defer", "else",
    "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
    "map", "package", "range", "return", "select", "struct", "switch", "type",
    "var", "true", "false", "nil", "string", "int", "int8", "int16",
    "int32", "int64", "uint", "uint8", "uint16", "uint32", "uint64", "float32",
    "float64", "complex64", "complex128", "byte", "rune", "bool", "error", "any",
    "make", "new", "append", "cap", "close", "copy", "delete", "len",
    "panic", "print", "println", "recover", "fmt", "Printf", "Println", "Sprintf",
    "Errorf", "os", "io", "strings", "strconv", "sync", "time", "context",
    "json",
};
#define KW_GO_COUNT 73

static const char *const kw_html[] = {
    "html", "head", "body", "title", "meta", "link", "script", "style",
    "div", "span", "p", "a", "img", "ul", "ol", "li",
    "table", "thead", "tbody", "tfoot", "tr", "th", "td", "form",
    "input", "button", "textarea", "select", "option", "label", "header", "footer",
    "main", "section", "article", "aside", "nav", "h1", "h2", "h3",
    "h4", "h5", "h6", "pre", "code", "blockquote", "canvas", "svg",
    "video", "audio", "source", "iframe", "template", "slot", "class", "id",
    "href", "src", "alt", "type", "name", "value", "placeholder", "disabled",
    "checked", "selected", "required", "readonly", "target", "rel", "width", "height",
};
#define KW_HTML_COUNT 72

static const char *const kw_java[] = {
    "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
    "class", "const", "continue", "default", "do", "double", "else", "enum",
    "extends", "final", "finally", "float", "for", "goto", "if", "implements",
    "import", "instanceof", "int", "interface", "long", "native", "new", "package",
    "private", "protected", "public", "return", "short", "static", "strictfp", "super",
    "switch", "synchronized", "this", "throw", "throws", "transient", "try", "void",
    "volatile", "while", "true", "false", "null", "String", "Object", "Integer",
    "Double", "Boolean", "Character", "List", "ArrayList", "Map", "HashMap", "Set",
    "HashSet", "Queue", "Deque", "ArrayDeque", "LinkedList", "Collections", "Arrays", "Optional",
    "Stream", "System", "Math", "Exception", "RuntimeException", "IOException", "Override", "public",
    "main", "println", "print", "length", "equals", "hashCode", "toString", "compareTo",
    "iterator",
};
#define KW_JAVA_COUNT 89

static const char *const kw_javascript[] = {
    "async", "await", "break", "case", "catch", "class", "const", "continue",
    "debugger", "default", "delete", "do", "else", "export", "extends", "finally",
    "for", "function", "if", "import", "in", "instanceof", "let", "new",
    "of", "return", "static", "super", "switch", "this", "throw", "try",
    "typeof", "var", "void", "while", "with", "yield", "true", "false",
    "null", "undefined", "NaN", "Infinity", "console", "document", "window", "globalThis",
    "Array", "Object", "String", "Number", "Boolean", "Math", "JSON", "Promise",
    "Map", "Set", "WeakMap", "WeakSet", "Symbol", "Date", "RegExp", "Error",
    "TypeError", "SyntaxError", "require", "module", "exports", "process", "setTimeout", "setInterval",
    "clearTimeout", "fetch", "addEventListener", "querySelector", "getElementById", "localStorage", "length", "push",
    "pop", "shift", "unshift", "slice", "splice", "map", "filter", "reduce",
    "forEach", "find", "includes", "join", "split", "replace", "trim", "toLowerCase",
    "toUpperCase", "parseInt", "parseFloat",
};
#define KW_JAVASCRIPT_COUNT 99

static const char *const kw_json[] = {
    "true", "false", "null", "string", "number", "object", "array", "properties",
    "required", "type", "items", "title", "description", "default", "enum", "schema",
    "version", "name", "id", "value", "data",
};
#define KW_JSON_COUNT 21

static const char *const kw_kotlin[] = {
    "as", "break", "class", "continue", "do", "else", "false", "for",
    "fun", "if", "in", "interface", "is", "null", "object", "package",
    "return", "super", "this", "throw", "true", "try", "typealias", "typeof",
    "val", "var", "when", "while", "by", "catch", "constructor", "delegate",
    "dynamic", "field", "file", "finally", "get", "import", "init", "param",
    "property", "receiver", "set", "setparam", "where", "actual", "abstract", "annotation",
    "companion", "const", "crossinline", "data", "enum", "expect", "external", "final",
    "infix", "inline", "inner", "internal", "lateinit", "noinline", "open", "operator",
    "out", "override", "private", "protected", "public", "reified", "sealed", "suspend",
    "tailrec", "vararg", "println", "listOf", "mapOf",
};
#define KW_KOTLIN_COUNT 77

static const char *const kw_lua[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for",
    "function", "goto", "if", "in", "local", "nil", "not", "or",
    "repeat", "return", "then", "true", "until", "while", "print", "pairs",
    "ipairs", "require", "table", "string", "math", "io", "os", "coroutine",
    "insert", "remove", "concat", "sort", "format", "match", "gsub", "find",
};
#define KW_LUA_COUNT 40

static const char *const kw_markdown[] = {
    "heading", "title", "section", "table", "link", "image", "code", "blockquote",
    "list", "task", "bold", "italic", "strikethrough", "footnote", "reference", "frontmatter",
    "toc", "note", "warning", "tip", "important", "summary", "details",
};
#define KW_MARKDOWN_COUNT 23

static const char *const kw_php[] = {
    "abstract", "and", "array", "as", "break", "callable", "case", "catch",
    "class", "clone", "const", "continue", "declare", "default", "do", "echo",
    "else", "elseif", "empty", "enddeclare", "endfor", "endforeach", "endif", "endswitch",
    "endwhile", "eval", "exit", "extends", "final", "finally", "fn", "for",
    "foreach", "function", "global", "goto", "if", "implements", "include", "include_once",
    "instanceof", "insteadof", "interface", "isset", "list", "match", "namespace", "new",
    "or", "print", "private", "protected", "public", "readonly", "require", "require_once",
    "return", "static", "switch", "throw", "trait", "try", "unset", "use",
    "var", "while", "xor", "yield", "true", "false", "null", "self",
    "parent", "this", "string", "int", "float", "bool", "array", "object",
    "mixed", "void", "never",
};
#define KW_PHP_COUNT 83

static const char *const kw_python[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue",
    "def", "del", "elif", "else", "except", "finally", "for", "from",
    "global", "if", "import", "in", "is", "lambda", "nonlocal", "not",
    "or", "pass", "raise", "return", "try", "while", "with", "yield",
    "match", "case", "None", "True", "False", "self", "cls", "super",
    "print", "len", "range", "enumerate", "zip", "map", "filter", "sorted",
    "reversed", "sum", "min", "max", "abs", "round", "open", "input",
    "isinstance", "issubclass", "getattr", "setattr", "hasattr", "repr", "str", "int",
    "float", "bool", "list", "dict", "set", "tuple", "bytes", "bytearray",
    "frozenset", "object", "type", "Exception", "ValueError", "TypeError", "KeyError", "IndexError",
    "StopIteration", "RuntimeError", "NotImplementedError", "__init__", "__main__", "__name__", "__repr__", "dataclass",
    "property", "staticmethod", "classmethod", "contextmanager", "asyncio", "typing", "collections", "pathlib",
    "os", "sys", "json", "re", "time", "math", "random", "itertools",
    "functools",
};
#define KW_PYTHON_COUNT 105

static const char *const kw_ruby[] = {
    "alias", "and", "begin", "break", "case", "class", "def", "defined",
    "do", "else", "elsif", "end", "ensure", "false", "for", "if",
    "in", "module", "next", "nil", "not", "or", "redo", "rescue",
    "retry", "return", "self", "super", "then", "true", "undef", "unless",
    "until", "when", "while", "yield", "require", "require_relative", "attr_accessor", "attr_reader",
    "attr_writer", "initialize", "puts", "print", "p", "gets", "each", "map",
    "select", "reject", "reduce", "inject", "length", "size", "to_s", "to_i",
    "to_a", "to_h", "new", "raise", "Array", "Hash", "String", "Integer",
    "Float", "Symbol", "Range",
};
#define KW_RUBY_COUNT 67

static const char *const kw_rust[] = {
    "as", "async", "await", "break", "const", "continue", "crate", "dyn",
    "else", "enum", "extern", "false", "fn", "for", "if", "impl",
    "in", "let", "loop", "match", "mod", "move", "mut", "pub",
    "ref", "return", "self", "Self", "static", "struct", "super", "trait",
    "true", "type", "unsafe", "use", "where", "while", "yield", "Some",
    "None", "Ok", "Err", "Option", "Result", "Vec", "String", "str",
    "HashMap", "HashSet", "BTreeMap", "Box", "Rc", "Arc", "Cell", "RefCell",
    "Mutex", "RwLock", "println", "print", "format", "vec", "panic", "unwrap",
    "expect", "clone", "iter", "into_iter", "map", "filter", "collect", "fold",
    "derive", "Debug", "Clone", "Copy", "PartialEq", "Eq", "Hash", "Default",
    "std", "core", "alloc", "io", "fs",
};
#define KW_RUST_COUNT 85

static const char *const kw_sql[] = {
    "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE", "SET",
    "DELETE", "CREATE", "TABLE", "ALTER", "DROP", "INDEX", "VIEW", "JOIN",
    "INNER", "LEFT", "RIGHT", "FULL", "OUTER", "ON", "AS", "AND",
    "OR", "NOT", "NULL", "IS", "IN", "LIKE", "BETWEEN", "EXISTS",
    "GROUP", "BY", "ORDER", "HAVING", "LIMIT", "OFFSET", "DISTINCT", "UNION",
    "ALL", "CASE", "WHEN", "THEN", "ELSE", "END", "PRIMARY", "KEY",
    "FOREIGN", "REFERENCES", "UNIQUE", "CHECK", "DEFAULT", "CONSTRAINT", "AUTOINCREMENT", "INTEGER",
    "TEXT", "REAL", "BLOB", "BOOLEAN", "DATE", "TIMESTAMP", "VARCHAR", "COUNT",
    "SUM", "AVG", "MIN", "MAX", "COALESCE", "CAST", "BEGIN", "COMMIT",
    "ROLLBACK", "TRANSACTION", "WITH", "RETURNING",
};
#define KW_SQL_COUNT 76

static const char *const kw_swift[] = {
    "associatedtype", "class", "deinit", "enum", "extension", "fileprivate", "func", "import",
    "init", "inout", "internal", "let", "open", "operator", "private", "protocol",
    "public", "rethrows", "static", "struct", "subscript", "typealias", "var", "break",
    "case", "continue", "default", "defer", "do", "else", "fallthrough", "for",
    "guard", "if", "in", "repeat", "return", "switch", "where", "while",
    "as", "Any", "catch", "false", "is", "nil", "super", "self",
    "Self", "throw", "throws", "true", "try", "async", "await", "actor",
    "some", "any", "String", "Int", "Double", "Float", "Bool", "Array",
    "Dictionary", "Set", "Optional", "Result", "print", "map", "filter", "reduce",
};
#define KW_SWIFT_COUNT 72

static const char *const kw_typescript[] = {
    "abstract", "any", "as", "asserts", "async", "await", "bigint", "boolean",
    "break", "case", "catch", "class", "const", "constructor", "continue", "declare",
    "default", "delete", "do", "else", "enum", "export", "extends", "finally",
    "for", "from", "function", "get", "if", "implements", "import", "in",
    "infer", "instanceof", "interface", "is", "keyof", "let", "module", "namespace",
    "never", "new", "null", "number", "object", "of", "override", "private",
    "protected", "public", "readonly", "require", "return", "set", "static", "string",
    "super", "switch", "symbol", "this", "throw", "try", "type", "typeof",
    "undefined", "unique", "unknown", "var", "void", "while", "with", "yield",
    "Partial", "Required", "Readonly", "Record", "Pick", "Omit", "Exclude", "Extract",
    "NonNullable", "ReturnType", "Parameters", "Promise", "Array", "Map", "Set", "Date",
    "RegExp", "console",
};
#define KW_TYPESCRIPT_COUNT 90

static const char *const kw_yaml[] = {
    "true", "false", "null", "yes", "no", "on", "off", "version",
    "name", "description", "services", "volumes", "networks", "environment", "image", "build",
    "ports", "depends_on", "restart", "command", "entrypoint", "steps", "jobs", "runs-on",
    "uses", "with", "secrets", "matrix", "include",
};
#define KW_YAML_COUNT 29

typedef struct {
    const char *name;
    const char *const *words;
    int count;
} KeywordSet;

static const KeywordSet keyword_sets[] = {
    {"common", kw_common, KW_COMMON_COUNT},
    {"bash", kw_bash, KW_BASH_COUNT},
    {"c", kw_c, KW_C_COUNT},
    {"cpp", kw_cpp, KW_CPP_COUNT},
    {"csharp", kw_csharp, KW_CSHARP_COUNT},
    {"css", kw_css, KW_CSS_COUNT},
    {"go", kw_go, KW_GO_COUNT},
    {"html", kw_html, KW_HTML_COUNT},
    {"java", kw_java, KW_JAVA_COUNT},
    {"javascript", kw_javascript, KW_JAVASCRIPT_COUNT},
    {"json", kw_json, KW_JSON_COUNT},
    {"kotlin", kw_kotlin, KW_KOTLIN_COUNT},
    {"lua", kw_lua, KW_LUA_COUNT},
    {"markdown", kw_markdown, KW_MARKDOWN_COUNT},
    {"php", kw_php, KW_PHP_COUNT},
    {"python", kw_python, KW_PYTHON_COUNT},
    {"ruby", kw_ruby, KW_RUBY_COUNT},
    {"rust", kw_rust, KW_RUST_COUNT},
    {"sql", kw_sql, KW_SQL_COUNT},
    {"swift", kw_swift, KW_SWIFT_COUNT},
    {"typescript", kw_typescript, KW_TYPESCRIPT_COUNT},
    {"yaml", kw_yaml, KW_YAML_COUNT},
};
#define KEYWORD_SET_COUNT ((int)(sizeof(keyword_sets) / sizeof(keyword_sets[0])))

#endif
