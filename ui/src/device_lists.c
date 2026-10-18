#include "device_lists.h"

static int ui_used_by_another_role(const int *selected, ui_role role, int device)
{
    int other;

    for (other = 0; other < UI_ROLE_COUNT; other++) {
        if (other != (int)role && selected[other] == device)
            return 1;
    }
    return 0;
}

static int ui_any_force_feedback(const ui_device_info *devices, int count)
{
    int index;

    for (index = 0; index < count; index++) {
        if (devices[index].force_feedback)
            return 1;
    }
    return 0;
}

int ui_role_choices(const ui_device_info *devices, int count, const int *selected, ui_role role, int *choices)
{
    int filter_wheels = role == UI_ROLE_WHEEL && ui_any_force_feedback(devices, count);
    int listed = 0;
    int index;

    for (index = 0; index < count && listed < UI_DEVICE_LIST_CAPACITY; index++) {
        if (ui_used_by_another_role(selected, role, index))
            continue;
        if (filter_wheels && !devices[index].force_feedback && selected[role] != index)
            continue;
        choices[listed++] = index;
    }
    return listed;
}

void ui_drop_conflicts(int *selected)
{
    int role;
    int earlier;

    for (role = UI_ROLE_PEDALS; role < UI_ROLE_COUNT; role++) {
        for (earlier = UI_ROLE_WHEEL; earlier < role; earlier++) {
            if (selected[role] != UI_NO_DEVICE && selected[role] == selected[earlier])
                selected[role] = UI_NO_DEVICE;
        }
    }
}
