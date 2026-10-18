#include <stdio.h>

#include "device_lists.h"

#define R9 0
#define SHIFTER 1
#define HANDBRAKE 2
#define PEDALS 3

static int failures = 0;

static void expect_true(const char *what, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static int listed(const int *choices, int count, int device)
{
    int index;

    for (index = 0; index < count; index++) {
        if (choices[index] == device)
            return 1;
    }
    return 0;
}

static const ui_device_info devices[] = {
    {0x346E, 0x0002, 1}, {0x044F, 0xB660, 0}, {0x1234, 0x0001, 0}, {0x1234, 0x0002, 0}};

static void test_wheel_list_only_shows_force_feedback_devices(void)
{
    int selected[UI_ROLE_COUNT] = {R9, -1, -1, -1};
    int choices[UI_DEVICE_LIST_CAPACITY];
    int count = ui_role_choices(devices, 4, selected, UI_ROLE_WHEEL, choices);

    expect_true("wheel list has the R9", listed(choices, count, R9));
    expect_true("wheel list hides the shifter", !listed(choices, count, SHIFTER));
    expect_true("wheel list hides the handbrake", !listed(choices, count, HANDBRAKE));
}

static void test_wheel_list_falls_back_without_force_feedback(void)
{
    static const ui_device_info plain[] = {{0x1111, 0x0001, 0}, {0x2222, 0x0002, 0}};
    int selected[UI_ROLE_COUNT] = {-1, -1, -1, -1};
    int choices[UI_DEVICE_LIST_CAPACITY];
    int count = ui_role_choices(plain, 2, selected, UI_ROLE_WHEEL, choices);

    expect_true("no force feedback anywhere -> every device", count == 2);
}

static void test_selected_wheel_without_force_feedback_stays_listed(void)
{
    int selected[UI_ROLE_COUNT] = {PEDALS, -1, -1, -1};
    int choices[UI_DEVICE_LIST_CAPACITY];
    int count = ui_role_choices(devices, 4, selected, UI_ROLE_WHEEL, choices);

    expect_true("current wheel kept in its list", listed(choices, count, PEDALS));
}

static void test_other_lists_hide_devices_used_elsewhere(void)
{
    int selected[UI_ROLE_COUNT] = {R9, -1, SHIFTER, HANDBRAKE};
    int choices[UI_DEVICE_LIST_CAPACITY];
    int count;

    count = ui_role_choices(devices, 4, selected, UI_ROLE_SHIFTER, choices);
    expect_true("shifter list hides the wheel", !listed(choices, count, R9));
    expect_true("shifter list keeps its own device", listed(choices, count, SHIFTER));
    expect_true("shifter list hides the handbrake", !listed(choices, count, HANDBRAKE));
    expect_true("shifter list offers free devices", listed(choices, count, PEDALS));

    count = ui_role_choices(devices, 4, selected, UI_ROLE_HANDBRAKE, choices);
    expect_true("handbrake list hides the shifter", !listed(choices, count, SHIFTER));
    expect_true("handbrake list keeps its own device", listed(choices, count, HANDBRAKE));

    count = ui_role_choices(devices, 4, selected, UI_ROLE_WHEEL, choices);
    expect_true("wheel list hides the shifter even when chosen", !listed(choices, count, SHIFTER));
}

static void test_conflicts_are_dropped(void)
{
    int selected[UI_ROLE_COUNT] = {R9, R9, SHIFTER, SHIFTER};

    ui_drop_conflicts(selected);
    expect_true("wheel kept", selected[UI_ROLE_WHEEL] == R9);
    expect_true("pedals on the wheel -> none", selected[UI_ROLE_PEDALS] == -1);
    expect_true("first extra role keeps the device", selected[UI_ROLE_SHIFTER] == SHIFTER);
    expect_true("later duplicate -> none", selected[UI_ROLE_HANDBRAKE] == -1);
}

int main(void)
{
    test_wheel_list_only_shows_force_feedback_devices();
    test_wheel_list_falls_back_without_force_feedback();
    test_selected_wheel_without_force_feedback_stays_listed();
    test_other_lists_hide_devices_used_elsewhere();
    test_conflicts_are_dropped();
    if (failures != 0) {
        printf("test_device_lists: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_device_lists: OK\n");
    return 0;
}
