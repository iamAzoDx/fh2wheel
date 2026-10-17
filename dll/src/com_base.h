#ifndef FHW_COM_BASE_H
#define FHW_COM_BASE_H

#include <windows.h>

#define FHW_STUB_STRIDE 16
#define FHW_DEFINE_STUB_TABLE(table, count, handler)                                                    \
    asm(".text\n"                                                                                       \
        ".balign 16\n"                                                                                  \
        ".globl " #table "\n" #table ":\n"                                                              \
        ".rept " #count "\n"                                                                            \
        ".balign 16\n"                                                                                  \
        "movl $(.-" #table ")/16, %edx\n"                                                               \
        "jmp " #handler "\n"                                                                            \
        ".endr\n")

typedef ULONG (*fhw_reference_function)(void *object);
typedef HRESULT (*fhw_query_function)(void *object, const GUID *interface_id, void **out);

int fhw_is_base_interface(const GUID *interface_id);
HRESULT fhw_inspectable_no_iids(void *self, ULONG *count, GUID **interface_ids);
HRESULT fhw_inspectable_trust(void *self, int *trust);
void fhw_fill_vtable(void **vtable, unsigned int count, const unsigned char *stubs, void *const *known);
void fhw_format_guid(const GUID *guid, char *out, size_t capacity);
ULONG fhw_com_add_ref(void *object);
ULONG fhw_com_release(void *object);
HRESULT fhw_com_query(void *object, const GUID *interface_id, void **out);
void *fhw_com_slot(void *object, unsigned int slot);

#endif
