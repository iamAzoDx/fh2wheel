#include <windows.h>
#include <string.h>

#include "config.h"
#include "controller_statics.h"
#include "force_loop.h"
#include "iat_hook.h"
#include "log.h"
#include "memory_region.h"
#include "targets.h"
#include "wheel_controller.h"

#define FHW_VERSION 1u
#define FHW_ACTIVATION_STEP "activation: IAT slot 141DD1330 hook"
#define FHW_ADDRESS_DESCRIPTION_CAPACITY 160
#define FHW_MAX_HEADER_OFFSET 0x1000

typedef HMODULE(WINAPI *fhw_load_library_ex_w)(LPCWSTR, HANDLE, DWORD);

static const unsigned char fhw_expected_thunk[] = FHW_ACTIVATION_THUNK_BYTES;
static void *volatile fhw_next_load_library_ex_w = NULL;
static void *volatile fhw_next_activation = NULL;
static LONG fhw_trigger_fired = 0;
static HINSTANCE fhw_module = NULL;
static fhw_config fhw_settings;

static int fhw_path_has_suffix(const wchar_t *path, const wchar_t *suffix)
{
    int path_length;
    int suffix_length;

    if (path == NULL)
        return 0;
    path_length = lstrlenW(path);
    suffix_length = lstrlenW(suffix);
    if (suffix_length > path_length)
        return 0;
    return CompareStringOrdinal(path + path_length - suffix_length, suffix_length, suffix, suffix_length, TRUE) ==
           CSTR_EQUAL;
}

static int fhw_game_matches(void)
{
    const unsigned char *base = (const unsigned char *)(UINT_PTR)FHW_GAME_PREFERRED_BASE;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS64 *nt;

    if (!fhw_range_is_readable(base, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    if (dos->e_lfanew <= 0 || dos->e_lfanew > FHW_MAX_HEADER_OFFSET)
        return 0;
    nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (!fhw_range_is_readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    if (nt->FileHeader.TimeDateStamp != FHW_GAME_TIMESTAMP ||
        nt->OptionalHeader.SizeOfImage != FHW_GAME_SIZE_OF_IMAGE) {
        fhw_log_format("game -- SKIPPED -- unexpected fingerprint (timestamp %08lX, SizeOfImage %08lX)",
                       (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)nt->OptionalHeader.SizeOfImage);
        return 0;
    }
    fhw_log_format("game -- OK (timestamp %08X, SizeOfImage %08X)", FHW_GAME_TIMESTAMP, FHW_GAME_SIZE_OF_IMAGE);
    return 1;
}

static long fhw_hooked_activation(const void *class_name, const void *interface_id, void **factory)
{
    fhw_activation_function next = (fhw_activation_function)fhw_next_activation;
    long result = next(class_name, interface_id, factory);

    if (result >= 0)
        fhw_statics_wrap(class_name, interface_id, factory);
    return result;
}

static long fhw_activation_passthrough(const void *class_name, const void *interface_id, void **factory)
{
    fhw_activation_function next = (fhw_activation_function)fhw_next_activation;

    if (next == NULL)
        return E_FAIL;
    return next(class_name, interface_id, factory);
}

static void **fhw_activation_slot(void)
{
    return (void **)(UINT_PTR)FHW_ACTIVATION_FACTORY_IAT_SLOT;
}

static int fhw_slot_is_resolved(void)
{
    return fhw_region_is_executable(*(void *volatile *)fhw_activation_slot());
}

static void fhw_chain_activation(DWORD waited_ms)
{
    char owner[FHW_ADDRESS_DESCRIPTION_CAPACITY];

    if (!fhw_chain_iat_slot(fhw_activation_slot(), (void *)fhw_hooked_activation, &fhw_next_activation)) {
        fhw_log_step_skipped(FHW_ACTIVATION_STEP, "IAT slot not writable");
        return;
    }
    fhw_describe_address(fhw_next_activation, owner, sizeof(owner));
    fhw_log_format("%s -- OK (chained to %016llX, %s, resolved after %lu ms)", FHW_ACTIVATION_STEP,
                   (unsigned long long)(UINT_PTR)fhw_next_activation, owner, (unsigned long)waited_ms);
}

static DWORD WINAPI fhw_wait_for_slot(LPVOID parameter)
{
    DWORD started = GetTickCount();
    DWORD waited = 0;

    (void)parameter;
    while (waited <= FHW_SLOT_WAIT_LIMIT_MS) {
        if (fhw_slot_is_resolved()) {
            fhw_chain_activation(waited);
            return 0;
        }
        Sleep(FHW_SLOT_POLL_INTERVAL_MS);
        waited = GetTickCount() - started;
    }
    fhw_log_format("%s -- SKIPPED -- slot still unresolved after %lu ms", FHW_ACTIVATION_STEP,
                   (unsigned long)FHW_SLOT_WAIT_LIMIT_MS);
    return 0;
}

static void fhw_install_activation_hook(void)
{
    const unsigned char *thunk = (const unsigned char *)(UINT_PTR)FHW_ACTIVATION_THUNK;
    HANDLE watcher;

    if (!fhw_range_is_readable(thunk, sizeof(fhw_expected_thunk)) ||
        memcmp(thunk, fhw_expected_thunk, sizeof(fhw_expected_thunk)) != 0) {
        fhw_log_step_skipped(FHW_ACTIVATION_STEP, "unexpected bytes at thunk 140EDA1FA");
        return;
    }
    if (!fhw_range_is_readable(fhw_activation_slot(), sizeof(void *))) {
        fhw_log_step_skipped(FHW_ACTIVATION_STEP, "IAT slot unreadable");
        return;
    }
    if (fhw_slot_is_resolved()) {
        fhw_chain_activation(0);
        return;
    }
    watcher = CreateThread(NULL, 0, fhw_wait_for_slot, NULL, 0, NULL);
    if (watcher == NULL) {
        fhw_log_step_skipped(FHW_ACTIVATION_STEP, "cannot create the wait thread");
        return;
    }
    CloseHandle(watcher);
}

static void fhw_fire_trigger(const wchar_t *module_path)
{
    if (InterlockedCompareExchange(&fhw_trigger_fired, 1, 0) != 0)
        return;
    fhw_log_format("trigger -- OK (%ls)", module_path);
    fhw_config_load(&fhw_settings);
    if (!fhw_settings.enabled) {
        fhw_log_step_skipped("wheel", "disabled in the configuration");
        return;
    }
    if (!fhw_game_matches()) {
        fhw_log_step_skipped("wheel", "game not recognised: no hook installed");
        return;
    }
    fhw_force_start(&fhw_settings, fhw_module);
    fhw_statics_init(fhw_activation_passthrough);
    fhw_wheel_init(&fhw_settings, fhw_statics_user_provider());
    fhw_install_activation_hook();
}

static HMODULE WINAPI fhw_hooked_load_library_ex_w(LPCWSTR name, HANDLE file, DWORD flags)
{
    fhw_load_library_ex_w next = (fhw_load_library_ex_w)fhw_next_load_library_ex_w;
    HMODULE loaded = next(name, file, flags);

    if (loaded != NULL && fhw_path_has_suffix(name, FHW_TLS_MODULE_SUFFIX_W))
        fhw_fire_trigger(name);
    return loaded;
}

static void fhw_install_trigger(void)
{
    void **slot = fhw_find_iat_slot(GetModuleHandleW(NULL), FHW_LOADER_DLL, FHW_LOADER_FUNCTION);

    if (slot == NULL) {
        fhw_log_step_skipped("trigger: hook of " FHW_LOADER_FUNCTION, "IAT slot not found in the host executable");
        return;
    }
    if (!fhw_chain_iat_slot(slot, (void *)fhw_hooked_load_library_ex_w, &fhw_next_load_library_ex_w)) {
        fhw_log_step_skipped("trigger: hook of " FHW_LOADER_FUNCTION, "IAT slot not writable");
        return;
    }
    fhw_log_format("trigger: hook of %s -- OK (chained to %016llX, waiting for %ls)", FHW_LOADER_FUNCTION,
                   (unsigned long long)(UINT_PTR)fhw_next_load_library_ex_w, FHW_TLS_MODULE_SUFFIX_W);
}

__declspec(dllexport) unsigned int fh2wheel_version(void)
{
    return FHW_VERSION;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        fhw_module = instance;
        if (!fhw_log_open())
            return TRUE;
        fhw_log_format("fh2wheel.dll v%u loaded", FHW_VERSION);
        fhw_install_trigger();
    } else if (reason == DLL_PROCESS_DETACH) {
        fhw_log_close();
    }
    return TRUE;
}
