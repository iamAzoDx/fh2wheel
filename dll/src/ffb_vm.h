#ifndef FHW_FFB_VM_H
#define FHW_FFB_VM_H

#define FHW_VM_PROGRAM_SLOTS 16
#define FHW_VM_INSTRUCTION_CAPACITY 96
#define FHW_VM_VARIABLE_CAPACITY 32
#define FHW_VM_VARIABLE_NAME_CAPACITY 40
#define FHW_VM_PROGRAM_REGISTERS 4
#define FHW_VM_SHARED_REGISTERS 4
#define FHW_VM_OPERAND_CAPACITY 3

typedef enum {
    FHW_OPERAND_LITERAL,
    FHW_OPERAND_PROGRAM_REGISTER,
    FHW_OPERAND_SHARED_REGISTER,
    FHW_OPERAND_VARIABLE,
    FHW_OPERAND_LOCAL_TIME,
    FHW_OPERAND_EFFECT_TIME,
    FHW_OPERAND_AXIS_POSITION,
    FHW_OPERAND_AXIS_FORCE,
    FHW_OPERAND_AXIS_FORCE_ADD
} fhw_operand_kind;

typedef enum {
    FHW_OP_TOFL,
    FHW_OP_MOV,
    FHW_OP_ADD,
    FHW_OP_SUB,
    FHW_OP_MUL,
    FHW_OP_DIV,
    FHW_OP_MOD,
    FHW_OP_GT,
    FHW_OP_GTE,
    FHW_OP_LT,
    FHW_OP_LTE,
    FHW_OP_EQ,
    FHW_OP_NEQ,
    FHW_OP_END,
    FHW_OP_ENDZ,
    FHW_OP_ENDN,
    FHW_OP_SKPZ,
    FHW_OP_SKPN
} fhw_opcode;

typedef struct {
    fhw_operand_kind kind;
    int index;
    float value;
} fhw_operand;

typedef struct {
    fhw_opcode opcode;
    fhw_operand operands[FHW_VM_OPERAND_CAPACITY];
} fhw_instruction;

typedef struct {
    int loaded;
    int enabled;
    int count;
    double enabled_at_ms;
    float registers[FHW_VM_PROGRAM_REGISTERS];
    fhw_instruction instructions[FHW_VM_INSTRUCTION_CAPACITY];
} fhw_program;

typedef struct {
    fhw_program programs[FHW_VM_PROGRAM_SLOTS];
    float shared[FHW_VM_SHARED_REGISTERS];
    int variable_count;
    char variable_names[FHW_VM_VARIABLE_CAPACITY][FHW_VM_VARIABLE_NAME_CAPACITY];
    float variable_values[FHW_VM_VARIABLE_CAPACITY];
    char last_error[96];
} fhw_vm;

void fhw_vm_init(fhw_vm *vm);
void fhw_vm_reset(fhw_vm *vm);
int fhw_vm_declare(fhw_vm *vm, const char *name);
int fhw_vm_set(fhw_vm *vm, const char *name, float value);
int fhw_vm_get(const fhw_vm *vm, const char *name, float *value);
int fhw_vm_load(fhw_vm *vm, unsigned int slot, const char *text);
int fhw_vm_enable(fhw_vm *vm, unsigned int slot, double now_ms);
float fhw_vm_step(fhw_vm *vm, double now_ms, float axis_position);

#endif
