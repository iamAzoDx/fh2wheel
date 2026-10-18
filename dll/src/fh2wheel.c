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

#define FHW_VERSION_MAJOR 1u
#define FHW_VERSION_MINOR 4u
#define FHW_VERSION_SHIFT 16
#define FHW_ACTIVATION_STEP "activation: IAT slot 141DD1330 hook"
#define FHW_ADDRESS_DESCRIPTION_CAPACITY 160
#define FHW_MAX_HEADER_OFFSET 0x1000

typedef enum {
    FHW_GAME_ABSENT,
    FHW_GAME_FOREIGN,
    FHW_GAME_INCOMPLETE,
    FHW_GAME_LOADING,
    FHW_GAME_READY
} fhw_game_state;

static const unsigned char fhw_expected_thunk[] = FHW_ACTIVATION_THUNK_BYTES;
static void *volatile fhw_next_activation = NULL;
static HINSTANCE fhw_module = NULL;
static fhw_config fhw_settings;

static void **fhw_activation_slot(void)
{
    return (void **)(UINT_PTR)FHW_ACTIVATION_FACTORY_IAT_SLOT;
}

static const IMAGE_NT_HEADERS64 *fhw_game_headers(void)
{
    const unsigned char *base = (const unsigned char *)(UINT_PTR)FHW_GAME_PREFERRED_BASE;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS64 *nt;

    if (!fhw_range_is_readable(base, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;
    if (dos->e_lfanew <= 0 || dos->e_lfanew > FHW_MAX_HEADER_OFFSET)
        return NULL;
    nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (!fhw_range_is_readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;
    return nt;
}

static int fhw_thunk_matches(void)
{
    const unsigned char *thunk = (const unsigned char *)(UINT_PTR)FHW_ACTIVATION_THUNK;

    return fhw_range_is_readable(thunk, sizeof(fhw_expected_thunk)) &&
           memcmp(thunk, fhw_expected_thunk, sizeof(fhw_expected_thunk)) == 0;
}

static fhw_game_state fhw_game_state_now(void)
{
    const IMAGE_NT_HEADERS64 *nt = fhw_game_headers();

    if (nt == NULL)
        return FHW_GAME_ABSENT;
    if (nt->FileHeader.TimeDateStamp != FHW_GAME_TIMESTAMP ||
        nt->OptionalHeader.SizeOfImage != FHW_GAME_SIZE_OF_IMAGE)
        return FHW_GAME_FOREIGN;
    if (!fhw_thunk_matches() || !fhw_range_is_readable(fhw_activation_slot(), sizeof(void *)))
        return FHW_GAME_INCOMPLETE;
    if (GetModuleHandleW(FHW_TLS_MODULE_W) == NULL)
        return FHW_GAME_LOADING;
    return FHW_GAME_READY;
}

static void fhw_log_game_not_ready(fhw_game_state state)
{
    const IMAGE_NT_HEADERS64 *nt = fhw_game_headers();

    if (state == FHW_GAME_FOREIGN && nt != NULL)
        fhw_log_format("game -- SKIPPED -- unexpected fingerprint (timestamp %08lX, SizeOfImage %08lX)",
                       (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)nt->OptionalHeader.SizeOfImage);
    else if (state == FHW_GAME_INCOMPLETE)
        fhw_log_step_skipped(FHW_ACTIVATION_STEP, "unexpected bytes at thunk 140EDA1FA or IAT slot unreadable");
    else if (state == FHW_GAME_LOADING)
        fhw_log_format("game -- SKIPPED -- %ls never loaded after %lu ms", FHW_TLS_MODULE_W,
                       (unsigned long)FHW_GAME_WAIT_LIMIT_MS);
    else
        fhw_log_format("game -- SKIPPED -- not mapped at %016llX after %lu ms",
                       (unsigned long long)FHW_GAME_PREFERRED_BASE, (unsigned long)FHW_GAME_WAIT_LIMIT_MS);
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

static int fhw_wait_for_game(DWORD *waited_ms)
{
    DWORD started = GetTickCount();
    fhw_game_state state = fhw_game_state_now();

    *waited_ms = 0;
    while (state != FHW_GAME_READY && *waited_ms <= FHW_GAME_WAIT_LIMIT_MS) {
        Sleep(FHW_GAME_POLL_INTERVAL_MS);
        *waited_ms = GetTickCount() - started;
        state = fhw_game_state_now();
    }
    if (state != FHW_GAME_READY) {
        fhw_log_game_not_ready(state);
        return 0;
    }
    fhw_log_format("game -- OK (timestamp %08X, SizeOfImage %08X, mapped after %lu ms)", FHW_GAME_TIMESTAMP,
                   FHW_GAME_SIZE_OF_IMAGE, (unsigned long)*waited_ms);
    return 1;
}

static void fhw_wait_for_slot(void)
{
    DWORD started = GetTickCount();
    DWORD waited = 0;

    while (waited <= FHW_SLOT_WAIT_LIMIT_MS) {
        if (fhw_slot_is_resolved()) {
            fhw_chain_activation(waited);
            return;
        }
        Sleep(FHW_SLOT_POLL_INTERVAL_MS);
        waited = GetTickCount() - started;
    }
    fhw_log_format("%s -- SKIPPED -- slot still unresolved after %lu ms", FHW_ACTIVATION_STEP,
                   (unsigned long)FHW_SLOT_WAIT_LIMIT_MS);
}

static DWORD WINAPI fhw_watch_game(LPVOID parameter)
{
    DWORD waited;

    (void)parameter;
    if (!fhw_wait_for_game(&waited)) {
        fhw_log_step_skipped("wheel", "game not recognised: no hook installed");
        return 0;
    }
    fhw_config_load(&fhw_settings);
    if (!fhw_settings.enabled) {
        fhw_log_step_skipped("wheel", "disabled in the configuration");
        return 0;
    }
    fhw_force_start(&fhw_settings, fhw_module);
    fhw_statics_init(fhw_activation_passthrough);
    fhw_wheel_init(&fhw_settings, fhw_statics_user_provider());
    fhw_wait_for_slot();
    return 0;
}

static void fhw_start_game_watch(void)
{
    HANDLE watcher = CreateThread(NULL, 0, fhw_watch_game, NULL, 0, NULL);

    if (watcher == NULL) {
        fhw_log_step_skipped("game watch", "cannot create the wait thread");
        return;
    }
    CloseHandle(watcher);
    fhw_log_format("game watch -- OK (waiting for FH2 at %016llX and %ls, no host import touched)",
                   (unsigned long long)FHW_GAME_PREFERRED_BASE, FHW_TLS_MODULE_W);
}

__declspec(dllexport) unsigned int fh2wheel_version(void)
{
    return (FHW_VERSION_MAJOR << FHW_VERSION_SHIFT) | FHW_VERSION_MINOR;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        fhw_module = instance;
        if (!fhw_log_open())
            return TRUE;
        fhw_log_format("fh2wheel.dll v%u.%u loaded", FHW_VERSION_MAJOR, FHW_VERSION_MINOR);
        fhw_start_game_watch();
    } else if (reason == DLL_PROCESS_DETACH) {
        fhw_log_close();
    }
    return TRUE;
}
