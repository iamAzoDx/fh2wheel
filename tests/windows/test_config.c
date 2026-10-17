#include <windows.h>
#include <stdio.h>

#include "config.h"
#include "log.h"

#define SAMPLE_INI                                                                                            \
    "[General]\r\nEnabled=1\r\nRotationDegrees=540\r\nTrace=1\r\n"                                            \
    "[Devices]\r\nWheelVid=0x346E\r\nWheelPid=0x0002\r\nPedalsVid=0x1234\r\nPedalsPid=0x5678\r\n"             \
    "[Axes]\r\nSteering=X\r\nSteeringInverted=1\r\nThrottle=Z\r\nThrottleSource=Wheel\r\n"                    \
    "ThrottleInverted=0\r\nBrake=RZ\r\nBrakeSource=Wheel\r\nBrakeInverted=1\r\nClutch=Slider0\r\n"           \
    "ClutchSource=Pedals\r\nClutchInverted=0\r\nHandbrake=None\r\n"                                           \
    "[Buttons]\r\nA=1\r\nB=2\r\nX=3\r\nY=4\r\nView=24\r\nMenu=37\r\nLeftPaddle=14\r\nRightPaddle=13\r\n"      \
    "DpadFromHat=0\r\nDpadUp=HatUp\r\nDpadDown=5\r\nDpadLeft=8\r\nDpadRight=7\r\n"                            \
    "[ForceFeedback]\r\nEnabled=1\r\nGainPercent=90\r\nInverted=1\r\nRateHz=250\r\n"

static int failures = 0;

static void expect(const char *what, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static int write_sample(const wchar_t *path)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;

    if (file == INVALID_HANDLE_VALUE)
        return 0;
    WriteFile(file, SAMPLE_INI, sizeof(SAMPLE_INI) - 1, &written, NULL);
    CloseHandle(file);
    return written == sizeof(SAMPLE_INI) - 1;
}

static void check_values(const fhw_config *config, const char *stage)
{
    char what[96];

#define CHECK(condition, name)                                                                                \
    do {                                                                                                      \
        snprintf(what, sizeof(what), "%s: %s", stage, name);                                                  \
        expect(what, condition);                                                                              \
    } while (0)
    CHECK(config->rotation_degrees == 540, "rotation");
    CHECK(config->trace == 1, "trace");
    CHECK(config->pedals_vid == 0x1234 && config->pedals_pid == 0x5678, "pedals device");
    CHECK(config->steering_inverted == 1, "steering inverted");
    CHECK(config->pedals[FHW_PEDAL_BRAKE].axis == FHW_AXIS_RZ && config->pedals[FHW_PEDAL_BRAKE].inverted, "brake");
    CHECK(config->pedals[FHW_PEDAL_CLUTCH].axis == FHW_AXIS_SLIDER0, "clutch axis");
    CHECK(config->pedals[FHW_PEDAL_CLUTCH].source == FHW_SOURCE_PEDALS, "clutch source");
    CHECK(config->pedals[FHW_PEDAL_HANDBRAKE].axis == FHW_AXIS_NONE, "handbrake");
    CHECK(config->buttons[FHW_BUTTON_VIEW] == 24 && config->buttons[FHW_BUTTON_MENU] == 37, "view and menu");
    CHECK(config->buttons[FHW_BUTTON_UP] == FHW_BINDING_HAT_UP, "hat up");
    CHECK(config->dpad_from_hat == 0, "d-pad from hat");
    CHECK(config->force_gain_percent == 90 && config->force_inverted == 1, "gain and inversion");
    CHECK(config->force_frequency_hz == 250, "rate");
#undef CHECK
}

static void test_defaults_when_missing(const wchar_t *path)
{
    fhw_config config;

    DeleteFileW(path);
    fhw_config_load(&config);
    expect("missing file is created", GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
    expect("default rotation", config.rotation_degrees == 900);
    expect("default rate", config.force_frequency_hz == 500);
}

static void test_round_trip(const wchar_t *path)
{
    wchar_t value[32];
    fhw_config config;

    expect("sample written", write_sample(path));
    fhw_config_load(&config);
    check_values(&config, "first read");

    DeleteFileW(path);
    expect("save", fhw_config_save(&config));
    expect("hat written by name", GetPrivateProfileStringW(L"Buttons", L"DpadUp", L"", value, 32, path) > 0 &&
                                      lstrcmpW(value, L"HatUp") == 0);
    fhw_config_load(&config);
    check_values(&config, "read after save");
}

static void test_out_of_range_values(const wchar_t *path)
{
    fhw_config config;

    write_sample(path);
    WritePrivateProfileStringW(L"General", L"RotationDegrees", L"5000", path);
    WritePrivateProfileStringW(L"ForceFeedback", L"RateHz", L"5", path);
    WritePrivateProfileStringW(L"Axes", L"Steering", L"Banana", path);
    fhw_config_load(&config);
    expect("rotation clamped", config.rotation_degrees == 900);
    expect("rate clamped", config.force_frequency_hz == FHW_MIN_FREQUENCY_HZ);
    expect("unknown axis falls back", config.steering_axis == FHW_AXIS_X);
}

int main(void)
{
    wchar_t path[MAX_PATH];
    wchar_t directory[MAX_PATH];

    if (!fhw_host_path(L"data", directory, MAX_PATH) || !fhw_host_path(L"data\\fh2wheel.ini", path, MAX_PATH)) {
        printf("FAIL paths\n");
        return 1;
    }
    CreateDirectoryW(directory, NULL);
    test_defaults_when_missing(path);
    test_round_trip(path);
    test_out_of_range_values(path);
    DeleteFileW(path);
    if (failures != 0) {
        printf("test_config: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_config: OK\n");
    return 0;
}
