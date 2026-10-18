#ifndef FHW_DEVICE_LISTS_H
#define FHW_DEVICE_LISTS_H

#define UI_DEVICE_LIST_CAPACITY 16
#define UI_NO_DEVICE (-1)

typedef enum {
    UI_ROLE_WHEEL,
    UI_ROLE_PEDALS,
    UI_ROLE_SHIFTER,
    UI_ROLE_HANDBRAKE,
    UI_ROLE_COUNT
} ui_role;

typedef struct {
    unsigned int vid;
    unsigned int pid;
    int force_feedback;
} ui_device_info;

int ui_role_choices(const ui_device_info *devices, int count, const int *selected, ui_role role, int *choices);
void ui_drop_conflicts(int *selected);

#endif
