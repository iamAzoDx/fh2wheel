#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ffb_vm.h"

#define FHW_VM_TOKEN_CAPACITY 64
#define FHW_VM_STEP_BUDGET 4096

typedef struct {
    const char *name;
    fhw_opcode opcode;
    int operand_count;
} fhw_opcode_spec;

static const fhw_opcode_spec fhw_opcode_specs[] = {
    {"TOFL", FHW_OP_TOFL, 2}, {"MOV", FHW_OP_MOV, 2},   {"ADD", FHW_OP_ADD, 3},   {"SUB", FHW_OP_SUB, 3},
    {"MUL", FHW_OP_MUL, 3},   {"DIV", FHW_OP_DIV, 3},   {"MOD", FHW_OP_MOD, 3},   {"GT", FHW_OP_GT, 3},
    {"GTE", FHW_OP_GTE, 3},   {"LT", FHW_OP_LT, 3},     {"LTE", FHW_OP_LTE, 3},   {"EQ", FHW_OP_EQ, 3},
    {"NEQ", FHW_OP_NEQ, 3},   {"END", FHW_OP_END, 0},   {"ENDZ", FHW_OP_ENDZ, 1}, {"ENDN", FHW_OP_ENDN, 1},
    {"SKPZ", FHW_OP_SKPZ, 2}, {"SKPN", FHW_OP_SKPN, 2},
};

typedef struct {
    fhw_vm *vm;
    fhw_program *program;
    double now_ms;
    float axis_position;
    float axis_force;
} fhw_vm_context;

void fhw_vm_init(fhw_vm *vm)
{
    memset(vm, 0, sizeof(*vm));
}

void fhw_vm_reset(fhw_vm *vm)
{
    memset(vm->programs, 0, sizeof(vm->programs));
    memset(vm->shared, 0, sizeof(vm->shared));
}

static int fhw_vm_find(const fhw_vm *vm, const char *name)
{
    int index;

    for (index = 0; index < vm->variable_count; index++) {
        if (strcmp(vm->variable_names[index], name) == 0)
            return index;
    }
    return -1;
}

int fhw_vm_declare(fhw_vm *vm, const char *name)
{
    int index = fhw_vm_find(vm, name);

    if (index >= 0)
        return index;
    if (vm->variable_count >= FHW_VM_VARIABLE_CAPACITY)
        return -1;
    if (strlen(name) == 0 || strlen(name) >= FHW_VM_VARIABLE_NAME_CAPACITY)
        return -1;
    index = vm->variable_count++;
    strcpy(vm->variable_names[index], name);
    vm->variable_values[index] = 0.0f;
    return index;
}

int fhw_vm_set(fhw_vm *vm, const char *name, float value)
{
    int index = fhw_vm_declare(vm, name);

    if (index < 0)
        return 0;
    vm->variable_values[index] = value;
    return 1;
}

int fhw_vm_get(const fhw_vm *vm, const char *name, float *value)
{
    int index = fhw_vm_find(vm, name);

    if (index < 0)
        return 0;
    *value = vm->variable_values[index];
    return 1;
}

static void fhw_vm_fail(fhw_vm *vm, const char *reason, const char *token)
{
    snprintf(vm->last_error, sizeof(vm->last_error), "%s : '%s'", reason, token);
}

static const char *fhw_vm_next_token(const char *cursor, char *token)
{
    size_t length = 0;

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        cursor++;
    if (*cursor == '\0')
        return NULL;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n') {
        if (length + 1 < FHW_VM_TOKEN_CAPACITY)
            token[length++] = *cursor;
        cursor++;
    }
    token[length] = '\0';
    return cursor;
}

static const fhw_opcode_spec *fhw_vm_find_opcode(const char *token)
{
    size_t index;

    for (index = 0; index < sizeof(fhw_opcode_specs) / sizeof(fhw_opcode_specs[0]); index++) {
        if (strcmp(fhw_opcode_specs[index].name, token) == 0)
            return &fhw_opcode_specs[index];
    }
    return NULL;
}

static int fhw_vm_parse_register(const char *token, char prefix, int capacity, int *index)
{
    char *end;
    long value;

    if (token[0] != prefix || token[1] < '0' || token[1] > '9')
        return 0;
    value = strtol(token + 1, &end, 10);
    if (*end != '\0' || value < 0 || value >= capacity)
        return 0;
    *index = (int)value;
    return 1;
}

static int fhw_vm_parse_literal(const char *token, float *value)
{
    char *end;
    double parsed = strtod(token, &end);

    if (end == token)
        return 0;
    if (*end == 'f')
        end++;
    if (*end != '\0')
        return 0;
    *value = (float)parsed;
    return 1;
}

static int fhw_vm_parse_operand(fhw_vm *vm, const char *token, fhw_operand *operand)
{
    size_t length = strlen(token);

    memset(operand, 0, sizeof(*operand));
    if (strcmp(token, "Lt") == 0) {
        operand->kind = FHW_OPERAND_LOCAL_TIME;
        return 1;
    }
    if (strcmp(token, "Et") == 0) {
        operand->kind = FHW_OPERAND_EFFECT_TIME;
        return 1;
    }
    if (strcmp(token, "Ad") == 0) {
        operand->kind = FHW_OPERAND_AXIS_POSITION;
        return 1;
    }
    if (strcmp(token, "Af") == 0) {
        operand->kind = FHW_OPERAND_AXIS_FORCE;
        return 1;
    }
    if (strcmp(token, "Af+") == 0) {
        operand->kind = FHW_OPERAND_AXIS_FORCE_ADD;
        return 1;
    }
    if (fhw_vm_parse_register(token, 'E', FHW_VM_PROGRAM_REGISTERS, &operand->index)) {
        operand->kind = FHW_OPERAND_PROGRAM_REGISTER;
        return 1;
    }
    if (fhw_vm_parse_register(token, 'L', FHW_VM_SHARED_REGISTERS, &operand->index)) {
        operand->kind = FHW_OPERAND_SHARED_REGISTER;
        return 1;
    }
    if (length > 2 && token[0] == '{' && token[length - 1] == '}') {
        char name[FHW_VM_VARIABLE_NAME_CAPACITY];

        if (length - 2 >= sizeof(name))
            return 0;
        memcpy(name, token + 1, length - 2);
        name[length - 2] = '\0';
        operand->kind = FHW_OPERAND_VARIABLE;
        operand->index = fhw_vm_declare(vm, name);
        return operand->index >= 0;
    }
    if (fhw_vm_parse_literal(token, &operand->value)) {
        operand->kind = FHW_OPERAND_LITERAL;
        return 1;
    }
    return 0;
}

static int fhw_vm_is_writable(fhw_operand_kind kind)
{
    return kind == FHW_OPERAND_PROGRAM_REGISTER || kind == FHW_OPERAND_SHARED_REGISTER ||
           kind == FHW_OPERAND_VARIABLE || kind == FHW_OPERAND_AXIS_FORCE || kind == FHW_OPERAND_AXIS_FORCE_ADD;
}

static int fhw_vm_writes_last_operand(fhw_opcode opcode)
{
    return opcode != FHW_OP_END && opcode != FHW_OP_ENDZ && opcode != FHW_OP_ENDN && opcode != FHW_OP_SKPZ &&
           opcode != FHW_OP_SKPN;
}

int fhw_vm_load(fhw_vm *vm, unsigned int slot, const char *text)
{
    fhw_program parsed;
    char token[FHW_VM_TOKEN_CAPACITY];
    const char *cursor = text;

    if (slot >= FHW_VM_PROGRAM_SLOTS) {
        snprintf(vm->last_error, sizeof(vm->last_error), "slot %u out of range", slot);
        return 0;
    }
    memset(&parsed, 0, sizeof(parsed));
    while ((cursor = fhw_vm_next_token(cursor, token)) != NULL) {
        const fhw_opcode_spec *spec = fhw_vm_find_opcode(token);
        fhw_instruction *instruction;
        int operand;

        if (spec == NULL) {
            fhw_vm_fail(vm, "unknown instruction", token);
            return 0;
        }
        if (parsed.count >= FHW_VM_INSTRUCTION_CAPACITY) {
            fhw_vm_fail(vm, "program too long", token);
            return 0;
        }
        instruction = &parsed.instructions[parsed.count++];
        instruction->opcode = spec->opcode;
        for (operand = 0; operand < spec->operand_count; operand++) {
            cursor = fhw_vm_next_token(cursor, token);
            if (cursor == NULL) {
                fhw_vm_fail(vm, "missing operand", spec->name);
                return 0;
            }
            if (!fhw_vm_parse_operand(vm, token, &instruction->operands[operand])) {
                fhw_vm_fail(vm, "invalid operand", token);
                return 0;
            }
        }
        if (spec->operand_count > 0 && fhw_vm_writes_last_operand(spec->opcode) &&
            !fhw_vm_is_writable(instruction->operands[spec->operand_count - 1].kind)) {
            fhw_vm_fail(vm, "destination not writable", spec->name);
            return 0;
        }
    }
    parsed.loaded = 1;
    vm->programs[slot] = parsed;
    vm->last_error[0] = '\0';
    return 1;
}

int fhw_vm_enable(fhw_vm *vm, unsigned int slot, double now_ms)
{
    if (slot >= FHW_VM_PROGRAM_SLOTS || !vm->programs[slot].loaded)
        return 0;
    vm->programs[slot].enabled = 1;
    vm->programs[slot].enabled_at_ms = now_ms;
    return 1;
}

static float fhw_vm_read(const fhw_vm_context *context, const fhw_operand *operand)
{
    switch (operand->kind) {
    case FHW_OPERAND_LITERAL:
        return operand->value;
    case FHW_OPERAND_PROGRAM_REGISTER:
        return context->program->registers[operand->index];
    case FHW_OPERAND_SHARED_REGISTER:
        return context->vm->shared[operand->index];
    case FHW_OPERAND_VARIABLE:
        return context->vm->variable_values[operand->index];
    case FHW_OPERAND_LOCAL_TIME:
        return (float)context->now_ms;
    case FHW_OPERAND_EFFECT_TIME:
        return (float)(context->now_ms - context->program->enabled_at_ms);
    case FHW_OPERAND_AXIS_POSITION:
        return context->axis_position;
    case FHW_OPERAND_AXIS_FORCE:
    case FHW_OPERAND_AXIS_FORCE_ADD:
        return context->axis_force;
    }
    return 0.0f;
}

static void fhw_vm_write(fhw_vm_context *context, const fhw_operand *operand, float value)
{
    switch (operand->kind) {
    case FHW_OPERAND_PROGRAM_REGISTER:
        context->program->registers[operand->index] = value;
        break;
    case FHW_OPERAND_SHARED_REGISTER:
        context->vm->shared[operand->index] = value;
        break;
    case FHW_OPERAND_VARIABLE:
        context->vm->variable_values[operand->index] = value;
        break;
    case FHW_OPERAND_AXIS_FORCE:
        context->axis_force = value;
        break;
    case FHW_OPERAND_AXIS_FORCE_ADD:
        context->axis_force += value;
        break;
    default:
        break;
    }
}

static float fhw_vm_binary(fhw_opcode opcode, float left, float right)
{
    switch (opcode) {
    case FHW_OP_ADD:
        return left + right;
    case FHW_OP_SUB:
        return left - right;
    case FHW_OP_MUL:
        return left * right;
    case FHW_OP_DIV:
        return right != 0.0f ? left / right : 0.0f;
    case FHW_OP_MOD:
        return right != 0.0f ? fmodf(left, right) : 0.0f;
    case FHW_OP_GT:
        return left > right ? 1.0f : 0.0f;
    case FHW_OP_GTE:
        return left >= right ? 1.0f : 0.0f;
    case FHW_OP_LT:
        return left < right ? 1.0f : 0.0f;
    case FHW_OP_LTE:
        return left <= right ? 1.0f : 0.0f;
    case FHW_OP_EQ:
        return left == right ? 1.0f : 0.0f;
    case FHW_OP_NEQ:
        return left != right ? 1.0f : 0.0f;
    default:
        return 0.0f;
    }
}

static void fhw_vm_run_program(fhw_vm_context *context)
{
    const fhw_program *program = context->program;
    int pc = 0;
    int budget = FHW_VM_STEP_BUDGET;

    while (pc >= 0 && pc < program->count && budget-- > 0) {
        const fhw_instruction *instruction = &program->instructions[pc];
        const fhw_operand *operands = instruction->operands;

        pc++;
        switch (instruction->opcode) {
        case FHW_OP_TOFL:
        case FHW_OP_MOV:
            fhw_vm_write(context, &operands[1], fhw_vm_read(context, &operands[0]));
            break;
        case FHW_OP_END:
            return;
        case FHW_OP_ENDZ:
            if (fhw_vm_read(context, &operands[0]) == 0.0f)
                return;
            break;
        case FHW_OP_ENDN:
            if (fhw_vm_read(context, &operands[0]) != 0.0f)
                return;
            break;
        case FHW_OP_SKPZ:
            if (fhw_vm_read(context, &operands[0]) == 0.0f)
                pc += (int)fhw_vm_read(context, &operands[1]);
            break;
        case FHW_OP_SKPN:
            if (fhw_vm_read(context, &operands[0]) != 0.0f)
                pc += (int)fhw_vm_read(context, &operands[1]);
            break;
        default:
            fhw_vm_write(context, &operands[2],
                         fhw_vm_binary(instruction->opcode, fhw_vm_read(context, &operands[0]),
                                       fhw_vm_read(context, &operands[1])));
            break;
        }
    }
}

float fhw_vm_step(fhw_vm *vm, double now_ms, float axis_position)
{
    fhw_vm_context context;
    unsigned int slot;

    context.vm = vm;
    context.now_ms = now_ms;
    context.axis_position = axis_position;
    context.axis_force = 0.0f;
    for (slot = 0; slot < FHW_VM_PROGRAM_SLOTS; slot++) {
        if (!vm->programs[slot].loaded || !vm->programs[slot].enabled)
            continue;
        context.program = &vm->programs[slot];
        fhw_vm_run_program(&context);
    }
    return context.axis_force;
}
