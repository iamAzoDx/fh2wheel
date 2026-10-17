#include <windows.h>
#include <stddef.h>
#include <stdio.h>

#include "com_base.h"
#include "force_loop.h"
#include "log.h"
#include "targets.h"
#include "wheel_controller.h"
#include "winrt_string.h"

#define FHW_SLOT_LOG_CAPACITY 64
#define FHW_GUID_TEXT_CAPACITY 40
#define FHW_STATIC_REFERENCES 1

typedef struct {
    void *const *controller_vtable;
    void *const *wheel_vtable;
    volatile LONG references;
} fhw_wheel_object;

typedef struct {
    void *const *vtable;
    volatile LONG references;
    fhw_wheel_input input;
    unsigned long long timestamp;
} fhw_reading_object;

typedef struct {
    void *const *vtable;
} fhw_settings_object;

typedef enum {
    FHW_KIND_CONTROLLER,
    FHW_KIND_WHEEL,
    FHW_KIND_READING,
    FHW_KIND_SETTINGS,
    FHW_KIND_COUNT
} fhw_object_kind;

extern const unsigned char fhw_controller_stubs[];
extern const unsigned char fhw_wheel_stubs[];
extern const unsigned char fhw_reading_stubs[];
extern const unsigned char fhw_settings_stubs[];

HRESULT fhw_controller_unknown(void *self, unsigned int slot);
HRESULT fhw_wheel_unknown(void *self, unsigned int slot);
HRESULT fhw_reading_unknown(void *self, unsigned int slot);
HRESULT fhw_settings_unknown(void *self, unsigned int slot);

FHW_DEFINE_STUB_TABLE(fhw_controller_stubs, 16, fhw_controller_unknown);
FHW_DEFINE_STUB_TABLE(fhw_wheel_stubs, 48, fhw_wheel_unknown);
FHW_DEFINE_STUB_TABLE(fhw_reading_stubs, 40, fhw_reading_unknown);
FHW_DEFINE_STUB_TABLE(fhw_settings_stubs, 24, fhw_settings_unknown);

static const char *const fhw_kind_names[FHW_KIND_COUNT] = {"controller", "wheel", "reading", "settings"};
static volatile LONG fhw_unknown_calls[FHW_KIND_COUNT][FHW_SLOT_LOG_CAPACITY];
static volatile LONG fhw_query_logs = 0;
static volatile LONG fhw_program_logs = 0;
static volatile LONG fhw_variable_logs = 0;
static volatile LONG fhw_reading_logs = 0;

static const GUID fhw_iid_wheel = FHW_IID_WHEEL;
static const GUID fhw_iid_gamepad = FHW_IID_GAMEPAD;

static void *fhw_controller_vtable[FHW_CONTROLLER_SLOT_COUNT];
static void *fhw_wheel_vtable[FHW_WHEEL_SLOT_COUNT];
static void *fhw_reading_vtable[FHW_READING_SLOT_COUNT];
static void *fhw_settings_vtable[FHW_SETTINGS_SLOT_COUNT];

static fhw_wheel_object fhw_wheel_instance;
static fhw_settings_object fhw_settings_instance;
static fhw_config fhw_wheel_config;
static fhw_user_provider fhw_provide_user = NULL;
static volatile LONG fhw_rotation_request = 0;

static HRESULT fhw_note_unknown(fhw_object_kind kind, unsigned int slot)
{
    LONG calls = slot < FHW_SLOT_LOG_CAPACITY ? InterlockedIncrement(&fhw_unknown_calls[kind][slot]) : 1;

    if (calls <= FHW_UNKNOWN_SLOT_LOG_LIMIT)
        fhw_log_format("%s: unknown method slot %u (offset 0x%X) called, returned S_OK without effect", fhw_kind_names[kind],
                       slot, slot * 8u);
    return S_OK;
}

HRESULT fhw_controller_unknown(void *self, unsigned int slot)
{
    (void)self;
    return fhw_note_unknown(FHW_KIND_CONTROLLER, slot);
}

HRESULT fhw_wheel_unknown(void *self, unsigned int slot)
{
    (void)self;
    return fhw_note_unknown(FHW_KIND_WHEEL, slot);
}

HRESULT fhw_reading_unknown(void *self, unsigned int slot)
{
    (void)self;
    return fhw_note_unknown(FHW_KIND_READING, slot);
}

HRESULT fhw_settings_unknown(void *self, unsigned int slot)
{
    (void)self;
    return fhw_note_unknown(FHW_KIND_SETTINGS, slot);
}

static fhw_wheel_object *fhw_from_wheel_face(void *self)
{
    return (fhw_wheel_object *)((unsigned char *)self - offsetof(fhw_wheel_object, wheel_vtable));
}

static HRESULT fhw_wheel_object_query(fhw_wheel_object *object, const GUID *interface_id, void **out)
{
    char text[FHW_GUID_TEXT_CAPACITY];

    if (out == NULL)
        return E_POINTER;
    *out = NULL;
    if (fhw_is_base_interface(interface_id)) {
        *out = &object->controller_vtable;
    } else if (IsEqualGUID(interface_id, &fhw_iid_wheel)) {
        *out = &object->wheel_vtable;
    } else {
        if (!IsEqualGUID(interface_id, &fhw_iid_gamepad) &&
            InterlockedIncrement(&fhw_query_logs) <= FHW_QUERY_LOG_LIMIT) {
            fhw_format_guid(interface_id, text, sizeof(text));
            fhw_log_format("controller: interface %s requested -- refused", text);
        }
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&object->references);
    return S_OK;
}

static ULONG fhw_wheel_object_add_ref(fhw_wheel_object *object)
{
    return (ULONG)InterlockedIncrement(&object->references);
}

static ULONG fhw_wheel_object_release(fhw_wheel_object *object)
{
    LONG remaining = InterlockedDecrement(&object->references);

    if (remaining < FHW_STATIC_REFERENCES)
        InterlockedExchange(&object->references, FHW_STATIC_REFERENCES);
    return (ULONG)(remaining < FHW_STATIC_REFERENCES ? FHW_STATIC_REFERENCES : remaining);
}

static HRESULT fhw_controller_query(void *self, const GUID *interface_id, void **out)
{
    return fhw_wheel_object_query((fhw_wheel_object *)self, interface_id, out);
}

static ULONG fhw_controller_add_ref(void *self)
{
    return fhw_wheel_object_add_ref((fhw_wheel_object *)self);
}

static ULONG fhw_controller_release(void *self)
{
    return fhw_wheel_object_release((fhw_wheel_object *)self);
}

static HRESULT fhw_wheel_query(void *self, const GUID *interface_id, void **out)
{
    return fhw_wheel_object_query(fhw_from_wheel_face(self), interface_id, out);
}

static ULONG fhw_wheel_add_ref(void *self)
{
    return fhw_wheel_object_add_ref(fhw_from_wheel_face(self));
}

static ULONG fhw_wheel_release(void *self)
{
    return fhw_wheel_object_release(fhw_from_wheel_face(self));
}

static HRESULT fhw_controller_class_name(void *self, void **name)
{
    (void)self;
    if (name == NULL)
        return E_POINTER;
    return fhw_create_game_string(FHW_CONTROLLER_RUNTIME_CLASS, name);
}

static HRESULT fhw_controller_get_id(void *self, unsigned long long *id)
{
    (void)self;
    if (id == NULL)
        return E_POINTER;
    *id = FHW_CONTROLLER_ID;
    return S_OK;
}

static HRESULT fhw_controller_get_type(void *self, void **type)
{
    (void)self;
    if (type == NULL)
        return E_POINTER;
    return fhw_create_game_string(FHW_CONTROLLER_TYPE, type);
}

static HRESULT fhw_controller_get_user(void *self, void **user)
{
    static volatile LONG logged = 0;
    HRESULT result;

    (void)self;
    if (user == NULL)
        return E_POINTER;
    *user = NULL;
    result = fhw_provide_user != NULL ? fhw_provide_user(user) : E_FAIL;
    if (InterlockedIncrement(&logged) == 1)
        fhw_log_format("controller: user requested -> %08lX (%p)", (unsigned long)result, *user);
    return S_OK;
}

static HRESULT fhw_wheel_device_id(void *self, unsigned int *id)
{
    static volatile LONG logged = 0;

    (void)self;
    if (id == NULL)
        return E_POINTER;
    *id = fhw_wheel_config.reported_device_id;
    if (InterlockedIncrement(&logged) == 1)
        fhw_log_format("wheel: id requested -> %08X", *id);
    return S_OK;
}

static HRESULT fhw_wheel_device_revision(void *self, unsigned int *revision)
{
    (void)self;
    if (revision == NULL)
        return E_POINTER;
    *revision = FHW_DEFAULT_DEVICE_REVISION;
    return S_OK;
}

static HRESULT fhw_wheel_current_reading(void *self, void **reading)
{
    fhw_reading_object *object;

    (void)self;
    if (reading == NULL)
        return E_POINTER;
    *reading = NULL;
    object = (fhw_reading_object *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (object == NULL)
        return E_OUTOFMEMORY;
    object->vtable = fhw_reading_vtable;
    object->references = 1;
    fhw_force_snapshot(&object->input, &object->timestamp);
    if (InterlockedIncrement(&fhw_reading_logs) == 1)
        fhw_log_step_ok("wheel: first reading requested by the game");
    *reading = object;
    return S_OK;
}

static HRESULT fhw_wheel_settings(void *self, void **settings)
{
    (void)self;
    if (settings == NULL)
        return E_POINTER;
    *settings = &fhw_settings_instance;
    return S_OK;
}

static HRESULT fhw_wheel_set_rotation(void *self, unsigned int degrees)
{
    (void)self;
    if (InterlockedExchange(&fhw_rotation_request, (LONG)degrees) != (LONG)degrees)
        fhw_log_format("wheel: the game asks for %u degrees of rotation (not applied to the base)", degrees);
    return S_OK;
}

static HRESULT fhw_wheel_load_program(void *self, unsigned char slot, void *text, unsigned int flags,
                                      unsigned int *result)
{
    char program[FHW_VM_PROGRAM_TEXT_LIMIT];
    int loaded;

    (void)self;
    (void)result;
    if (!fhw_game_string_ascii(text, program, sizeof(program))) {
        fhw_log_format("wheel: program %u unreadable or too long", slot);
        return S_OK;
    }
    fhw_force_lock();
    loaded = fhw_vm_load(fhw_force_vm(), slot, program);
    fhw_force_unlock();
    if (InterlockedIncrement(&fhw_program_logs) <= FHW_PROGRAM_LOG_LIMIT) {
        if (loaded)
            fhw_log_format("wheel: program %u loaded (option %u): %s", slot, flags, program);
        else
            fhw_log_format("wheel: program %u REFUSED (%s): %s", slot, fhw_force_vm()->last_error, program);
    }
    return S_OK;
}

static HRESULT fhw_wheel_enable_program(void *self, unsigned char slot)
{
    int enabled;

    (void)self;
    fhw_force_lock();
    enabled = fhw_vm_enable(fhw_force_vm(), slot, fhw_force_now_ms());
    fhw_force_unlock();
    if (InterlockedIncrement(&fhw_program_logs) <= FHW_PROGRAM_LOG_LIMIT)
        fhw_log_format("wheel: program %u enabled -> %d", slot, enabled);
    return S_OK;
}

static HRESULT fhw_wheel_commit(void *self)
{
    static volatile LONG logged = 0;

    (void)self;
    if (InterlockedIncrement(&logged) <= FHW_PROGRAM_LOG_LIMIT)
        fhw_log_step_ok("wheel: programs committed by the game");
    return S_OK;
}

static HRESULT fhw_wheel_declare_variable(void *self, void *name)
{
    char text[FHW_VM_VARIABLE_NAME_LIMIT + 1];

    (void)self;
    if (!fhw_game_string_ascii(name, text, sizeof(text)))
        return S_OK;
    fhw_force_lock();
    fhw_vm_declare(fhw_force_vm(), text);
    fhw_force_unlock();
    return S_OK;
}

static HRESULT fhw_wheel_set_variable(void *self, void *name, float value)
{
    char text[FHW_VM_VARIABLE_NAME_LIMIT + 1];

    (void)self;
    if (!fhw_game_string_ascii(name, text, sizeof(text)))
        return S_OK;
    fhw_force_lock();
    fhw_vm_set(fhw_force_vm(), text, value);
    fhw_force_unlock();
    if (fhw_wheel_config.trace && InterlockedIncrement(&fhw_variable_logs) <= FHW_VARIABLE_LOG_LIMIT)
        fhw_log_format("wheel: variable %s = %f", text, value);
    return S_OK;
}

static HRESULT fhw_wheel_flush(void *self, unsigned int mode, void *result)
{
    (void)self;
    (void)mode;
    (void)result;
    return S_OK;
}

static HRESULT fhw_wheel_reset(void *self)
{
    (void)self;
    fhw_force_lock();
    fhw_vm_reset(fhw_force_vm());
    fhw_force_unlock();
    if (InterlockedIncrement(&fhw_program_logs) <= FHW_PROGRAM_LOG_LIMIT)
        fhw_log_step_ok("wheel: programs reset by the game");
    return S_OK;
}

static HRESULT fhw_reading_query(void *self, const GUID *interface_id, void **out)
{
    if (out == NULL)
        return E_POINTER;
    *out = NULL;
    if (!fhw_is_base_interface(interface_id))
        return E_NOINTERFACE;
    InterlockedIncrement(&((fhw_reading_object *)self)->references);
    *out = self;
    return S_OK;
}

static ULONG fhw_reading_add_ref(void *self)
{
    return (ULONG)InterlockedIncrement(&((fhw_reading_object *)self)->references);
}

static ULONG fhw_reading_release(void *self)
{
    fhw_reading_object *object = (fhw_reading_object *)self;
    LONG remaining = InterlockedDecrement(&object->references);

    if (remaining == 0)
        HeapFree(GetProcessHeap(), 0, object);
    return (ULONG)remaining;
}

static HRESULT fhw_reading_button(void *self, fhw_button button, unsigned char *pressed)
{
    if (pressed == NULL)
        return E_POINTER;
    *pressed = (((fhw_reading_object *)self)->input.buttons >> button) & 1u;
    return S_OK;
}

static HRESULT fhw_reading_a(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_A, pressed);
}

static HRESULT fhw_reading_b(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_B, pressed);
}

static HRESULT fhw_reading_x(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_X, pressed);
}

static HRESULT fhw_reading_y(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_Y, pressed);
}

static HRESULT fhw_reading_view(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_VIEW, pressed);
}

static HRESULT fhw_reading_menu(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_MENU, pressed);
}

static HRESULT fhw_reading_up(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_UP, pressed);
}

static HRESULT fhw_reading_down(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_DOWN, pressed);
}

static HRESULT fhw_reading_left(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_LEFT, pressed);
}

static HRESULT fhw_reading_right(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_RIGHT, pressed);
}

static HRESULT fhw_reading_paddle_left(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_PADDLE_LEFT, pressed);
}

static HRESULT fhw_reading_paddle_right(void *self, unsigned char *pressed)
{
    return fhw_reading_button(self, FHW_BUTTON_PADDLE_RIGHT, pressed);
}

static HRESULT fhw_reading_wheel(void *self, float *value)
{
    if (value == NULL)
        return E_POINTER;
    *value = ((fhw_reading_object *)self)->input.steering;
    return S_OK;
}

static HRESULT fhw_reading_rotation(void *self, unsigned int *degrees)
{
    (void)self;
    if (degrees == NULL)
        return E_POINTER;
    *degrees = fhw_wheel_config.rotation_degrees;
    return S_OK;
}

static HRESULT fhw_reading_pedal(void *self, fhw_pedal pedal, float *value)
{
    if (value == NULL)
        return E_POINTER;
    *value = ((fhw_reading_object *)self)->input.pedals[pedal];
    return S_OK;
}

static HRESULT fhw_reading_throttle(void *self, float *value)
{
    return fhw_reading_pedal(self, FHW_PEDAL_THROTTLE, value);
}

static HRESULT fhw_reading_brake(void *self, float *value)
{
    return fhw_reading_pedal(self, FHW_PEDAL_BRAKE, value);
}

static HRESULT fhw_reading_clutch(void *self, float *value)
{
    return fhw_reading_pedal(self, FHW_PEDAL_CLUTCH, value);
}

static HRESULT fhw_reading_flag(unsigned char *out, int value)
{
    if (out == NULL)
        return E_POINTER;
    *out = value ? 1 : 0;
    return S_OK;
}

static HRESULT fhw_reading_has_throttle(void *self, unsigned char *present)
{
    (void)self;
    return fhw_reading_flag(present, fhw_wheel_config.pedals[FHW_PEDAL_THROTTLE].axis != FHW_AXIS_NONE);
}

static HRESULT fhw_reading_has_brake(void *self, unsigned char *present)
{
    (void)self;
    return fhw_reading_flag(present, fhw_wheel_config.pedals[FHW_PEDAL_BRAKE].axis != FHW_AXIS_NONE);
}

static HRESULT fhw_reading_has_clutch(void *self, unsigned char *present)
{
    (void)self;
    return fhw_reading_flag(present, fhw_wheel_config.pedals[FHW_PEDAL_CLUTCH].axis != FHW_AXIS_NONE);
}

static HRESULT fhw_reading_has_pattern_shifter(void *self, unsigned char *present)
{
    (void)self;
    return fhw_reading_flag(present, 0);
}

static HRESULT fhw_reading_max_gear(void *self, unsigned char *gear)
{
    (void)self;
    if (gear == NULL)
        return E_POINTER;
    *gear = FHW_MAX_GEAR;
    return S_OK;
}

static HRESULT fhw_reading_gear(void *self, unsigned char *gear)
{
    (void)self;
    if (gear == NULL)
        return E_POINTER;
    *gear = FHW_GEAR_NEUTRAL;
    return S_OK;
}

static HRESULT fhw_reading_timestamp(void *self, unsigned long long *timestamp)
{
    if (timestamp == NULL)
        return E_POINTER;
    *timestamp = ((fhw_reading_object *)self)->timestamp;
    return S_OK;
}

static HRESULT fhw_settings_query(void *self, const GUID *interface_id, void **out)
{
    if (out == NULL)
        return E_POINTER;
    *out = NULL;
    if (!fhw_is_base_interface(interface_id))
        return E_NOINTERFACE;
    *out = self;
    return S_OK;
}

static ULONG fhw_settings_reference(void *self)
{
    (void)self;
    return FHW_STATIC_REFERENCES;
}

static HRESULT fhw_settings_min_rotation(void *self, unsigned int *degrees)
{
    (void)self;
    if (degrees == NULL)
        return E_POINTER;
    *degrees = fhw_wheel_config.rotation_degrees;
    return S_OK;
}

static HRESULT fhw_settings_max_rotation(void *self, unsigned int *degrees)
{
    (void)self;
    if (degrees == NULL)
        return E_POINTER;
    *degrees = fhw_wheel_config.rotation_degrees;
    return S_OK;
}

static HRESULT fhw_settings_can_change_rotation(void *self, unsigned char *allowed)
{
    (void)self;
    return fhw_reading_flag(allowed, 0);
}

static HRESULT fhw_empty_class_name(void *self, void **name)
{
    (void)self;
    if (name == NULL)
        return E_POINTER;
    *name = NULL;
    return S_OK;
}

static void fhw_build_controller_vtable(void)
{
    void *known[FHW_CONTROLLER_SLOT_COUNT] = {0};

    known[0] = (void *)fhw_controller_query;
    known[1] = (void *)fhw_controller_add_ref;
    known[2] = (void *)fhw_controller_release;
    known[3] = (void *)fhw_inspectable_no_iids;
    known[4] = (void *)fhw_controller_class_name;
    known[5] = (void *)fhw_inspectable_trust;
    known[6] = (void *)fhw_controller_get_id;
    known[7] = (void *)fhw_controller_get_type;
    known[8] = (void *)fhw_controller_get_user;
    fhw_fill_vtable(fhw_controller_vtable, FHW_CONTROLLER_SLOT_COUNT, fhw_controller_stubs, known);
}

static void fhw_build_wheel_vtable(void)
{
    void *known[FHW_WHEEL_SLOT_COUNT] = {0};

    known[0] = (void *)fhw_wheel_query;
    known[1] = (void *)fhw_wheel_add_ref;
    known[2] = (void *)fhw_wheel_release;
    known[3] = (void *)fhw_inspectable_no_iids;
    known[4] = (void *)fhw_controller_class_name;
    known[5] = (void *)fhw_inspectable_trust;
    known[10] = (void *)fhw_wheel_device_id;
    known[11] = (void *)fhw_wheel_device_revision;
    known[12] = (void *)fhw_wheel_current_reading;
    known[13] = (void *)fhw_wheel_settings;
    known[14] = (void *)fhw_wheel_set_rotation;
    known[26] = (void *)fhw_wheel_load_program;
    known[30] = (void *)fhw_wheel_enable_program;
    known[33] = (void *)fhw_wheel_commit;
    known[36] = (void *)fhw_wheel_declare_variable;
    known[38] = (void *)fhw_wheel_set_variable;
    known[41] = (void *)fhw_wheel_flush;
    known[42] = (void *)fhw_wheel_reset;
    fhw_fill_vtable(fhw_wheel_vtable, FHW_WHEEL_SLOT_COUNT, fhw_wheel_stubs, known);
}

static void fhw_build_reading_vtable(void)
{
    void *known[FHW_READING_SLOT_COUNT] = {0};

    known[0] = (void *)fhw_reading_query;
    known[1] = (void *)fhw_reading_add_ref;
    known[2] = (void *)fhw_reading_release;
    known[3] = (void *)fhw_inspectable_no_iids;
    known[4] = (void *)fhw_empty_class_name;
    known[5] = (void *)fhw_inspectable_trust;
    known[6] = (void *)fhw_reading_a;
    known[7] = (void *)fhw_reading_b;
    known[8] = (void *)fhw_reading_x;
    known[9] = (void *)fhw_reading_y;
    known[10] = (void *)fhw_reading_view;
    known[11] = (void *)fhw_reading_menu;
    known[12] = (void *)fhw_reading_up;
    known[13] = (void *)fhw_reading_down;
    known[14] = (void *)fhw_reading_left;
    known[15] = (void *)fhw_reading_right;
    known[16] = (void *)fhw_reading_wheel;
    known[17] = (void *)fhw_reading_rotation;
    known[18] = (void *)fhw_reading_throttle;
    known[19] = (void *)fhw_reading_brake;
    known[20] = (void *)fhw_reading_clutch;
    known[24] = (void *)fhw_reading_has_throttle;
    known[25] = (void *)fhw_reading_has_brake;
    known[26] = (void *)fhw_reading_has_clutch;
    known[28] = (void *)fhw_reading_paddle_left;
    known[29] = (void *)fhw_reading_paddle_right;
    known[30] = (void *)fhw_reading_has_pattern_shifter;
    known[31] = (void *)fhw_reading_max_gear;
    known[32] = (void *)fhw_reading_gear;
    known[33] = (void *)fhw_reading_timestamp;
    fhw_fill_vtable(fhw_reading_vtable, FHW_READING_SLOT_COUNT, fhw_reading_stubs, known);
}

static void fhw_build_settings_vtable(void)
{
    void *known[FHW_SETTINGS_SLOT_COUNT] = {0};

    known[0] = (void *)fhw_settings_query;
    known[1] = (void *)fhw_settings_reference;
    known[2] = (void *)fhw_settings_reference;
    known[3] = (void *)fhw_inspectable_no_iids;
    known[4] = (void *)fhw_empty_class_name;
    known[5] = (void *)fhw_inspectable_trust;
    known[11] = (void *)fhw_settings_min_rotation;
    known[12] = (void *)fhw_settings_max_rotation;
    known[14] = (void *)fhw_settings_can_change_rotation;
    fhw_fill_vtable(fhw_settings_vtable, FHW_SETTINGS_SLOT_COUNT, fhw_settings_stubs, known);
}

void fhw_wheel_init(const fhw_config *config, fhw_user_provider user_provider)
{
    fhw_wheel_config = *config;
    fhw_provide_user = user_provider;
    fhw_build_controller_vtable();
    fhw_build_wheel_vtable();
    fhw_build_reading_vtable();
    fhw_build_settings_vtable();
    fhw_wheel_instance.controller_vtable = fhw_controller_vtable;
    fhw_wheel_instance.wheel_vtable = fhw_wheel_vtable;
    fhw_wheel_instance.references = FHW_STATIC_REFERENCES;
    fhw_settings_instance.vtable = fhw_settings_vtable;
}

void *fhw_wheel_controller(void)
{
    InterlockedIncrement(&fhw_wheel_instance.references);
    return &fhw_wheel_instance.controller_vtable;
}
