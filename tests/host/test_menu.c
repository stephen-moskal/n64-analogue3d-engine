#include "test.h"
#include "ui/menu.h"
#include <string.h>

// Menu model (menu.c): cursor, values, tabs, scrolling, cancel/revert.

static const char *opts3[] = {"A", "B", "C"};

static void build(Menu *m, int items) {
    menu_init(m, "Test");
    int t0 = menu_add_tab(m, "One");
    menu_add_tab(m, "Two");
    for (int i = 0; i < items; i++) menu_add_item(m, t0, "Item", opts3, 3, 0);
    menu_add_item(m, 1, "Other", opts3, 3, 2);
}

static void test_menu_cursor(void) {
    Menu m;
    build(&m, 4);
    menu_item_set_disabled(&m, 0, 0, true);
    menu_open(&m);
    CHECK(m.tabs[0].cursor == 1);                 // opens on the first enabled item
    menu_move_cursor(&m, 1);
    menu_move_cursor(&m, 1);
    CHECK(m.tabs[0].cursor == 3);
    menu_move_cursor(&m, 1);
    CHECK(m.tabs[0].cursor == 0);                 // wraps onto the disabled item 0 (visitable)
    menu_change_value(&m, 1);
    CHECK(menu_get_value(&m, 0, 0) == 0);          // ...but it cannot change
    menu_move_cursor(&m, -1);
    CHECK(m.tabs[0].cursor == 3);
}

static void test_menu_scroll(void) {
    Menu m;
    build(&m, 10);
    menu_open(&m);
    for (int i = 0; i < 8; i++) menu_move_cursor(&m, 1);
    CHECK(m.tabs[0].cursor == 8);
    CHECK(m.tabs[0].scroll_offset == 8 - MENU_VISIBLE_ITEMS + 1);
    menu_move_cursor(&m, 1);
    menu_move_cursor(&m, 1);                       // wraps to 0
    CHECK(m.tabs[0].cursor == 0);
    CHECK(m.tabs[0].scroll_offset == 0);
}

static void test_menu_values_and_tabs(void) {
    Menu m;
    build(&m, 3);
    menu_open(&m);
    menu_change_value(&m, -1);
    CHECK(menu_get_value(&m, 0, 0) == 2);          // wraps backwards
    menu_change_value(&m, 1);
    CHECK(menu_get_value(&m, 0, 0) == 0);
    menu_switch_tab(&m, -1);
    CHECK(m.active_tab == 1);                      // wraps
    menu_change_value(&m, 1);
    CHECK(menu_get_value(&m, 1, 0) == 0);          // 2 -> 0

    menu_item_set_disabled(&m, 1, 0, true);
    menu_change_value(&m, 1);
    CHECK(menu_get_value(&m, 1, 0) == 0);          // disabled items don't change

    menu_set_value(&m, 0, 1, 2);
    CHECK(menu_get_value(&m, 0, 1) == 2);
    menu_set_value(&m, 0, 1, 7);                   // out of range: ignored
    CHECK(menu_get_value(&m, 0, 1) == 2);
}

// One frame of UI input at a time (what action_ui() hands the menu)
static void test_menu_update(void) {
    Menu m;
    build(&m, 3);
    menu_open(&m);
    UiInput in = { .down = true };
    menu_update(&m, &in);
    CHECK(m.tabs[0].cursor == 1);
    in = (UiInput){ .right = true };
    menu_update(&m, &in);
    CHECK(menu_get_value(&m, 0, 1) == 1);
    in = (UiInput){ .next_tab = true };
    menu_update(&m, &in);
    CHECK(m.active_tab == 1);
    in = (UiInput){ .start = true };
    menu_update(&m, &in);
    CHECK(m.is_open);                              // Start is the caller's
    in = (UiInput){ .cancel = true };
    menu_update(&m, &in);
    CHECK(!m.is_open && menu_get_value(&m, 0, 1) == 0);   // reverted

    menu_open(&m);
    in = (UiInput){ .left = true };
    menu_update(&m, &in);                          // item 0 wraps 0 -> 2
    in = (UiInput){ .confirm = true };
    menu_update(&m, &in);
    CHECK(!m.is_open && menu_get_value(&m, 0, 0) == 2);   // applied
    in = (UiInput){ .down = true };
    menu_update(&m, &in);                          // closed: ignored
    CHECK(!m.is_open && m.tabs[0].cursor == 0);
    menu_update(&m, NULL);
}

static void test_menu_revert(void) {
    Menu m;
    build(&m, 3);
    menu_open(&m);
    menu_change_value(&m, 1);
    menu_close(&m, false);                         // cancel
    CHECK(menu_get_value(&m, 0, 0) == 0);
    CHECK(!m.is_open);
    menu_open(&m);
    menu_change_value(&m, 1);
    menu_close(&m, true);                          // apply
    CHECK(menu_get_value(&m, 0, 0) == 1);
}

static void test_menu_limits(void) {
    Menu m;
    menu_init(&m, "Full");
    for (int t = 0; t < MENU_MAX_TABS; t++) CHECK(menu_add_tab(&m, "T") == t);
    CHECK(menu_add_tab(&m, "T") == -1);
    for (int i = 0; i < MENU_MAX_ITEMS; i++) CHECK(menu_add_item(&m, 0, "I", opts3, 3, 0) == i);
    CHECK(menu_add_item(&m, 0, "I", opts3, 3, 0) == -1);
    CHECK(menu_add_item(&m, 9, "I", opts3, 3, 0) == -1);
}

void run_menu_tests(void) {
    RUN_TEST(test_menu_cursor);
    RUN_TEST(test_menu_scroll);
    RUN_TEST(test_menu_values_and_tabs);
    RUN_TEST(test_menu_update);
    RUN_TEST(test_menu_revert);
    RUN_TEST(test_menu_limits);
}
