#include <math.h>
#include <stdio.h>
#include <wchar.h>

#include "preview_model.h"

static int failures = 0;

static void expect_near(const char *what, float actual, float expected)
{
    if (fabsf(actual - expected) > 1e-3f) {
        printf("FAIL %s: got %f, expected %f\n", what, actual, expected);
        failures++;
    }
}

static void expect_text(const char *what, const wchar_t *actual, const wchar_t *expected)
{
    if (wcscmp(actual, expected) != 0) {
        printf("FAIL %s: got '%ls', expected '%ls'\n", what, actual, expected);
        failures++;
    }
}

static void test_wheel_angle(void)
{
    expect_near("centered", ui_wheel_angle(0.0f, 900), 0.0f);
    expect_near("full right on 900", ui_wheel_angle(1.0f, 900), 450.0f);
    expect_near("half left on 540", ui_wheel_angle(-0.5f, 540), -135.0f);
    expect_near("out of range clamped", ui_wheel_angle(2.0f, 900), 450.0f);
}

static void test_percent(void)
{
    expect_near("released", (float)ui_percent(0.0f), 0.0f);
    expect_near("rounded", (float)ui_percent(0.426f), 43.0f);
    expect_near("clamped high", (float)ui_percent(1.4f), 100.0f);
    expect_near("clamped low", (float)ui_percent(-0.2f), 0.0f);
}

static void test_gear_label(void)
{
    wchar_t text[UI_GEAR_LABEL_CAPACITY];

    ui_gear_label(0, 1, text);
    expect_text("neutral", text, L"N");
    ui_gear_label(3, 1, text);
    expect_text("third", text, L"3");
    ui_gear_label(0xFF, 1, text);
    expect_text("reverse", text, L"R");
    ui_gear_label(0, 0, text);
    expect_text("no H-shifter", text, L"–");
}

int main(void)
{
    test_wheel_angle();
    test_percent();
    test_gear_label();
    if (failures != 0) {
        printf("test_preview_model: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_preview_model: OK\n");
    return 0;
}
