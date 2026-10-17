#include <windows.h>

#include "com_base.h"
#include "controller_statics.h"
#include "force_loop.h"
#include "log.h"
#include "targets.h"
#include "wheel_controller.h"

#define FHW_GUID_TEXT_CAPACITY 40
#define FHW_STATIC_REFERENCES 1
#define FHW_FIRST_REAL_CONTROLLER 0u
#define FHW_FIRST_USER 0u
#define FHW_E_BOUNDS ((HRESULT)0x8000000BL)

typedef HRESULT (*fhw_get_controllers_function)(void *statics, void **vector);
typedef HRESULT (*fhw_get_at_function)(void *vector, unsigned int index, void **item);
typedef HRESULT (*fhw_get_size_function)(void *vector, unsigned int *size);
typedef HRESULT (*fhw_index_of_function)(void *vector, void *item, unsigned int *index, unsigned char *found);
typedef HRESULT (*fhw_get_user_function)(void *controller, void **user);
typedef HRESULT (*fhw_get_users_function)(void *statics, void **vector);

typedef struct {
    void *const *vtable;
    void *volatile real;
} fhw_statics_wrapper;

typedef struct {
    void *const *vtable;
    volatile LONG references;
    void *real;
} fhw_vector_object;

asm(".text\n"
    ".irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n"
    ".balign 16\n"
    "fhw_statics_forward_\\n:\n"
    "movq 8(%rcx), %rcx\n"
    "movq (%rcx), %rax\n"
    "jmpq *(\\n*8)(%rax)\n"
    ".endr\n"
    ".data\n"
    ".balign 8\n"
    "fhw_statics_forward_table:\n"
    ".irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n"
    ".quad fhw_statics_forward_\\n\n"
    ".endr\n"
    ".text\n");

extern void *const fhw_statics_forward_table[FHW_STATICS_FORWARDED_SLOTS];

static const GUID fhw_iid_controller_statics = FHW_IID_CONTROLLER_STATICS;
static const GUID fhw_iid_vector_view = FHW_IID_CONTROLLER_VECTOR_VIEW;
static const GUID fhw_iid_user_statics = FHW_IID_USER_STATICS;

static void *fhw_statics_vtable[FHW_STATICS_FORWARDED_SLOTS];
static void *fhw_vector_vtable[FHW_VECTOR_SLOT_COUNT];
static fhw_statics_wrapper fhw_wrapper = {fhw_statics_vtable, NULL};
static fhw_activation_function fhw_next_activation = NULL;
static volatile LONG fhw_wrap_logged = 0;
static volatile LONG fhw_absent_logged = 0;
static volatile LONG fhw_enumeration_logs = 0;
static volatile LONG fhw_vector_query_logs = 0;

static HRESULT fhw_real_get_at(void *vector, unsigned int index, void **item)
{
    return ((fhw_get_at_function)fhw_com_slot(vector, FHW_VECTOR_GET_AT_SLOT))(vector, index, item);
}

static HRESULT fhw_real_size(void *vector, unsigned int *size)
{
    *size = 0;
    return ((fhw_get_size_function)fhw_com_slot(vector, FHW_VECTOR_GET_SIZE_SLOT))(vector, size);
}

static HRESULT fhw_vector_query(void *self, const GUID *interface_id, void **out)
{
    fhw_vector_object *vector = (fhw_vector_object *)self;
    char text[FHW_GUID_TEXT_CAPACITY];

    if (out == NULL)
        return E_POINTER;
    if (fhw_is_base_interface(interface_id) || IsEqualGUID(interface_id, &fhw_iid_vector_view)) {
        InterlockedIncrement(&vector->references);
        *out = self;
        return S_OK;
    }
    if (InterlockedIncrement(&fhw_vector_query_logs) <= FHW_QUERY_LOG_LIMIT) {
        fhw_format_guid(interface_id, text, sizeof(text));
        fhw_log_format("controller list: interface %s requested, forwarded to Azimuth (without the wheel)", text);
    }
    return fhw_com_query(vector->real, interface_id, out);
}

static ULONG fhw_vector_add_ref(void *self)
{
    return (ULONG)InterlockedIncrement(&((fhw_vector_object *)self)->references);
}

static ULONG fhw_vector_release(void *self)
{
    fhw_vector_object *vector = (fhw_vector_object *)self;
    LONG remaining = InterlockedDecrement(&vector->references);

    if (remaining == 0) {
        fhw_com_release(vector->real);
        HeapFree(GetProcessHeap(), 0, vector);
    }
    return (ULONG)remaining;
}

static HRESULT fhw_vector_class_name(void *self, void **name)
{
    (void)self;
    if (name == NULL)
        return E_POINTER;
    *name = NULL;
    return S_OK;
}

static HRESULT fhw_vector_get_at(void *self, unsigned int index, void **item)
{
    fhw_vector_object *vector = (fhw_vector_object *)self;
    unsigned int real_size = 0;
    HRESULT result;

    if (item == NULL)
        return E_POINTER;
    *item = NULL;
    result = fhw_real_size(vector->real, &real_size);
    if (FAILED(result))
        return result;
    if (index < real_size)
        return fhw_real_get_at(vector->real, index, item);
    if (index == real_size) {
        *item = fhw_wheel_controller();
        return S_OK;
    }
    return FHW_E_BOUNDS;
}

static HRESULT fhw_vector_get_size(void *self, unsigned int *size)
{
    fhw_vector_object *vector = (fhw_vector_object *)self;
    HRESULT result;

    if (size == NULL)
        return E_POINTER;
    result = fhw_real_size(vector->real, size);
    if (FAILED(result))
        return result;
    *size += 1;
    if (InterlockedIncrement(&fhw_enumeration_logs) <= FHW_QUERY_LOG_LIMIT)
        fhw_log_format("controller list: %u Azimuth controller(s) + the wheel", *size - 1);
    return S_OK;
}

static HRESULT fhw_vector_index_of(void *self, void *item, unsigned int *index, unsigned char *found)
{
    fhw_vector_object *vector = (fhw_vector_object *)self;
    void *wheel = fhw_wheel_controller();
    unsigned int real_size = 0;
    HRESULT result;

    fhw_com_release(wheel);
    if (index == NULL || found == NULL)
        return E_POINTER;
    if (item == wheel) {
        result = fhw_real_size(vector->real, &real_size);
        *index = real_size;
        *found = 1;
        return result;
    }
    return ((fhw_index_of_function)fhw_com_slot(vector->real, FHW_VECTOR_INDEX_OF_SLOT))(vector->real, item, index,
                                                                                       found);
}

static HRESULT fhw_vector_get_many(void *self, unsigned int start, unsigned int capacity, void **items,
                                   unsigned int *actual)
{
    unsigned int size = 0;
    unsigned int index;
    HRESULT result;

    if (actual == NULL || (items == NULL && capacity != 0))
        return E_POINTER;
    *actual = 0;
    result = fhw_vector_get_size(self, &size);
    if (FAILED(result))
        return result;
    for (index = start; index < size && *actual < capacity; index++) {
        result = fhw_vector_get_at(self, index, &items[*actual]);
        if (FAILED(result))
            return result;
        (*actual)++;
    }
    return S_OK;
}

static void *fhw_wrap_vector(void *real)
{
    fhw_vector_object *vector = (fhw_vector_object *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*vector));

    if (vector == NULL)
        return real;
    vector->vtable = fhw_vector_vtable;
    vector->references = 1;
    vector->real = real;
    return vector;
}

static HRESULT fhw_statics_query(void *self, const GUID *interface_id, void **out)
{
    if (out == NULL)
        return E_POINTER;
    if (fhw_is_base_interface(interface_id) || IsEqualGUID(interface_id, &fhw_iid_controller_statics)) {
        *out = self;
        return S_OK;
    }
    return fhw_com_query(fhw_wrapper.real, interface_id, out);
}

static ULONG fhw_statics_reference(void *self)
{
    (void)self;
    return FHW_STATIC_REFERENCES;
}

static HRESULT fhw_statics_get_controllers(void *self, void **vector)
{
    void *real_vector = NULL;
    void *real = fhw_wrapper.real;
    HRESULT result;

    (void)self;
    if (vector == NULL)
        return E_POINTER;
    result = ((fhw_get_controllers_function)fhw_com_slot(real, FHW_STATICS_GET_CONTROLLERS_SLOT))(real, &real_vector);
    if (FAILED(result) || real_vector == NULL) {
        *vector = real_vector;
        return result;
    }
    *vector = fhw_wrap_vector(real_vector);
    return S_OK;
}

static HRESULT fhw_user_from_real_controller(void **user)
{
    void *statics = fhw_wrapper.real;
    void *vector = NULL;
    void *controller = NULL;
    unsigned int size = 0;
    HRESULT result;

    if (statics == NULL)
        return E_FAIL;
    result = ((fhw_get_controllers_function)fhw_com_slot(statics, FHW_STATICS_GET_CONTROLLERS_SLOT))(statics, &vector);
    if (FAILED(result) || vector == NULL)
        return E_FAIL;
    if (SUCCEEDED(fhw_real_size(vector, &size)) && size > 0 &&
        SUCCEEDED(fhw_real_get_at(vector, FHW_FIRST_REAL_CONTROLLER, &controller)) && controller != NULL) {
        result = ((fhw_get_user_function)fhw_com_slot(controller, FHW_CONTROLLER_GET_USER_SLOT))(controller, user);
        fhw_com_release(controller);
    } else {
        result = E_FAIL;
    }
    fhw_com_release(vector);
    return SUCCEEDED(result) && *user != NULL ? S_OK : E_FAIL;
}

static HRESULT fhw_user_from_user_statics(void **user)
{
    void *statics = NULL;
    void *users = NULL;
    unsigned int size = 0;
    HRESULT result;

    if (fhw_next_activation == NULL)
        return E_FAIL;
    result = fhw_next_activation(FHW_USER_CLASS_NAME, &fhw_iid_user_statics, &statics);
    if (FAILED(result) || statics == NULL)
        return E_FAIL;
    result = ((fhw_get_users_function)fhw_com_slot(statics, FHW_USER_STATICS_GET_USERS_SLOT))(statics, &users);
    fhw_com_release(statics);
    if (FAILED(result) || users == NULL)
        return E_FAIL;
    if (SUCCEEDED(fhw_real_size(users, &size)) && size > 0)
        result = fhw_real_get_at(users, FHW_FIRST_USER, user);
    else
        result = E_FAIL;
    fhw_com_release(users);
    return SUCCEEDED(result) && *user != NULL ? S_OK : E_FAIL;
}

static HRESULT fhw_provide_user(void **user)
{
    *user = NULL;
    if (SUCCEEDED(fhw_user_from_real_controller(user)))
        return S_OK;
    return fhw_user_from_user_statics(user);
}

void fhw_statics_init(fhw_activation_function next_activation)
{
    void *known_vector[FHW_VECTOR_SLOT_COUNT] = {0};
    unsigned int index;

    fhw_next_activation = next_activation;
    for (index = 0; index < FHW_STATICS_FORWARDED_SLOTS; index++)
        fhw_statics_vtable[index] = fhw_statics_forward_table[index];
    fhw_statics_vtable[0] = (void *)fhw_statics_query;
    fhw_statics_vtable[1] = (void *)fhw_statics_reference;
    fhw_statics_vtable[2] = (void *)fhw_statics_reference;
    fhw_statics_vtable[FHW_STATICS_GET_CONTROLLERS_SLOT] = (void *)fhw_statics_get_controllers;

    known_vector[0] = (void *)fhw_vector_query;
    known_vector[1] = (void *)fhw_vector_add_ref;
    known_vector[2] = (void *)fhw_vector_release;
    known_vector[3] = (void *)fhw_inspectable_no_iids;
    known_vector[4] = (void *)fhw_vector_class_name;
    known_vector[5] = (void *)fhw_inspectable_trust;
    known_vector[6] = (void *)fhw_vector_get_at;
    known_vector[7] = (void *)fhw_vector_get_size;
    known_vector[8] = (void *)fhw_vector_index_of;
    known_vector[9] = (void *)fhw_vector_get_many;
    for (index = 0; index < FHW_VECTOR_SLOT_COUNT; index++)
        fhw_vector_vtable[index] = known_vector[index];
}

void fhw_statics_wrap(const void *class_name, const void *interface_id, void **factory)
{
    void *real;

    if (factory == NULL || *factory == NULL || class_name == NULL || interface_id == NULL)
        return;
    if (lstrcmpW((const wchar_t *)class_name, FHW_CONTROLLER_CLASS_NAME) != 0)
        return;
    if (!IsEqualGUID((const GUID *)interface_id, &fhw_iid_controller_statics))
        return;
    if (!fhw_force_wheel_present()) {
        if (InterlockedIncrement(&fhw_absent_logged) == 1)
            fhw_log_step_skipped("Controller statics", "no wheel: controller list unchanged");
        return;
    }
    real = *factory;
    if (InterlockedCompareExchangePointer(&fhw_wrapper.real, real, NULL) != NULL)
        fhw_com_release(real);
    *factory = &fhw_wrapper;
    if (InterlockedIncrement(&fhw_wrap_logged) == 1)
        fhw_log_format("Controller statics -- OK (Azimuth %p wrapped, wheel added to the list)", fhw_wrapper.real);
}

fhw_user_provider fhw_statics_user_provider(void)
{
    return fhw_provide_user;
}
