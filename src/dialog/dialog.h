#ifndef DIALOG_H
#define DIALOG_H

// Dialog: conversations compiled from JSON (tools/dialog_build.py) into a
// .dlg bank, and a runner that walks one conversation: lines, speakers,
// choices, events and variables. Pure C (no rendering): the text box
// (src/ui/textbox.h) shows what the runner holds. See docs/DIALOG.md.

#include <stdbool.h>
#include <stdint.h>

#define DIALOG_TEXT_MAX     512    // bytes of one line after variables are filled in
#define DIALOG_CHOICE_MAX     4
#define DIALOG_CHOICE_TEXT   48

// Style palette ids used by compiled text (^NN); text.c / textbox.c bind them
// to the UiStyle's colours
enum { DIALOG_COL_TEXT = 2, DIALOG_COL_ACCENT, DIALOG_COL_HILITE, DIALOG_COL_TITLE, DIALOG_COL_DIM };

// In-text control bytes (compiled from [p=ms] and [page])
#define DIALOG_CTRL_PAUSE  0x01    // followed by one byte: pause in units of 20 ms
#define DIALOG_CTRL_PAGE   0x02    // forced page break
#define DIALOG_PAUSE_UNIT_MS 20

typedef struct DialogBank DialogBank;

// Load a compiled bank ("rom:/dialog/demo.dlg"); NULL on error (debugf says why)
DialogBank *dialog_bank_load(const char *path);
// Parse a bank from memory (copied). err gets a message on failure.
DialogBank *dialog_bank_parse(const void *data, int size, char *err, int errlen);
void        dialog_bank_free(DialogBank *bank);
int         dialog_bank_conversations(const DialogBank *bank);

// Game hooks. Any may be NULL. Events fire when a line starts; check()
// decides choices with an "if"; variable() fills {name} (unknown names stay
// as written). ctx is passed through.
typedef struct {
    void (*event)(const char *event, void *ctx);
    bool (*check)(const char *cond, void *ctx);
    bool (*variable)(const char *name, char *out, int outlen, void *ctx);
    void *ctx;
} DialogHooks;

typedef struct {
    const DialogBank *bank;
    DialogHooks hooks;
    int  node;                                 // current line, -1 when finished
    char speaker[DIALOG_CHOICE_TEXT];          // speaker name, variables filled in
    char text[DIALOG_TEXT_MAX];                // the line, variables filled in
    int  choice_count;                         // choices whose condition passed
    int  choice_next[DIALOG_CHOICE_MAX];
    char choice_text[DIALOG_CHOICE_MAX][DIALOG_CHOICE_TEXT];
    uint32_t serial;                           // +1 whenever the line changes
} DialogRunner;

// Start a conversation by id; false if the bank has no such conversation
bool dialog_start(DialogRunner *r, const DialogBank *bank, const char *conversation,
                  const DialogHooks *hooks);
bool dialog_running(const DialogRunner *r);
void dialog_stop(DialogRunner *r);

const char *dialog_speaker(const DialogRunner *r);     // name, or "" for none
int         dialog_speaker_color(const DialogRunner *r);  // DIALOG_COL_*
const char *dialog_text(const DialogRunner *r);        // compiled text (with ^NN and control bytes)

// After the line has been shown: continue (lines without choices) or pick
void dialog_advance(DialogRunner *r);
void dialog_choose(DialogRunner *r, int index);

#endif
