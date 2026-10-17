#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ffb_vm.h"

#define HEARTBEAT_PROGRAM "ENDZ {PauseHeartbeat} TOFL Lt E0 ADD E0 2500f L0 MOV 0f {PauseHeartbeat} "
#define TORQUE_PROGRAM "TOFL Lt E0 GT E0 L0 E0 ENDN E0 MOV {Torque} Af+ "
#define SPRING_PROGRAM                                                                                  \
    "TOFL Lt E0 GT E0 L0 E0 ENDN E0 SUB 0 {InRaceSpringScale} E0 MUL E0 Ad E0 SUB 0 "                   \
    "{InRaceSpringMaxForce} E1 LT E0 E1 E2 SKPZ E2 1 MOV E1 E0 GT E0 {InRaceSpringMaxForce} E2 SKPZ E2 " \
    "1 MOV {InRaceSpringMaxForce} E0 MOV E0 Af+ "
#define VELOCITY_PROGRAM "SUB Ad E0 E2 SUB E0 E1 E3 ADD E2 E3 E2 MUL E2 0.5f L1 MUL L1 100.0f L1 MOV E0 E1 MOV Ad E0 "
#define DAMPING_PROGRAM                                                                                     \
    "TOFL Lt E0 GT E0 L0 E0 ENDN E0 MUL {InRaceDampingScale} L1 E0 SUB 0.0f E0 E0 SUB 0.0f "                \
    "{InRaceDampingMaxForce} E1 LT E0 E1 E2 SKPZ E2 1 MOV E1 E0 GT E0 {InRaceDampingMaxForce} E2 SKPZ E2 1 " \
    "MOV {InRaceDampingMaxForce} E0 MOV E0 Af+ "
#define MENU_SPRING_PROGRAM "TOFL Lt E0 LTE E0 L0 E0 ENDN E0 SUB 0 {InMenuSpringScale} E0 MUL E0 Ad Af+ "
#define LEFT_RUMBLE_PROGRAM                                                                                   \
    "TOFL Lt E0 GT E0 L0 E0 ENDN E0 DIV {LeftWavelength} 2f E0 TOFL Et E1 MOD E1 {LeftWavelength} E1 LT E1 E0 " \
    "E1 SKPZ E1 2 SUB 0f {LeftAmplitude} Af+ END MOV {LeftAmplitude} Af+ "
#define CLAMP_PROGRAM "SUB 0 0.5f E0 LT Af E0 E1 SKPZ E1 1 MOV E0 Af GT Af 0.5f E1 SKPZ E1 1 MOV 0.5f Af "

static int failures = 0;

static void expect_near(const char *what, float actual, float expected)
{
    if (fabsf(actual - expected) > 1e-4f) {
        printf("FAIL %s: got %f, expected %f\n", what, actual, expected);
        failures++;
    }
}

static void expect_true(const char *what, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static void load_or_fail(fhw_vm *vm, unsigned int slot, const char *text)
{
    if (!fhw_vm_load(vm, slot, text) || !fhw_vm_enable(vm, slot, 0.0)) {
        printf("FAIL loading program %u: %s\n", slot, vm->last_error);
        failures++;
    }
}

static void test_heartbeat_extends_deadline(void)
{
    fhw_vm vm;
    float value = -1.0f;

    fhw_vm_init(&vm);
    load_or_fail(&vm, 0, HEARTBEAT_PROGRAM);
    fhw_vm_set(&vm, "PauseHeartbeat", 1.0f);
    fhw_vm_step(&vm, 1000.0, 0.0f);
    expect_near("heartbeat: deadline", vm.shared[0], 3500.0f);
    expect_true("heartbeat: flag cleared", fhw_vm_get(&vm, "PauseHeartbeat", &value));
    expect_near("heartbeat: flag", value, 0.0f);
    fhw_vm_step(&vm, 2000.0, 0.0f);
    expect_near("heartbeat: deadline unchanged without flag", vm.shared[0], 3500.0f);
}

static void test_torque_stops_after_deadline(void)
{
    fhw_vm vm;

    fhw_vm_init(&vm);
    load_or_fail(&vm, 0, HEARTBEAT_PROGRAM);
    load_or_fail(&vm, 1, TORQUE_PROGRAM);
    fhw_vm_set(&vm, "PauseHeartbeat", 1.0f);
    fhw_vm_set(&vm, "Torque", 0.3f);
    expect_near("torque active", fhw_vm_step(&vm, 1000.0, 0.0f), 0.3f);
    expect_near("torque cut after the deadline", fhw_vm_step(&vm, 4000.0, 0.0f), 0.0f);
}

static void test_spring_is_clamped(void)
{
    fhw_vm vm;

    fhw_vm_init(&vm);
    load_or_fail(&vm, 0, HEARTBEAT_PROGRAM);
    load_or_fail(&vm, 2, SPRING_PROGRAM);
    fhw_vm_set(&vm, "PauseHeartbeat", 1.0f);
    fhw_vm_set(&vm, "InRaceSpringScale", 2.0f);
    fhw_vm_set(&vm, "InRaceSpringMaxForce", 0.4f);
    expect_near("linear spring", fhw_vm_step(&vm, 10.0, 0.1f), -0.2f);
    expect_near("spring capped on the right", fhw_vm_step(&vm, 20.0, 0.5f), -0.4f);
    expect_near("spring capped on the left", fhw_vm_step(&vm, 30.0, -0.5f), 0.4f);
}

static void test_damping_uses_velocity(void)
{
    fhw_vm vm;

    fhw_vm_init(&vm);
    load_or_fail(&vm, 0, HEARTBEAT_PROGRAM);
    load_or_fail(&vm, 3, VELOCITY_PROGRAM);
    load_or_fail(&vm, 4, DAMPING_PROGRAM);
    fhw_vm_set(&vm, "PauseHeartbeat", 1.0f);
    fhw_vm_set(&vm, "InRaceDampingScale", 1.0f);
    fhw_vm_set(&vm, "InRaceDampingMaxForce", 10.0f);
    fhw_vm_step(&vm, 10.0, 0.0f);
    fhw_vm_step(&vm, 20.0, 0.0f);
    expect_near("damping at rest", fhw_vm_step(&vm, 30.0, 0.0f), 0.0f);
    expect_near("damping while moving", fhw_vm_step(&vm, 40.0, 0.01f), -0.5f);
    expect_near("average velocity", vm.shared[1], 0.5f);
}

static void test_menu_spring_only_without_heartbeat(void)
{
    fhw_vm vm;

    fhw_vm_init(&vm);
    load_or_fail(&vm, 0, HEARTBEAT_PROGRAM);
    load_or_fail(&vm, 5, MENU_SPRING_PROGRAM);
    fhw_vm_set(&vm, "InMenuSpringScale", 1.0f);
    fhw_vm_set(&vm, "PauseHeartbeat", 1.0f);
    expect_near("menu spring off while racing", fhw_vm_step(&vm, 100.0, 0.5f), 0.0f);
    expect_near("menu spring on outside races", fhw_vm_step(&vm, 5000.0, 0.5f), -0.5f);
}

static void test_square_rumble(void)
{
    fhw_vm vm;

    fhw_vm_init(&vm);
    load_or_fail(&vm, 0, HEARTBEAT_PROGRAM);
    load_or_fail(&vm, 6, LEFT_RUMBLE_PROGRAM);
    fhw_vm_set(&vm, "PauseHeartbeat", 1.0f);
    fhw_vm_set(&vm, "LeftWavelength", 100.0f);
    fhw_vm_set(&vm, "LeftAmplitude", 0.2f);
    expect_near("rumble first half period", fhw_vm_step(&vm, 10.0, 0.0f), -0.2f);
    expect_near("rumble second half period", fhw_vm_step(&vm, 60.0, 0.0f), 0.2f);
}

static void test_clamp_program_runs_last(void)
{
    fhw_vm vm;

    fhw_vm_init(&vm);
    load_or_fail(&vm, 0, HEARTBEAT_PROGRAM);
    load_or_fail(&vm, 1, TORQUE_PROGRAM);
    load_or_fail(&vm, 8, CLAMP_PROGRAM);
    fhw_vm_set(&vm, "PauseHeartbeat", 1.0f);
    fhw_vm_set(&vm, "Torque", 0.9f);
    expect_near("positive cap", fhw_vm_step(&vm, 10.0, 0.0f), 0.5f);
    fhw_vm_set(&vm, "Torque", -0.9f);
    expect_near("negative cap", fhw_vm_step(&vm, 20.0, 0.0f), -0.5f);
}

static void test_rejects_unknown_instruction(void)
{
    fhw_vm vm;

    fhw_vm_init(&vm);
    expect_true("unknown instruction refused", !fhw_vm_load(&vm, 0, "FOO E0 E1"));
    expect_true("error message", strstr(vm.last_error, "FOO") != NULL);
    expect_true("literal destination refused", !fhw_vm_load(&vm, 0, "MOV E0 1f"));
    expect_true("missing operand refused", !fhw_vm_load(&vm, 0, "ADD E0 E1"));
    expect_true("out-of-range slot refused", !fhw_vm_load(&vm, FHW_VM_PROGRAM_SLOTS, "END"));
    expect_true("refused program not enabled", !fhw_vm_enable(&vm, 0, 0.0));
}

static void test_reset_clears_programs(void)
{
    fhw_vm vm;

    fhw_vm_init(&vm);
    load_or_fail(&vm, 1, TORQUE_PROGRAM);
    fhw_vm_set(&vm, "Torque", 0.3f);
    fhw_vm_reset(&vm);
    expect_near("no force after reset", fhw_vm_step(&vm, 0.0, 0.0f), 0.0f);
}

int main(void)
{
    test_heartbeat_extends_deadline();
    test_torque_stops_after_deadline();
    test_spring_is_clamped();
    test_damping_uses_velocity();
    test_menu_spring_only_without_heartbeat();
    test_square_rumble();
    test_clamp_program_runs_last();
    test_rejects_unknown_instruction();
    test_reset_clears_programs();
    if (failures != 0) {
        printf("test_ffb_vm: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ffb_vm: OK\n");
    return 0;
}
