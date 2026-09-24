#include "dialog.h"
#include <libdragon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Bank layout: see tools/dialog_build.py (big-endian, version 1)
#define HEADER_BYTES   20
#define SPEAKER_BYTES   8
#define CONV_BYTES      8
#define NODE_BYTES     16
#define CHOICE_BYTES   12
#define NONE16     0xFFFF
#define NONE32     0xFFFFFFFFu

struct DialogBank {
    uint8_t *data;
    int size;
    int n_speakers, n_convs, n_nodes, n_choices;
    const uint8_t *speakers, *convs, *nodes, *choices;
    const char *strings;
    uint32_t string_bytes;
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static const char *str_at(const DialogBank *b, uint32_t off) {
    return (off == NONE32 || off >= b->string_bytes) ? NULL : b->strings + off;
}

DialogBank *dialog_bank_parse(const void *data, int size, char *err, int errlen) {
    const uint8_t *d = data;
    #define FAIL(msg) do { if (err) snprintf(err, errlen, "%s", msg); return NULL; } while (0)
    if (!d || size < HEADER_BYTES || memcmp(d, "DLG1", 4) != 0) FAIL("not a dialog bank (DLG1)");
    if (rd16(d + 4) != 1) FAIL("unsupported dialog bank version (rebuild with tools/dialog_build.py)");
    int ns = rd16(d + 8), nc = rd16(d + 10), nn = rd16(d + 12), nch = rd16(d + 14);
    uint32_t sb = rd32(d + 16);
    long need = HEADER_BYTES + (long)ns * SPEAKER_BYTES + (long)nc * CONV_BYTES +
                (long)nn * NODE_BYTES + (long)nch * CHOICE_BYTES + (long)sb;
    if (need != size) FAIL("dialog bank size does not match its header");
    if (sb == 0 || d[size - 1] != 0) FAIL("dialog bank string table is not terminated");

    DialogBank *b = calloc(1, sizeof(*b));
    if (!b) FAIL("out of memory");
    b->data = malloc(size);
    if (!b->data) { free(b); FAIL("out of memory"); }
    memcpy(b->data, d, size);
    b->size = size;
    b->n_speakers = ns; b->n_convs = nc; b->n_nodes = nn; b->n_choices = nch;
    const uint8_t *p = b->data + HEADER_BYTES;
    b->speakers = p; p += ns * SPEAKER_BYTES;
    b->convs = p;    p += nc * CONV_BYTES;
    b->nodes = p;    p += nn * NODE_BYTES;
    b->choices = p;  p += nch * CHOICE_BYTES;
    b->strings = (const char *)p;
    b->string_bytes = sb;

    // Every reference must stay inside the bank: a bad file fails here, not mid-conversation
    for (int i = 0; i < nc; i++)
        if (rd16(b->convs + i * CONV_BYTES + 4) >= nn || !str_at(b, rd32(b->convs + i * CONV_BYTES))) {
            dialog_bank_free(b); FAIL("dialog bank: bad conversation entry");
        }
    for (int i = 0; i < nn; i++) {
        const uint8_t *n = b->nodes + i * NODE_BYTES;
        uint16_t spk = rd16(n + 8), next = rd16(n + 10), fc = rd16(n + 12);
        int cc = n[14];
        if (!str_at(b, rd32(n)) || (spk != NONE16 && spk >= ns) || (next != NONE16 && next >= nn) ||
            cc > DIALOG_CHOICE_MAX || fc + cc > nch) {
            dialog_bank_free(b); FAIL("dialog bank: bad line entry");
        }
    }
    for (int i = 0; i < nch; i++) {
        uint16_t next = rd16(b->choices + i * CHOICE_BYTES + 8);
        if (!str_at(b, rd32(b->choices + i * CHOICE_BYTES)) || (next != NONE16 && next >= nn)) {
            dialog_bank_free(b); FAIL("dialog bank: bad choice entry");
        }
    }
    #undef FAIL
    return b;
}

DialogBank *dialog_bank_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { debugf("dialog: %s not found\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc(size > 0 ? size : 1);
    bool ok = buf && size > 0 && fread(buf, 1, size, f) == (size_t)size;
    fclose(f);
    char err[96] = "read error";
    DialogBank *b = ok ? dialog_bank_parse(buf, (int)size, err, sizeof(err)) : NULL;
    free(buf);
    if (!b) debugf("dialog: %s: %s\n", path, err);
    return b;
}

void dialog_bank_free(DialogBank *bank) {
    if (!bank) return;
    free(bank->data);
    free(bank);
}

int dialog_bank_conversations(const DialogBank *bank) { return bank ? bank->n_convs : 0; }

// Copy src to out, replacing {name} with the game's value and {{ with {
static void fill_vars(const DialogRunner *r, const char *src, char *out, int outlen) {
    int o = 0;
    while (*src && o < outlen - 1) {
        if (src[0] == '{' && src[1] == '{') { out[o++] = '{'; src += 2; continue; }
        if (src[0] == '{') {
            const char *end = strchr(src, '}');
            if (end && end - src - 1 < 32) {
                char name[32], val[64];
                int n = (int)(end - src - 1);
                memcpy(name, src + 1, n);
                name[n] = '\0';
                if (r->hooks.variable && r->hooks.variable(name, val, sizeof(val), r->hooks.ctx)) {
                    for (const char *v = val; *v && o < outlen - 1; v++) {
                        if ((*v == '^' || *v == '$') && o < outlen - 2) out[o++] = *v;   // keep rdpq escapes
                        out[o++] = *v;
                    }
                    src = end + 1;
                    continue;
                }
            }
        }
        out[o++] = *src++;
    }
    out[o] = '\0';
}

// Enter a line: fill in its text and choices, then fire its event
static void enter(DialogRunner *r, int node) {
    const DialogBank *b = r->bank;
    r->serial++;
    r->choice_count = 0;
    if (node < 0 || node >= b->n_nodes) { r->node = -1; r->text[0] = '\0'; return; }
    r->node = node;
    const uint8_t *n = b->nodes + node * NODE_BYTES;
    fill_vars(r, str_at(b, rd32(n)), r->text, sizeof(r->text));
    uint16_t spk = rd16(n + 8);
    r->speaker[0] = '\0';
    if (spk != NONE16)
        fill_vars(r, str_at(b, rd32(b->speakers + spk * SPEAKER_BYTES)), r->speaker, sizeof(r->speaker));

    int fc = rd16(n + 12), cc = n[14];
    for (int i = 0; i < cc; i++) {
        const uint8_t *c = b->choices + (fc + i) * CHOICE_BYTES;
        const char *cond = str_at(b, rd32(c + 4));
        if (cond && r->hooks.check && !r->hooks.check(cond, r->hooks.ctx)) continue;
        int k = r->choice_count++;
        uint16_t next = rd16(c + 8);
        r->choice_next[k] = next == NONE16 ? -1 : next;
        fill_vars(r, str_at(b, rd32(c)), r->choice_text[k], DIALOG_CHOICE_TEXT);
    }

    const char *event = str_at(b, rd32(n + 4));
    if (event && r->hooks.event) r->hooks.event(event, r->hooks.ctx);
}

bool dialog_start(DialogRunner *r, const DialogBank *bank, const char *conversation,
                  const DialogHooks *hooks) {
    memset(r, 0, sizeof(*r));
    r->node = -1;
    if (!bank || !conversation) return false;
    r->bank = bank;
    if (hooks) r->hooks = *hooks;
    for (int i = 0; i < bank->n_convs; i++) {
        const uint8_t *c = bank->convs + i * CONV_BYTES;
        if (strcmp(str_at(bank, rd32(c)), conversation) == 0) {
            enter(r, rd16(c + 4));
            return true;
        }
    }
    return false;
}

bool dialog_running(const DialogRunner *r) { return r->bank && r->node >= 0; }

void dialog_stop(DialogRunner *r) { r->node = -1; r->serial++; }

const char *dialog_speaker(const DialogRunner *r) {
    return dialog_running(r) ? r->speaker : "";
}

int dialog_speaker_color(const DialogRunner *r) {
    if (!dialog_running(r)) return DIALOG_COL_TITLE;
    uint16_t spk = rd16(r->bank->nodes + r->node * NODE_BYTES + 8);
    return spk == NONE16 ? DIALOG_COL_TITLE : r->bank->speakers[spk * SPEAKER_BYTES + 4];
}

const char *dialog_text(const DialogRunner *r) { return dialog_running(r) ? r->text : ""; }

void dialog_advance(DialogRunner *r) {
    if (!dialog_running(r) || r->choice_count > 0) return;
    uint16_t next = rd16(r->bank->nodes + r->node * NODE_BYTES + 10);
    enter(r, next == NONE16 ? -1 : next);
}

void dialog_choose(DialogRunner *r, int index) {
    if (!dialog_running(r) || index < 0 || index >= r->choice_count) return;
    enter(r, r->choice_next[index]);
}
