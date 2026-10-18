#include <windows.h>
#include <mmsystem.h>

#include "force_loop.h"
#include "log.h"

#define FHW_DEVICE_WAIT_MS 5000u
#define FHW_TIMER_RESOLUTION_MS 1u
#define FHW_TRACE_INTERVAL_MS 2000.0
#define FHW_PERCENT 100.0f
#define FHW_MILLISECONDS_PER_SECOND 1000.0
#define FHW_HEARTBEAT_REGISTER 0

static CRITICAL_SECTION fhw_force_section;
static fhw_vm fhw_vm_instance;
static fhw_wheel_input fhw_latest_input;
static unsigned long long fhw_latest_timestamp = 0;
static fhw_config fhw_force_config;
static void *fhw_force_module = NULL;
static HANDLE fhw_device_ready = NULL;
static volatile LONG fhw_wheel_present = 0;
static LARGE_INTEGER fhw_clock_origin;
static LARGE_INTEGER fhw_clock_frequency;

double fhw_force_now_ms(void)
{
    LARGE_INTEGER now;

    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - fhw_clock_origin.QuadPart) * FHW_MILLISECONDS_PER_SECOND /
           (double)fhw_clock_frequency.QuadPart;
}

void fhw_force_lock(void)
{
    EnterCriticalSection(&fhw_force_section);
}

void fhw_force_unlock(void)
{
    LeaveCriticalSection(&fhw_force_section);
}

fhw_vm *fhw_force_vm(void)
{
    return &fhw_vm_instance;
}

int fhw_force_wheel_present(void)
{
    if (fhw_device_ready != NULL)
        WaitForSingleObject(fhw_device_ready, FHW_DEVICE_WAIT_MS);
    return fhw_wheel_present != 0;
}

void fhw_force_snapshot(fhw_wheel_input *input, unsigned long long *timestamp)
{
    fhw_force_lock();
    *input = fhw_latest_input;
    *timestamp = fhw_latest_timestamp;
    fhw_force_unlock();
}

static void fhw_pump_messages(void)
{
    MSG message;

    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static void fhw_wait_until(double deadline_ms)
{
    double remaining;

    while ((remaining = deadline_ms - fhw_force_now_ms()) > 0.0)
        Sleep(remaining >= 2.0 ? 1 : 0);
}

static DWORD WINAPI fhw_force_thread(LPVOID parameter)
{
    const double period_ms = FHW_MILLISECONDS_PER_SECOND / (double)fhw_force_config.force_frequency_hz;
    const float gain = (float)fhw_force_config.force_gain_percent / FHW_PERCENT *
                       (fhw_force_config.force_inverted ? -1.0f : 1.0f);
    double next_tick;
    double next_trace;
    float peak = 0.0f;
    unsigned long ticks = 0;

    (void)parameter;
    if (!fhw_device_open(&fhw_force_config, fhw_force_module)) {
        SetEvent(fhw_device_ready);
        fhw_device_close();
        return 0;
    }
    InterlockedExchange(&fhw_wheel_present, 1);
    SetEvent(fhw_device_ready);
    timeBeginPeriod(FHW_TIMER_RESOLUTION_MS);

    next_tick = fhw_force_now_ms();
    next_trace = next_tick + FHW_TRACE_INTERVAL_MS;
    for (;;) {
        fhw_wheel_input input;
        double now;
        float force;

        fhw_pump_messages();
        if (!fhw_device_poll(&input)) {
            fhw_wait_until(next_tick += period_ms);
            continue;
        }
        now = fhw_force_now_ms();
        fhw_force_lock();
        fhw_latest_input = input;
        fhw_latest_timestamp++;
        force = fhw_vm_step(&fhw_vm_instance, now, input.steering);
        fhw_force_unlock();

        force *= gain;
        if (!fhw_force_config.force_enabled)
            force = 0.0f;
        fhw_device_set_force(force);

        ticks++;
        if (force > peak)
            peak = force;
        if (-force > peak)
            peak = -force;
        if (fhw_force_config.trace && now >= next_trace) {
            float torque = 0.0f;
            float menu_spring = 0.0f;
            float deadline;

            fhw_force_lock();
            fhw_vm_get(&fhw_vm_instance, "Torque", &torque);
            fhw_vm_get(&fhw_vm_instance, "InMenuSpringScale", &menu_spring);
            deadline = fhw_vm_instance.shared[FHW_HEARTBEAT_REGISTER];
            fhw_force_unlock();
            fhw_log_format("loop: %lu cycles, steering %.3f, throttle %.2f, brake %.2f, clutch %.2f, "
                           "buttons %03X, gear %u, peak force %.3f (effect %d), Torque %.3f, menu spring %.3f, "
                           "heartbeat %+.0f ms",
                           ticks, input.steering, input.pedals[FHW_PEDAL_THROTTLE], input.pedals[FHW_PEDAL_BRAKE],
                           input.pedals[FHW_PEDAL_CLUTCH], input.buttons, (unsigned int)input.gear, peak,
                           fhw_device_has_force(), torque, menu_spring, deadline - now);
            peak = 0.0f;
            next_trace = now + FHW_TRACE_INTERVAL_MS;
        }
        next_tick += period_ms;
        if (next_tick < now)
            next_tick = now;
        fhw_wait_until(next_tick);
    }
    return 0;
}

int fhw_force_start(const fhw_config *config, void *module)
{
    HANDLE thread;

    InitializeCriticalSection(&fhw_force_section);
    QueryPerformanceFrequency(&fhw_clock_frequency);
    QueryPerformanceCounter(&fhw_clock_origin);
    fhw_vm_init(&fhw_vm_instance);
    ZeroMemory(&fhw_latest_input, sizeof(fhw_latest_input));
    fhw_force_config = *config;
    fhw_force_module = module;
    fhw_device_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, fhw_force_thread, NULL, 0, NULL);
    if (thread == NULL) {
        fhw_log_step_skipped("force feedback loop", "cannot create thread");
        SetEvent(fhw_device_ready);
        return 0;
    }
    SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
    CloseHandle(thread);
    fhw_log_format("force feedback loop -- started (%u Hz)", config->force_frequency_hz);
    return 1;
}
