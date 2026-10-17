#include <windows.h>
#include <stdio.h>

#include "com_base.h"

#define FHW_BASE_TRUST 0
#define FHW_QUERY_SLOT 0u
#define FHW_ADD_REF_SLOT 1u
#define FHW_RELEASE_SLOT 2u

static const GUID fhw_iid_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID fhw_iid_inspectable = {0xAF86E2E0, 0xB12D, 0x4C6A,
                                         {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
static const GUID fhw_iid_agile_object = {0x94EA2B94, 0xE9CC, 0x49E0,
                                          {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};

int fhw_is_base_interface(const GUID *interface_id)
{
    return IsEqualGUID(interface_id, &fhw_iid_unknown) || IsEqualGUID(interface_id, &fhw_iid_inspectable) ||
           IsEqualGUID(interface_id, &fhw_iid_agile_object);
}

HRESULT fhw_inspectable_no_iids(void *self, ULONG *count, GUID **interface_ids)
{
    (void)self;
    if (count == NULL || interface_ids == NULL)
        return E_POINTER;
    *count = 0;
    *interface_ids = NULL;
    return S_OK;
}

HRESULT fhw_inspectable_trust(void *self, int *trust)
{
    (void)self;
    if (trust == NULL)
        return E_POINTER;
    *trust = FHW_BASE_TRUST;
    return S_OK;
}

void fhw_fill_vtable(void **vtable, unsigned int count, const unsigned char *stubs, void *const *known)
{
    unsigned int index;

    for (index = 0; index < count; index++)
        vtable[index] = known[index] != NULL ? known[index] : (void *)(stubs + (size_t)index * FHW_STUB_STRIDE);
}

void fhw_format_guid(const GUID *guid, char *out, size_t capacity)
{
    snprintf(out, capacity, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", (unsigned long)guid->Data1,
             guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4],
             guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

void *fhw_com_slot(void *object, unsigned int slot)
{
    return (*(void *const **)object)[slot];
}

ULONG fhw_com_add_ref(void *object)
{
    return ((fhw_reference_function)fhw_com_slot(object, FHW_ADD_REF_SLOT))(object);
}

ULONG fhw_com_release(void *object)
{
    return ((fhw_reference_function)fhw_com_slot(object, FHW_RELEASE_SLOT))(object);
}

HRESULT fhw_com_query(void *object, const GUID *interface_id, void **out)
{
    return ((fhw_query_function)fhw_com_slot(object, FHW_QUERY_SLOT))(object, interface_id, out);
}
