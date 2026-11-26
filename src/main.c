/* Anvil entry point and command-line options. */
#include "anvil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    printf(
        "Usage: %s [options] [file]\n"
        "\n"
        "A notepad-like TUI editor with live data-structure state.\n"
        "\n"
        "Options:\n"
        "  --autosave / --no-autosave     enable or disable autosave (default: on)\n"
        "  --autosave-interval SECONDS    autosave interval (default: 30)\n"
        "  --port PORT                    state bridge port (default: 8765)\n"
        "  --no-server                    disable the HTTP bridge\n"
        "  --state-file PATH              JSON fallback path\n"
        "  --no-state-file                disable the JSON fallback\n"
        "  --theme NAME                   colour theme name (default: monokai)\n"
        "  --tab N                        soft-tab width 1-16 (default: 4)\n"
        "  --panel / --no-panel           show or hide the data panel\n"
        "  --undo-capacity N              undo stack capacity (default: 150)\n"
        "  --clipboard-capacity N         clipboard history capacity (default: 50)\n"
        "  --version                      print version\n"
        "  --help                         print this help\n"
        "\n"
        "Press F1 inside the editor for the full shortcut list.\n",
        prog);
}

static bool arg_is(const char *arg, const char *name) {
    return strcmp(arg, name) == 0;
}

int main(int argc, char **argv) {
    const char *path = NULL;
    bool autosave = true;
    double autosave_interval = 30.0;
    int port = 8765;
    bool server_enabled = true;
    const char *state_file = NULL;
    bool state_file_enabled = true;
    const char *theme = NULL;
    int tab_width = 4;
    int panel = -1;
    int undo_capacity = 150;
    int clipboard_capacity = 50;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg_is(arg, "--help") || arg_is(arg, "-h")) {
            print_usage(argv[0]);
            return 0;
        }
        if (arg_is(arg, "--version")) {
            printf("anvil %s\n", ANVIL_VERSION);
            return 0;
        }
        if (arg_is(arg, "--autosave")) { autosave = true; continue; }
        if (arg_is(arg, "--no-autosave")) { autosave = false; continue; }
        if (arg_is(arg, "--no-server")) { server_enabled = false; continue; }
        if (arg_is(arg, "--no-state-file")) { state_file_enabled = false; continue; }
        if (arg_is(arg, "--panel")) { panel = 1; continue; }
        if (arg_is(arg, "--no-panel")) { panel = 0; continue; }
        if (arg_is(arg, "--autosave-interval") && i + 1 < argc) {
            autosave_interval = atof(argv[++i]);
            continue;
        }
        if (arg_is(arg, "--port") && i + 1 < argc) {
            port = atoi(argv[++i]);
            continue;
        }
        if (arg_is(arg, "--state-file") && i + 1 < argc) {
            state_file = argv[++i];
            continue;
        }
        if (arg_is(arg, "--theme") && i + 1 < argc) {
            theme = argv[++i];
            continue;
        }
        if (arg_is(arg, "--tab") && i + 1 < argc) {
            tab_width = atoi(argv[++i]);
            continue;
        }
        if (arg_is(arg, "--undo-capacity") && i + 1 < argc) {
            undo_capacity = atoi(argv[++i]);
            continue;
        }
        if (arg_is(arg, "--clipboard-capacity") && i + 1 < argc) {
            clipboard_capacity = atoi(argv[++i]);
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "anvil: unknown option: %s\n", arg);
            fprintf(stderr, "Try 'anvil --help'.\n");
            return 2;
        }
        if (!path) path = arg;
        else {
            fprintf(stderr, "anvil: only one file can be opened at a time\n");
            return 2;
        }
    }

    if (autosave_interval < 1.0) autosave_interval = 1.0;
    if (tab_width < 1) tab_width = 1;
    if (tab_width > 16) tab_width = 16;
    if (undo_capacity < 1) undo_capacity = 1;
    if (clipboard_capacity < 1) clipboard_capacity = 1;

    Editor ed;
    editor_init(&ed);
    ed.autosave_enabled = autosave;
    ed.autosave_interval = autosave_interval;
    ed.tab_width = tab_width;
    ed.server_enabled = server_enabled;
    if (theme && *theme) snprintf(ed.theme, sizeof(ed.theme), "%s", theme);
    if (undo_capacity != 150) {
        undo_free(&ed.undo);
        undo_init(&ed.undo, (size_t)undo_capacity, 0.9, 4000000);
    }
    if (clipboard_capacity != 50) {
        clip_free(&ed.clip);
        clip_init(&ed.clip, clipboard_capacity);
    }
    if (state_file) editor_set_state_file(&ed, state_file, state_file_enabled);
    else if (!state_file_enabled) editor_set_state_file(&ed, NULL, false);

    int rows = 24, cols = 80;
    ui_terminal_size(&rows, &cols);
    ed.rows = rows;
    ed.cols = cols;
    ed.panel_visible = panel == 1 || (panel < 0 && cols >= 100);

    if (path) editor_open(&ed, path, true);
    else editor_new(&ed, true);

    if (server_enabled) {
        ed.bridge = bridge_start(&ed, port, true, ed.state_file, ed.state_file_enabled);
        if (ed.bridge) {
            ed.port = bridge_port(ed.bridge);
        } else {
            ed.server_enabled = false;
            editor_set_message(&ed, "State bridge unavailable (port busy?)", true);
        }
    } else {
        ed.port = 0;
    }
    editor_publish(&ed, "ok");

    if (ui_init(&ed) != 0) {
        fprintf(stderr, "anvil: cannot initialize the terminal\n");
        if (ed.bridge) bridge_stop(ed.bridge);
        editor_free(&ed);
        return 1;
    }

    while (ed.running) {
        ui_draw(&ed);
        uint32_t ch = 0;
        int key = ui_read_key(250, &ch);
        if (ui_resized) {
            ui_resized = 0;
            int r, c;
            ui_terminal_size(&r, &c);
            ed.rows = r;
            ed.cols = c;
        }
        if (key != KEY_NONE) editor_handle_key(&ed, key, ch);
        editor_tick(&ed);
    }

    editor_publish(&ed, "closed");
    if (ed.bridge) bridge_stop(ed.bridge);
    ui_shutdown(&ed);
    editor_free(&ed);
    return 0;
}
