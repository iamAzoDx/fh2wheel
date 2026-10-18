#include <stdio.h>
#include <wchar.h>

#include "ui_text.h"

static int failures = 0;

static void expect_true(const char *what, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static void test_every_text_is_defined(void)
{
    int index;

    expect_true("text table complete", ui_text_complete());
    for (index = 0; index < UI_AXIS_ROWS; index++)
        expect_true("axis label and hint", wcslen(ui_axis_label(index)) > 0 && wcslen(ui_axis_hint(index)) > 0);
    for (index = 0; index < FHW_BUTTON_COUNT; index++)
        expect_true("button label", wcslen(ui_button_label((fhw_button)index)) > 0);
    for (index = 0; index < UI_HAT_DIRECTIONS; index++)
        expect_true("hat name", wcscmp(ui_hat_name(index), L"?") != 0);
    for (index = 0; index < FHW_SHIFTER_COUNT; index++)
        expect_true("shifter label", wcslen(ui_shifter_label((fhw_shifter_control)index)) > 0);
}

static void test_out_of_range(void)
{
    expect_true("out-of-range text is empty", wcscmp(ui_text(T_COUNT), L"") == 0);
    expect_true("out-of-range axis", wcscmp(ui_axis_name(FHW_AXIS_COUNT), L"?") == 0);
    expect_true("out-of-range hat", wcscmp(ui_hat_name(UI_HAT_DIRECTIONS), L"?") == 0);
    expect_true("out-of-range shifter", wcscmp(ui_shifter_label(FHW_SHIFTER_COUNT), L"") == 0);
    expect_true("reverse label", wcscmp(ui_shifter_label(FHW_SHIFTER_REVERSE), L"Reverse") == 0);
    expect_true("title", wcsstr(ui_text(T_TITLE), L"wheel setup") != NULL);
}

int main(void)
{
    test_every_text_is_defined();
    test_out_of_range();
    if (failures != 0) {
        printf("test_ui_text: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ui_text: OK\n");
    return 0;
}
