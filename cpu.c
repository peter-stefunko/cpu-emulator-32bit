#include "cpu.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 4096
#define CELL_SIZE (int32_t) sizeof(int32_t)

struct cpu
{
    int32_t registers[4];
    enum cpu_status status;
    int32_t stack_size;
    int32_t instruction_index;
    int32_t *memory;
    int32_t *stack_bottom;
    int32_t *stack_top;
};

static void cpu_clear(struct cpu *cpu)
{
    assert(cpu != NULL);

    memset(cpu->registers, 0, 4 * CELL_SIZE);
    cpu->status = CPU_OK;
    cpu->stack_size = 0;
    cpu->instruction_index = 0;
}

static bool reg_is_valid(struct cpu *cpu, int32_t reg)
{
    assert(cpu != NULL);

    if (reg >= REGISTER_A && reg <= REGISTER_D) {
        return true;
    }

    cpu->status = CPU_ILLEGAL_OPERAND;
    return false;
}

static bool in_stack(struct cpu *cpu, int32_t *address)
{
    assert(cpu != NULL);

    if (address >= cpu->stack_bottom - cpu->stack_size + 1 && address <= cpu->stack_bottom) {
        return true;
    }

    cpu->status = CPU_INVALID_STACK_OPERATION;
    return false;
}

static void handle_eof(struct cpu *cpu, int32_t reg)
{
    assert(cpu != NULL);

    cpu->registers[REGISTER_C] = 0;
    cpu->registers[reg] = -1;
    cpu->instruction_index++;
}

static void nop(struct cpu *cpu)
{
    cpu->instruction_index++;
}

static void halt(struct cpu *cpu)
{
    assert(cpu != NULL);

    cpu->status = CPU_HALTED;
}

static void add(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    cpu->registers[REGISTER_A] += cpu->registers[reg];
    cpu->instruction_index++;
}

static void sub(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    cpu->registers[REGISTER_A] -= cpu->registers[reg];
    cpu->instruction_index++;
}

static void mul(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    cpu->registers[REGISTER_A] *= cpu->registers[reg];
    cpu->instruction_index++;
}

static void div_reg(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    if (cpu->registers[reg] == 0) {
        cpu->status = CPU_DIV_BY_ZERO;
        return;
    }

    cpu->registers[REGISTER_A] /= cpu->registers[reg];
    cpu->instruction_index++;
}

static void inc(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    cpu->registers[reg]++;
    cpu->instruction_index++;
}

static void dec(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    cpu->registers[reg]--;
    cpu->instruction_index++;
}

static void loop(struct cpu *cpu)
{
    assert(cpu != NULL);

    if (cpu->registers[REGISTER_C] == 0) {
        cpu->instruction_index += 2;
        return;
    }

    size_t new_index = cpu->memory[++cpu->instruction_index];
    cpu->instruction_index = new_index;
}

static void movr(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    cpu->registers[reg] = cpu->memory[++cpu->instruction_index];
    cpu->instruction_index++;
}

static bool init_reg_target_address(struct cpu *cpu, int32_t *reg, int32_t **target_address)
{
    assert(cpu != NULL);

    *reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, *reg)) {
        return false;
    }

    int32_t num = cpu->memory[++cpu->instruction_index];
    int32_t index = cpu->registers[REGISTER_D] + num;
    *target_address = cpu->stack_bottom - cpu->stack_size + index + 1;

    if (!in_stack(cpu, *target_address)) {
        return false;
    }

    return true;
}

static void load(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg;
    int32_t *target_address;

    if (!init_reg_target_address(cpu, &reg, &target_address)) {
        return;
    }

    cpu->registers[reg] = *target_address;
    cpu->instruction_index++;
}

static void store(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg;
    int32_t *target_address;

    if (!init_reg_target_address(cpu, &reg, &target_address)) {
        return;
    }

    cpu->memory[target_address - cpu->memory] = cpu->registers[reg];
    cpu->instruction_index++;
}

static void in(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    int32_t num;
    int result = scanf("%" SCNd32, &num);

    if (result == 0) {
        cpu->status = CPU_IO_ERROR;
        return;
    }

    if (result == EOF) {
        handle_eof(cpu, reg);
        return;
    }

    cpu->registers[reg] = num;
    cpu->instruction_index++;
}

static void get(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    int32_t c = getchar();
    if (c == EOF) {
        handle_eof(cpu, reg);
        return;
    }

    cpu->registers[reg] = c;
    cpu->instruction_index++;
}

static void out(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    printf("%" PRId32, cpu->registers[reg]);
    cpu->instruction_index++;
}

static void put(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    int32_t c = cpu->registers[reg];
    if (c < 0 || c > 255) {
        cpu->status = CPU_ILLEGAL_OPERAND;
        return;
    }

    putchar(c);
    cpu->instruction_index++;
}

static void swap(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg1 = cpu->memory[++cpu->instruction_index];
    int32_t reg2 = cpu->memory[++cpu->instruction_index];

    if (!reg_is_valid(cpu, reg1) || !reg_is_valid(cpu, reg2)) {
        return;
    }

    int32_t reg2_bckp = cpu->registers[reg2];
    cpu->registers[reg2] = cpu->registers[reg1];
    cpu->registers[reg1] = reg2_bckp;
    cpu->instruction_index++;
}

static void push(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    if (cpu->stack_size == cpu->stack_bottom - cpu->stack_top + 1) {
        cpu->status = CPU_INVALID_STACK_OPERATION;
        return;
    }

    cpu->memory[(cpu->stack_bottom - cpu->stack_size - cpu->memory)] = cpu->registers[reg];
    cpu->stack_size++;
    cpu->instruction_index++;
}

static void pop(struct cpu *cpu)
{
    assert(cpu != NULL);

    int32_t reg = cpu->memory[++cpu->instruction_index];
    if (!reg_is_valid(cpu, reg)) {
        return;
    }

    if (cpu->stack_size <= 0) {
        cpu->status = CPU_INVALID_STACK_OPERATION;
        return;
    }

    cpu->stack_size--;
    int32_t idx = cpu->stack_bottom - cpu->stack_size - cpu->memory;
    int32_t value = cpu->memory[idx];
    cpu->registers[reg] = value;
    cpu->memory[idx] = 0;
    cpu->instruction_index++;
}

typedef void (*instruction)(struct cpu *cpu);

static const instruction instructions[] = {
    &nop,
    &halt,
    &add,
    &sub,
    &mul,
    &div_reg,
    &inc,
    &dec,
    &loop,
    &movr,
    &load,
    &store,
    &in,
    &get,
    &out,
    &put,
    &swap,
    &push,
    &pop
};

int32_t *cpu_create_memory(FILE *program, size_t stack_capacity, int32_t **stack_bottom)
{
    assert(program != NULL);
    assert(stack_bottom != NULL);

    int32_t capacity = BLOCK_SIZE;
    char *memory_init = malloc(capacity);
    if (memory_init == NULL) {
        return NULL;
    }

    int32_t size = 0;
    int c;

    while ((c = fgetc(program)) != EOF) {
        if (size + 1 > capacity) {
            char *old = memory_init;
            capacity += BLOCK_SIZE;
            memory_init = realloc(memory_init, capacity);
            if (memory_init == NULL) {
                free(old);
                return NULL;
            }
        }

        memory_init[size++] = c;
    }

    if (size % CELL_SIZE != 0) {
        free(memory_init);
        return NULL;
    }

    if ((size_t) (capacity - size) < stack_capacity * CELL_SIZE) {
        char *old = memory_init;
        capacity += BLOCK_SIZE * (((stack_capacity - 1) * CELL_SIZE - capacity + size + BLOCK_SIZE) / BLOCK_SIZE);

        memory_init = realloc(memory_init, capacity);
        if (memory_init == NULL) {
            free(old);
            return NULL;
        }
    }

    int32_t *memory = (int32_t *) memory_init;
    *stack_bottom = &memory[capacity / CELL_SIZE - 1];
    int32_t occupied_cells = size / CELL_SIZE;
    memset(&memory[occupied_cells], 0, (*stack_bottom - &memory[occupied_cells] + 1) * CELL_SIZE);
    return memory;
}

struct cpu *cpu_create(int32_t *memory, int32_t *stack_bottom, size_t stack_capacity)
{
    assert(memory != NULL);
    assert(stack_bottom != NULL);

    struct cpu *cpu = malloc(sizeof(struct cpu));
    if (cpu == NULL) {
        stack_bottom = NULL;
        return NULL;
    }

    cpu_clear(cpu);
    cpu->memory = memory;
    cpu->stack_bottom = stack_bottom;
    cpu->stack_top = stack_bottom - stack_capacity + 1;
    return cpu;
}

int32_t cpu_get_register(struct cpu *cpu, enum cpu_register reg)
{
    assert(cpu != NULL);

    assert(reg >= REGISTER_A && reg <= REGISTER_D);
    return cpu->registers[reg];
}

void cpu_set_register(struct cpu *cpu, enum cpu_register reg, int32_t value)
{
    assert(cpu != NULL);

    assert(reg >= REGISTER_A && reg <= REGISTER_D);
    cpu->registers[reg] = value;
}

enum cpu_status cpu_get_status(struct cpu *cpu)
{
    assert(cpu != NULL);

    return cpu->status;
}

int32_t cpu_get_stack_size(struct cpu *cpu)
{
    assert(cpu != NULL);

    return cpu->stack_size;
}

void cpu_destroy(struct cpu *cpu)
{
    assert(cpu != NULL);

    cpu_clear(cpu);
    free(cpu->memory);
    cpu->memory = NULL;
    cpu->stack_bottom = NULL;
}

void cpu_reset(struct cpu *cpu)
{
    assert(cpu != NULL);

    cpu_clear(cpu);
    memset(cpu->stack_bottom, 0, cpu->stack_size * CELL_SIZE);
    cpu->stack_bottom = NULL;
}

int cpu_step(struct cpu *cpu)
{
    assert(cpu != NULL);

    if (cpu->status != CPU_OK) {
        return 0;
    }

    int32_t index = cpu->instruction_index;
    if (index < 0 || index > cpu->stack_top - cpu->memory - 1) {
        cpu->status = CPU_INVALID_ADDRESS;
        return 0;
    }

    int32_t instruction = cpu->memory[index];
    if (instruction < 0 || instruction > 18) {
        cpu->status = CPU_ILLEGAL_INSTRUCTION;
        return 0;
    }

    instructions[instruction](cpu);
    if (cpu->status != CPU_OK) {
        return 0;
    }

    return 1;
}

long long cpu_run(struct cpu *cpu, size_t steps)
{
    assert(cpu != NULL);

    if (cpu->status != CPU_OK) {
        return 0;
    }

    long long performed = 0;
    while (cpu->status == CPU_OK && (performed < (long long) steps)) {
        cpu_step(cpu);
        performed++;
    }

    if (cpu->status == CPU_OK || cpu->status == CPU_HALTED) {
        return performed;
    }

    return -performed;
}
