#include "test.h"
#include "dialog/dialog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Dialog runner (dialog.c) on a bank compiled from data/test_dialog.json by
// tools/dialog_build.py (the Makefile builds it before the tests run).

#ifndef TEST_DIALOG_BANK
#define TEST_DIALOG_BANK "build/test_dialog.dlg"
#endif

static char events[128];
static bool has_key;

static void on_event(const char *e, void *ctx) {
    (void)ctx;
    strncat(events, e, sizeof(events) - strlen(events) - 2);
    strcat(events, ";");
}
static bool on_check(const char *cond, void *ctx) { (void)ctx; return strcmp(cond, "has_key") == 0 && has_key; }
static bool on_var(const char *name, char *out, int len, void *ctx) {
    (void)ctx;
    if (strcmp(name, "player") != 0) return false;
    snprintf(out, len, "Ada");
    return true;
}

static DialogBank *load(void) {
    FILE *f = fopen(TEST_DIALOG_BANK, "rb");
    if (!f) return NULL;
    static unsigned char buf[4096];
    int n = (int)fread(buf, 1, sizeof(buf), f);
    fclose(f);
    char err[96];
    return dialog_bank_parse(buf, n, err, sizeof(err));
}

static void test_dialog_flow(void) {
    DialogBank *b = load();
    CHECK(b != NULL);
    if (!b) return;
    CHECK(dialog_bank_conversations(b) == 3);

    DialogHooks hooks = { on_event, on_check, on_var, NULL };
    DialogRunner r;
    events[0] = '\0';
    has_key = false;
    CHECK(!dialog_start(&r, b, "missing", &hooks));
    CHECK(dialog_start(&r, b, "intro", &hooks));
    CHECK(strcmp(dialog_speaker(&r), "Guide") == 0);
    CHECK(dialog_speaker_color(&r) == DIALOG_COL_ACCENT);
    // Variables filled, colours compiled to ^NN, the pause kept as control bytes
    CHECK(memcmp(dialog_text(&r), "Hello, Ada. ^04Welcome^02.\x01\x0a", 28) == 0);

    dialog_advance(&r);
    CHECK(strcmp(dialog_speaker(&r), "Guide") == 0);          // inherited
    CHECK(strstr(dialog_text(&r), "{unknown}") != NULL);        // unknown variable left as written

    dialog_advance(&r);
    CHECK(strcmp(events, "asked;") == 0);                       // event on entering the line
    CHECK(r.choice_count == 2);                                 // "Secret" hidden (no key)
    CHECK(strcmp(r.choice_text[0], "Tour") == 0 && strcmp(r.choice_text[1], "Bye") == 0);
    dialog_advance(&r);                                         // lines with choices wait for a pick
    CHECK(r.choice_count == 2);

    dialog_choose(&r, 0);
    CHECK(strcmp(dialog_speaker(&r), "You") == 0);
    dialog_advance(&r);                                         // "next": skips a line
    CHECK(strcmp(dialog_text(&r), "Done.") == 0);
    CHECK(strcmp(events, "asked;finished;") == 0);
    dialog_advance(&r);                                         // last line of the conversation
    CHECK(!dialog_running(&r));

    // With the key, the conditional choice appears and jumps to another conversation
    has_key = true;
    CHECK(dialog_start(&r, b, "intro", &hooks));
    dialog_advance(&r);
    dialog_advance(&r);
    CHECK(r.choice_count == 3);
    dialog_choose(&r, 1);
    CHECK(strcmp(dialog_text(&r), "Hidden line.") == 0);
    dialog_choose(&r, 0);                                        // no choices here: ignored
    CHECK(dialog_running(&r));
    dialog_advance(&r);
    CHECK(!dialog_running(&r));

    // Speaker names take variables too
    CHECK(dialog_start(&r, b, "named", &hooks));
    CHECK(strcmp(dialog_speaker(&r), "Ada") == 0);

    // "end": true choice
    CHECK(dialog_start(&r, b, "intro", &hooks));
    dialog_advance(&r);
    dialog_advance(&r);
    dialog_choose(&r, r.choice_count - 1);
    CHECK(!dialog_running(&r));
    dialog_bank_free(b);
}

static void test_dialog_bad_banks(void) {
    char err[96];
    CHECK(dialog_bank_parse("nope", 4, err, sizeof(err)) == NULL);
    unsigned char hdr[20] = {'D','L','G','1', 0, 9};            // wrong version
    CHECK(dialog_bank_parse(hdr, sizeof(hdr), err, sizeof(err)) == NULL);
    CHECK(strstr(err, "version") != NULL);

    DialogBank *b = load();
    CHECK(b != NULL);
    if (!b) return;
    dialog_bank_free(b);
    // A truncated file fails the size check instead of reading past the end
    FILE *f = fopen(TEST_DIALOG_BANK, "rb");
    static unsigned char buf[4096];
    int n = (int)fread(buf, 1, sizeof(buf), f);
    fclose(f);
    CHECK(dialog_bank_parse(buf, n - 1, err, sizeof(err)) == NULL);
    int speakers = (buf[8] << 8) | buf[9];
    buf[20 + 8 * speakers + 4] = 0x7F;                           // conversation 0 first node out of range
    CHECK(dialog_bank_parse(buf, n, err, sizeof(err)) == NULL);
}

void run_dialog_tests(void) {
    RUN_TEST(test_dialog_flow);
    RUN_TEST(test_dialog_bad_banks);
}
