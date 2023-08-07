/*
* Copyright (c) 2023 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

/* TODO
 * [ ] pointer math, pointer types
 * [x] callk - allow linking to other named functions
 * [x] composite types - support for load, store, move, and function args.
 * [x] Have some mechanism for field access (dest = src.offset)
 * [x] Related, move type creation as opcodes like in SPIRV - have separate virtual "type slots" and value slots for this.
 * [ ] support for stack allocation of arrays
 * [ ] more math intrinsics
 * [x] source mapping (using built in Janet source mapping metadata on tuples)
 * [ ] better C interface for building up IR
 */

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#include "vector.h"
#include <math.h>
#endif

typedef enum {
    JANET_PRIM_U8,
    JANET_PRIM_S8,
    JANET_PRIM_U16,
    JANET_PRIM_S16,
    JANET_PRIM_U32,
    JANET_PRIM_S32,
    JANET_PRIM_U64,
    JANET_PRIM_S64,
    JANET_PRIM_F32,
    JANET_PRIM_F64,
    JANET_PRIM_POINTER,
    JANET_PRIM_BOOLEAN,
    JANET_PRIM_STRUCT,
} JanetPrim;

typedef struct {
    const char *name;
    JanetPrim prim;
} JanetPrimName;

static const JanetPrimName prim_names[] = {
    {"boolean", JANET_PRIM_BOOLEAN},
    {"f32", JANET_PRIM_F32},
    {"f64", JANET_PRIM_F64},
    {"pointer", JANET_PRIM_POINTER},
    {"s16", JANET_PRIM_S16},
    {"s32", JANET_PRIM_S32},
    {"s64", JANET_PRIM_S64},
    {"s8", JANET_PRIM_S8},
    {"struct", JANET_PRIM_STRUCT},
    {"u16", JANET_PRIM_U16},
    {"u32", JANET_PRIM_U32},
    {"u64", JANET_PRIM_U64},
    {"u8", JANET_PRIM_U8},
};

typedef enum {
    JANET_SYSOP_MOVE,
    JANET_SYSOP_CAST,
    JANET_SYSOP_ADD,
    JANET_SYSOP_SUBTRACT,
    JANET_SYSOP_MULTIPLY,
    JANET_SYSOP_DIVIDE,
    JANET_SYSOP_BAND,
    JANET_SYSOP_BOR,
    JANET_SYSOP_BXOR,
    JANET_SYSOP_BNOT,
    JANET_SYSOP_SHL,
    JANET_SYSOP_SHR,
    JANET_SYSOP_LOAD,
    JANET_SYSOP_STORE,
    JANET_SYSOP_GT,
    JANET_SYSOP_LT,
    JANET_SYSOP_EQ,
    JANET_SYSOP_NEQ,
    JANET_SYSOP_GTE,
    JANET_SYSOP_LTE,
    JANET_SYSOP_CONSTANT,
    JANET_SYSOP_CALL,
    JANET_SYSOP_RETURN,
    JANET_SYSOP_JUMP,
    JANET_SYSOP_BRANCH,
    JANET_SYSOP_ADDRESS,
    JANET_SYSOP_CALLK,
    JANET_SYSOP_TYPE_PRIMITIVE,
    JANET_SYSOP_TYPE_STRUCT,
    JANET_SYSOP_TYPE_BIND,
    JANET_SYSOP_ARG,
    JANET_SYSOP_FIELD_GET,
    JANET_SYSOP_FIELD_SET,
} JanetSysOp;

typedef struct {
    const char *name;
    JanetSysOp op;
} JanetSysInstrName;

static const JanetSysInstrName sys_op_names[] = {
    {"add", JANET_SYSOP_ADD},
    {"address", JANET_SYSOP_ADDRESS},
    {"band", JANET_SYSOP_BAND},
    {"bind", JANET_SYSOP_TYPE_BIND},
    {"bnot", JANET_SYSOP_BNOT},
    {"bor", JANET_SYSOP_BOR},
    {"branch", JANET_SYSOP_BRANCH},
    {"bxor", JANET_SYSOP_BXOR},
    {"call", JANET_SYSOP_CALL},
    {"cast", JANET_SYSOP_CAST},
    {"constant", JANET_SYSOP_CONSTANT},
    {"divide", JANET_SYSOP_DIVIDE},
    {"eq", JANET_SYSOP_EQ},
    {"fget", JANET_SYSOP_FIELD_GET},
    {"fset", JANET_SYSOP_FIELD_SET},
    {"gt", JANET_SYSOP_GT},
    {"gte", JANET_SYSOP_GTE},
    {"jump", JANET_SYSOP_JUMP},
    {"load", JANET_SYSOP_LOAD},
    {"lt", JANET_SYSOP_LT},
    {"lte", JANET_SYSOP_LTE},
    {"move", JANET_SYSOP_MOVE},
    {"multiply", JANET_SYSOP_MULTIPLY},
    {"neq", JANET_SYSOP_NEQ},
    {"prim", JANET_SYSOP_TYPE_PRIMITIVE},
    {"return", JANET_SYSOP_RETURN},
    {"shl", JANET_SYSOP_SHL},
    {"shr", JANET_SYSOP_SHR},
    {"store", JANET_SYSOP_STORE},
    {"struct", JANET_SYSOP_TYPE_STRUCT},
    {"subtract", JANET_SYSOP_SUBTRACT},
};

typedef struct {
    JanetPrim prim;
    uint32_t field_count;
    uint32_t field_start;
} JanetSysTypeInfo;

typedef struct {
    uint32_t type;
} JanetSysTypeField;

typedef struct {
    JanetSysOp opcode;
    union {
        struct {
            uint32_t dest;
            uint32_t lhs;
            uint32_t rhs;
        } three;
        struct {
            uint32_t dest;
            uint32_t callee;
            uint32_t arg_count;
        } call;
        struct {
            uint32_t dest;
            uint32_t src;
        } two;
        struct {
            uint32_t src;
        } one;
        struct {
            uint32_t to;
        } jump;
        struct {
            uint32_t cond;
            uint32_t to;
        } branch;
        struct {
            uint32_t dest;
            uint32_t constant;
        } constant;
        struct {
            uint32_t dest;
            uint32_t constant;
            uint32_t arg_count;
        } callk;
        struct {
            uint32_t dest_type;
            uint32_t prim;
        } type_prim;
        struct {
            uint32_t dest_type;
            uint32_t arg_count;
        } type_types;
        struct {
            uint32_t dest;
            uint32_t type;
        } type_bind;
        struct {
            uint32_t args[3];
        } arg;
        struct {
            uint32_t r;
            uint32_t st;
            uint32_t field;
        } field;
    };
    int32_t line;
    int32_t column;
} JanetSysInstruction;

typedef struct {
    JanetString link_name;
    uint32_t instruction_count;
    uint32_t register_count;
    uint32_t type_def_count;
    uint32_t field_def_count;
    uint32_t constant_count;
    uint32_t return_type;
    uint32_t *types;
    JanetSysTypeInfo *type_defs;
    JanetSysTypeField *field_defs;
    JanetSysInstruction *instructions;
    Janet *constants;
    uint32_t parameter_count;
} JanetSysIR;

/* Parse assembly */

static void instr_assert_length(JanetTuple tup, int32_t len, Janet x) {
    if (janet_tuple_length(tup) != len) {
        janet_panicf("expected instruction of length %d, got %v", len, x);
    }
}

static void instr_assert_min_length(JanetTuple tup, int32_t minlen, Janet x) {
    if (janet_tuple_length(tup) < minlen) {
        janet_panicf("expected instruction of at least ength %d, got %v", minlen, x);
    }
}

static uint32_t instr_read_operand(Janet x, JanetSysIR *ir) {
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer operand, got %v", x);
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    if (operand >= ir->register_count) {
        ir->register_count = operand + 1;
    }
    return operand;
}

static uint32_t instr_read_field(Janet x, JanetSysIR *ir) {
    if (!janet_checkuint(x)) janet_panicf("expected non-negative field index, got %v", x);
    (void) ir; /* Perhaps support syntax for named fields instead of numbered */
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    return operand;
}

static uint32_t instr_read_type_operand(Janet x, JanetSysIR *ir) {
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer operand, got %v", x);
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    if (operand >= ir->type_def_count) {
        ir->type_def_count = operand + 1;
    }
    return operand;
}

static JanetPrim instr_read_prim(Janet x) {
    if (!janet_checktype(x, JANET_SYMBOL)) {
        janet_panicf("expected primitive type, got %v", x);
    }
    JanetSymbol sym_type = janet_unwrap_symbol(x);
    const JanetPrimName *namedata = janet_strbinsearch(prim_names,
                                    sizeof(prim_names) / sizeof(prim_names[0]), sizeof(prim_names[0]), sym_type);
    if (NULL == namedata) {
        janet_panicf("unknown type %v", x);
    }
    return namedata->prim;
}

static uint32_t instr_read_label(Janet x, JanetTable *labels) {
    Janet check = janet_table_get(labels, x);
    if (!janet_checktype(check, JANET_NIL)) return (uint32_t) janet_unwrap_number(check);
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer label, got %v", x);
    return (uint32_t) janet_unwrap_number(x);
}

static void janet_sysir_init_instructions(JanetSysIR *out, JanetView instructions) {

    // TODO - add labels back

    JanetSysInstruction *ir = janet_malloc(sizeof(JanetSysInstruction) * 100);
    out->instructions = ir;
    uint32_t cursor = 0;
    JanetTable *labels = janet_table(0);
    JanetTable *constant_cache = janet_table(0);
    uint32_t next_constant = 0;

    /* Parse instructions */
    Janet x = janet_wrap_nil();
    for (int32_t i = 0; i < instructions.len; i++) {
        x = instructions.items[i];
        if (janet_checktype(x, JANET_KEYWORD)) continue;
        if (!janet_checktype(x, JANET_TUPLE)) {
            janet_panicf("expected instruction to be tuple, got %V", x);
        }
        JanetTuple tuple = janet_unwrap_tuple(x);
        if (janet_tuple_length(tuple) < 1) {
            janet_panic("invalid instruction, no opcode");
        }
        int32_t line = janet_tuple_sm_line(tuple);
        int32_t column = janet_tuple_sm_column(tuple);
        Janet opvalue = tuple[0];
        if (!janet_checktype(opvalue, JANET_SYMBOL)) {
            janet_panicf("expected opcode symbol, found %V", opvalue);
        }
        JanetSymbol opsymbol = janet_unwrap_symbol(opvalue);
        const JanetSysInstrName *namedata = janet_strbinsearch(sys_op_names,
                                            sizeof(sys_op_names) / sizeof(sys_op_names[0]), sizeof(sys_op_names[0]), opsymbol);
        if (NULL == namedata) {
            janet_panicf("unknown instruction %.4p", x);
        }
        JanetSysOp opcode = namedata->op;
        JanetSysInstruction instruction;
        instruction.opcode = opcode;
        instruction.line = line;
        instruction.column = column;
        switch (opcode) {
            case JANET_SYSOP_CALLK:
            case JANET_SYSOP_ARG:
                janet_panicf("invalid instruction %v", x);
                break;
            case JANET_SYSOP_ADD:
            case JANET_SYSOP_SUBTRACT:
            case JANET_SYSOP_MULTIPLY:
            case JANET_SYSOP_DIVIDE:
            case JANET_SYSOP_BAND:
            case JANET_SYSOP_BOR:
            case JANET_SYSOP_BXOR:
            case JANET_SYSOP_SHL:
            case JANET_SYSOP_SHR:
            case JANET_SYSOP_GT:
            case JANET_SYSOP_GTE:
            case JANET_SYSOP_LT:
            case JANET_SYSOP_LTE:
            case JANET_SYSOP_EQ:
            case JANET_SYSOP_NEQ:
                instr_assert_length(tuple, 4, opvalue);
                instruction.three.dest = instr_read_operand(tuple[1], out);
                instruction.three.lhs = instr_read_operand(tuple[2], out);
                instruction.three.rhs = instr_read_operand(tuple[3], out);
                ir[cursor++] = instruction;
                break;
            case JANET_SYSOP_CALL:
                instr_assert_min_length(tuple, 2, opvalue);
                instruction.call.dest = instr_read_operand(tuple[1], out);
                Janet c = tuple[2];
                if (janet_checktype(c, JANET_SYMBOL)) {
                    Janet check = janet_table_get(constant_cache, c);
                    if (janet_checktype(check, JANET_NUMBER)) {
                        instruction.callk.constant = (uint32_t) janet_unwrap_number(check);
                    } else {
                        instruction.callk.constant = next_constant;
                        janet_table_put(constant_cache, c, janet_wrap_integer(next_constant));
                        next_constant++;
                    }
                    opcode = JANET_SYSOP_CALLK;
                    instruction.opcode = opcode;
                } else {
                    instruction.call.callee = instr_read_operand(tuple[2], out);
                }
                instruction.call.arg_count = janet_tuple_length(tuple) - 3;
                ir[cursor++] = instruction;
                for (int32_t j = 3; j < janet_tuple_length(tuple); j += 3) {
                    JanetSysInstruction arginstr;
                    arginstr.opcode = JANET_SYSOP_ARG;
                    arginstr.line = line;
                    arginstr.column = column;
                    arginstr.arg.args[0] = 0;
                    arginstr.arg.args[1] = 0;
                    arginstr.arg.args[2] = 0;
                    int32_t remaining = janet_tuple_length(tuple) - j;
                    if (remaining > 3) remaining = 3;
                    for (int32_t k = 0; k < remaining; k++) {
                        arginstr.arg.args[k] = instr_read_operand(tuple[j + k], out);
                    }
                    ir[cursor++] = arginstr;
                }
                break;
            case JANET_SYSOP_LOAD:
            case JANET_SYSOP_STORE:
            case JANET_SYSOP_MOVE:
            case JANET_SYSOP_CAST:
            case JANET_SYSOP_BNOT:
            case JANET_SYSOP_ADDRESS:
                instr_assert_length(tuple, 3, opvalue);
                instruction.two.dest = instr_read_operand(tuple[1], out);
                instruction.two.src = instr_read_operand(tuple[2], out);
                ir[cursor++] = instruction;
                break;
            case JANET_SYSOP_FIELD_GET:
            case JANET_SYSOP_FIELD_SET:
                instr_assert_length(tuple, 4, opvalue);
                instruction.field.r = instr_read_operand(tuple[1], out);
                instruction.field.st = instr_read_operand(tuple[2], out);
                instruction.field.field = instr_read_field(tuple[3], out);
                ir[cursor++] = instruction;
                break;
            case JANET_SYSOP_RETURN:
                instr_assert_length(tuple, 2, opvalue);
                instruction.one.src = instr_read_operand(tuple[1], out);
                ir[cursor++] = instruction;
                break;
            case JANET_SYSOP_BRANCH:
                instr_assert_length(tuple, 3, opvalue);
                instruction.branch.cond = instr_read_operand(tuple[1], out);
                instruction.branch.to = instr_read_label(tuple[2], labels);
                ir[cursor++] = instruction;
                break;
            case JANET_SYSOP_JUMP:
                instr_assert_length(tuple, 2, opvalue);
                instruction.jump.to = instr_read_label(tuple[1], labels);
                ir[cursor++] = instruction;
                break;
            case JANET_SYSOP_CONSTANT: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.constant.dest = instr_read_operand(tuple[1], out);
                Janet c = tuple[2];
                Janet check = janet_table_get(constant_cache, c);
                if (janet_checktype(check, JANET_NUMBER)) {
                    instruction.constant.constant = (uint32_t) janet_unwrap_number(check);
                } else {
                    instruction.constant.constant = next_constant;
                    janet_table_put(constant_cache, c, janet_wrap_number(next_constant));
                    next_constant++;
                }
                ir[cursor++] = instruction;
                break;
            }
            case JANET_SYSOP_TYPE_PRIMITIVE: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.type_prim.dest_type = instr_read_type_operand(tuple[1], out);
                instruction.type_prim.prim = instr_read_prim(tuple[2]);
                ir[cursor++] = instruction;
                break;
            }
            case JANET_SYSOP_TYPE_STRUCT: {
                instr_assert_min_length(tuple, 1, opvalue);
                instruction.type_types.dest_type = instr_read_type_operand(tuple[1], out);
                instruction.type_types.arg_count = janet_tuple_length(tuple) - 2;
                ir[cursor++] = instruction;
                for (int32_t j = 2; j < janet_tuple_length(tuple); j += 3) {
                    JanetSysInstruction arginstr;
                    arginstr.opcode = JANET_SYSOP_ARG;
                    arginstr.line = line;
                    arginstr.column = column;
                    arginstr.arg.args[0] = 0;
                    arginstr.arg.args[1] = 0;
                    arginstr.arg.args[2] = 0;
                    int32_t remaining = janet_tuple_length(tuple) - j;
                    if (remaining > 3) remaining = 3;
                    for (int32_t k = 0; k < remaining; k++) {
                        arginstr.arg.args[k] = instr_read_type_operand(tuple[j + k], out);
                    }
                    ir[cursor++] = arginstr;
                }
                break;
            }
            case JANET_SYSOP_TYPE_BIND: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.type_bind.dest = instr_read_operand(tuple[1], out);
                instruction.type_bind.type = instr_read_type_operand(tuple[2], out);
                ir[cursor++] = instruction;
                break;
            }
        }
    }

    /* Check last instruction is jump or return */
    if ((ir[cursor - 1].opcode != JANET_SYSOP_JUMP) && (ir[cursor - 1].opcode != JANET_SYSOP_RETURN)) {
        janet_panicf("last instruction must be jump or return, got %v", x);
    }

    /* Fix up instructions table */
    ir = janet_realloc(ir, sizeof(JanetSysInstruction) * cursor);
    out->instructions = ir;
    out->instruction_count = cursor;

    /* Build constants */
    out->constant_count = next_constant;
    out->constants = next_constant ? janet_malloc(sizeof(Janet) * out->constant_count) : NULL;
    for (int32_t i = 0; i < constant_cache->capacity; i++) {
        JanetKV kv = constant_cache->data[i];
        if (!janet_checktype(kv.key, JANET_NIL)) {
            uint32_t index = (uint32_t) janet_unwrap_number(kv.value);
            out->constants[index] = kv.key;
        }
    }
}

/* Build up type tables */
static void janet_sysir_init_types(JanetSysIR *sysir) {
    JanetSysTypeField *fields = NULL;
    if (sysir->type_def_count == 0) {
        sysir->type_def_count++;
    }
    JanetSysTypeInfo *type_defs = janet_malloc(sizeof(JanetSysTypeInfo) * (sysir->type_def_count));
    uint32_t *types = janet_malloc(sizeof(uint32_t) * sysir->register_count);
    sysir->type_defs = type_defs;
    sysir->types = types;
    sysir->type_defs[0].field_count = 0;
    sysir->type_defs[0].prim = JANET_PRIM_S32;
_i4:
    for (uint32_t i = 0; i < sysir->register_count; i++) {
        sysir->types[i] = 0;
    }

    for (uint32_t i = 0; i < sysir->instruction_count; i++) {
        JanetSysInstruction instruction = sysir->instructions[i];
        switch (instruction.opcode) {
            default:
                break;
            case JANET_SYSOP_TYPE_PRIMITIVE: {
                uint32_t type_def = instruction.type_prim.dest_type;
                type_defs[type_def].field_count = 0;
                type_defs[type_def].prim = instruction.type_prim.prim;
                break;
            }
            case JANET_SYSOP_TYPE_STRUCT: {
                uint32_t type_def = instruction.type_types.dest_type;
                type_defs[type_def].field_count = instruction.type_types.arg_count;
                type_defs[type_def].prim = JANET_PRIM_STRUCT;
                type_defs[type_def].field_start = (uint32_t) janet_v_count(fields);
                for (uint32_t j = 0; j < instruction.type_types.arg_count; j++) {
                    uint32_t offset = j / 3 + 1;
                    uint32_t index = j % 3;
                    JanetSysInstruction arg_instruction = sysir->instructions[i + offset];
                    uint32_t arg = arg_instruction.arg.args[index];
                    JanetSysTypeField field;
                    field.type = arg;
                    janet_v_push(fields, field);
                }
                break;
            }
            case JANET_SYSOP_TYPE_BIND: {
                uint32_t type = instruction.type_bind.type;
                uint32_t dest = instruction.type_bind.dest;
                types[dest] = type;
                break;
            }
        }
    }

    sysir->field_defs = janet_v_flatten(fields);
}

/* Type checking */

static void tcheck_boolean(JanetSysIR *sysir, uint32_t reg1) {
    uint32_t t1 = sysir->types[reg1];
    if (sysir->type_defs[t1].prim != JANET_PRIM_BOOLEAN) {
        janet_panicf("type failure, expected boolean, got type-id:%d", t1); /* TODO improve this */
    }
}

static void tcheck_integer(JanetSysIR *sysir, uint32_t reg1) {
    JanetPrim t1 = sysir->type_defs[sysir->types[reg1]].prim;
    if (t1 != JANET_PRIM_S32 &&
        t1 != JANET_PRIM_S64 &&
        t1 != JANET_PRIM_S16 &&
        t1 != JANET_PRIM_S8 &&
        t1 != JANET_PRIM_U32 &&
        t1 != JANET_PRIM_U64 &&
        t1 != JANET_PRIM_U16 &&
        t1 != JANET_PRIM_U8) {
        janet_panicf("type failure, expected integer, got type-id:%d", t1); /* TODO improve this */
    }
}

static void tcheck_pointer(JanetSysIR *sysir, uint32_t reg1) {
    uint32_t t1 = sysir->types[reg1];
    if (sysir->type_defs[t1].prim != JANET_PRIM_POINTER) {
        janet_panicf("type failure, expected pointer, got type-id:%d", t1);
    }
}

static void tcheck_struct(JanetSysIR *sysir, uint32_t reg1) {
    uint32_t t1 = sysir->types[reg1];
    if (sysir->type_defs[t1].prim != JANET_PRIM_STRUCT) {
        janet_panicf("type failure, expected struct, got type-id:%d", t1);
    }
}

static void tcheck_equal(JanetSysIR *sysir, uint32_t reg1, uint32_t reg2) {
    uint32_t t1 = sysir->types[reg1];
    uint32_t t2 = sysir->types[reg2];
    if (t1 != t2) {
        janet_panicf("type failure, type-id:%d does not match type-id:%d", t1, t2); /* TODO improve this */
    }
}

static void janet_sysir_type_check(JanetSysIR *sysir) {
    int found_return = 0;
    for (uint32_t i = 0; i < sysir->instruction_count; i++) {
        JanetSysInstruction instruction = sysir->instructions[i];
        switch (instruction.opcode) {
            case JANET_SYSOP_TYPE_PRIMITIVE:
            case JANET_SYSOP_TYPE_STRUCT:
            case JANET_SYSOP_TYPE_BIND:
            case JANET_SYSOP_ARG:
            case JANET_SYSOP_JUMP:
                break;
            case JANET_SYSOP_RETURN: {
                uint32_t ret_type = sysir->types[instruction.one.src];
                if (found_return) {
                    if (sysir->return_type != ret_type) {
                        janet_panicf("multiple return types are not allowed: type-id:%d and type-id:%d", ret_type, sysir->return_type);
                    }
                } else {
                    sysir->return_type = ret_type;
                }
                found_return = 1;
                break;
            }
            case JANET_SYSOP_MOVE:
                tcheck_equal(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_CAST:
                break;
            case JANET_SYSOP_ADD:
            case JANET_SYSOP_SUBTRACT:
            case JANET_SYSOP_MULTIPLY:
            case JANET_SYSOP_DIVIDE:
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_BAND:
            case JANET_SYSOP_BOR:
            case JANET_SYSOP_BXOR:
                tcheck_integer(sysir, instruction.three.lhs);
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_BNOT:
                tcheck_integer(sysir, instruction.two.src);
                tcheck_equal(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_SHL:
            case JANET_SYSOP_SHR:
                tcheck_integer(sysir, instruction.three.lhs);
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_LOAD:
                tcheck_pointer(sysir, instruction.two.src);
                break;
            case JANET_SYSOP_STORE:
                tcheck_pointer(sysir, instruction.two.dest);
                break;
            case JANET_SYSOP_GT:
            case JANET_SYSOP_LT:
            case JANET_SYSOP_EQ:
            case JANET_SYSOP_NEQ:
            case JANET_SYSOP_GTE:
            case JANET_SYSOP_LTE:
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                tcheck_boolean(sysir, instruction.three.dest);
                break;
            case JANET_SYSOP_ADDRESS:
                tcheck_pointer(sysir, instruction.two.dest);
                break;
            case JANET_SYSOP_BRANCH:
                tcheck_boolean(sysir, instruction.branch.cond);
                break;
            case JANET_SYSOP_CONSTANT:
                /* TODO - check constant matches type */
                break;
            case JANET_SYSOP_CALL:
                tcheck_pointer(sysir, instruction.call.callee);
                break;
            case JANET_SYSOP_FIELD_GET:
            case JANET_SYSOP_FIELD_SET:
                tcheck_struct(sysir, instruction.field.st);
                uint32_t struct_type = sysir->types[instruction.field.st];
                if (instruction.field.field >= sysir->type_defs[struct_type].field_count) {
                    janet_panicf("invalid field index %u", instruction.field.field);
                }
                uint32_t field_type = sysir->type_defs[struct_type].field_start + instruction.field.field;
                uint32_t tfield = sysir->field_defs[field_type].type;
                uint32_t tdest = sysir->types[instruction.field.r];
                if (tfield != tdest) {
                    janet_panicf("field of type type-id:%d does not match type-id:%d", tfield, tdest);
                }
                break;
            case JANET_SYSOP_CALLK:
                /* TODO - check function return type */
                break;
        }
    }
}

void janet_sys_ir_init_from_table(JanetSysIR *ir, JanetTable *table) {
    ir->instructions = NULL;
    ir->types = NULL;
    ir->type_defs = NULL;
    ir->field_defs = NULL;
    ir->constants = NULL;
    ir->link_name = NULL;
    ir->register_count = 0;
    ir->type_def_count = 0;
    ir->field_def_count = 0;
    ir->constant_count = 0;
    ir->return_type = 0;
    ir->parameter_count = 0;
    Janet assembly = janet_table_get(table, janet_ckeywordv("instructions"));
    Janet param_count = janet_table_get(table, janet_ckeywordv("parameter-count"));
    Janet link_namev = janet_table_get(table, janet_ckeywordv("link-name"));
    JanetView asm_view = janet_getindexed(&assembly, 0);
    JanetString link_name = janet_getstring(&link_namev, 0);
    int32_t parameter_count = janet_getnat(&param_count, 0);
    ir->parameter_count = parameter_count;
    ir->link_name = link_name;
    janet_sysir_init_instructions(ir, asm_view);
    janet_sysir_init_types(ir);
    janet_sysir_type_check(ir);
}

/* Lowering to C */

static const char *c_prim_names[] = {
    "uint8_t",
    "int8_t",
    "uint16_t",
    "int16_t",
    "uint32_t",
    "int32_t",
    "uint64_t",
    "int64_t",
    "float",
    "double",
    "char *",
    "bool"
};

void janet_sys_ir_lower_to_c(JanetSysIR *ir, JanetBuffer *buffer) {

#define EMITBINOP(OP) \
    janet_formatb(buffer, "_r%u = _r%u " OP " _r%u;\n", instruction.three.dest, instruction.three.lhs, instruction.three.rhs)

    janet_formatb(buffer, "#include <stdint.h>\n#include <tgmath.h>\n\n");

    /* Emit type defs */
    for (uint32_t i = 0; i < ir->instruction_count; i++) {
        JanetSysInstruction instruction = ir->instructions[i];
        switch (instruction.opcode) {
            default:
                continue;
            case JANET_SYSOP_TYPE_PRIMITIVE:
            case JANET_SYSOP_TYPE_STRUCT:
                break;
        }
        if (instruction.line > 0) {
            janet_formatb(buffer, "#line %d\n", instruction.line);
        }
        switch (instruction.opcode) {
            default:
                break;
            case JANET_SYSOP_TYPE_PRIMITIVE:
                janet_formatb(buffer, "typedef %s _t%u;\n", c_prim_names[instruction.type_prim.prim], instruction.type_prim.dest_type);
                break;
            case JANET_SYSOP_TYPE_STRUCT:
                janet_formatb(buffer, "typedef struct {\n");
                for (uint32_t j = 0; j < instruction.type_types.arg_count; j++) {
                    uint32_t offset = j / 3 + 1;
                    uint32_t index = j % 3;
                    JanetSysInstruction arg_instruction = ir->instructions[i + offset];
                    janet_formatb(buffer, "  _t%u _f%u;\n", arg_instruction.arg.args[index], j);
                }
                janet_formatb(buffer, "} _t%u;\n", instruction.type_types.dest_type);
                break;
        }
    }

    /* Emit header */
    janet_formatb(buffer, "_t%u %s(", ir->return_type, (ir->link_name != NULL) ? ir->link_name : janet_cstring("_thunk"));
    for (uint32_t i = 0; i < ir->parameter_count; i++) {
        if (i) janet_buffer_push_cstring(buffer, ", ");
        janet_formatb(buffer, "_t%u _r%u", ir->types[i], i);
    }
    janet_buffer_push_cstring(buffer, ")\n{\n");
    for (uint32_t i = ir->parameter_count; i < ir->register_count; i++) {
        janet_formatb(buffer, "  _t%u _r%u;\n", ir->types[i], i);
    }
    janet_buffer_push_cstring(buffer, "\n");

    /* Emit body */
    for (uint32_t i = 0; i < ir->instruction_count; i++) {
        JanetSysInstruction instruction = ir->instructions[i];
        /* Skip instruction label for some opcodes */
        switch (instruction.opcode) {
            case JANET_SYSOP_TYPE_PRIMITIVE:
            case JANET_SYSOP_TYPE_BIND:
            case JANET_SYSOP_TYPE_STRUCT:
            case JANET_SYSOP_ARG:
                continue;
            default:
                break;
        }
        janet_formatb(buffer, "_i%u:\n", i);
        if (instruction.line > 0) {
            janet_formatb(buffer, "#line %d\n  ", instruction.line);
        }
        janet_buffer_push_cstring(buffer, "  ");
        switch (instruction.opcode) {
            case JANET_SYSOP_TYPE_PRIMITIVE:
            case JANET_SYSOP_TYPE_BIND:
            case JANET_SYSOP_TYPE_STRUCT:
            case JANET_SYSOP_ARG:
                break;
            case JANET_SYSOP_CONSTANT: {
                uint32_t cast = ir->types[instruction.two.dest];
                janet_formatb(buffer, "_r%u = (_t%u) %j;\n", instruction.two.dest, cast, ir->constants[instruction.two.src]);
                break;
            }
            case JANET_SYSOP_ADDRESS:
                janet_formatb(buffer, "_r%u = (char *) &_r%u;\n", instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_JUMP:
                janet_formatb(buffer, "goto _i%u;\n", instruction.jump.to);
                break;
            case JANET_SYSOP_BRANCH:
                janet_formatb(buffer, "if (_r%u) goto _i%u;\n", instruction.branch.cond, instruction.branch.to);
                break;
            case JANET_SYSOP_RETURN:
                janet_formatb(buffer, "return _r%u;\n", instruction.one.src);
                break;
            case JANET_SYSOP_ADD:
                EMITBINOP("+");
                break;
            case JANET_SYSOP_SUBTRACT:
                EMITBINOP("-");
                break;
            case JANET_SYSOP_MULTIPLY:
                EMITBINOP("*");
                break;
            case JANET_SYSOP_DIVIDE:
                EMITBINOP("/");
                break;
            case JANET_SYSOP_GT:
                EMITBINOP(">");
                break;
            case JANET_SYSOP_GTE:
                EMITBINOP(">");
                break;
            case JANET_SYSOP_LT:
                EMITBINOP("<");
                break;
            case JANET_SYSOP_LTE:
                EMITBINOP("<=");
                break;
            case JANET_SYSOP_EQ:
                EMITBINOP("==");
                break;
            case JANET_SYSOP_NEQ:
                EMITBINOP("!=");
                break;
            case JANET_SYSOP_BAND:
                EMITBINOP("&");
                break;
            case JANET_SYSOP_BOR:
                EMITBINOP("|");
                break;
            case JANET_SYSOP_BXOR:
                EMITBINOP("^");
                break;
            case JANET_SYSOP_SHL:
                EMITBINOP("<<");
                break;
            case JANET_SYSOP_SHR:
                EMITBINOP(">>");
                break;
            case JANET_SYSOP_CALL:
                janet_formatb(buffer, "_r%u = _r%u(", instruction.call.dest, instruction.call.callee);
                for (uint32_t j = 0; j < instruction.call.arg_count; j++) {
                    uint32_t offset = j / 3 + 1;
                    uint32_t index = j % 3;
                    JanetSysInstruction arg_instruction = ir->instructions[i + offset];
                    janet_formatb(buffer, j ? ", _r%u" : "_r%u", arg_instruction.arg.args[index]);
                }
                janet_formatb(buffer, ");\n");
                break;
            case JANET_SYSOP_CALLK:
                janet_formatb(buffer, "_r%u = %j(", instruction.callk.dest, ir->constants[instruction.callk.constant]);
                for (uint32_t j = 0; j < instruction.callk.arg_count; j++) {
                    uint32_t offset = j / 3 + 1;
                    uint32_t index = j % 3;
                    JanetSysInstruction arg_instruction = ir->instructions[i + offset];
                    janet_formatb(buffer, j ? ", _r%u" : "_r%u", arg_instruction.arg.args[index]);
                }
                janet_formatb(buffer, ");\n");
                break;
            case JANET_SYSOP_CAST:
                /* TODO - making casting rules explicit instead of just from C */
                janet_formatb(buffer, "_r%u = (_t%u) _r%u;\n", instruction.two.dest, ir->types[instruction.two.dest], instruction.two.src);
                break;
            case JANET_SYSOP_MOVE:
                janet_formatb(buffer, "_r%u = _r%u;\n", instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_BNOT:
                janet_formatb(buffer, "_r%u = ~_r%u;\n", instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_LOAD:
                janet_formatb(buffer, "_r%u = *((%s *) _r%u);\n", instruction.two.dest, c_prim_names[ir->types[instruction.two.dest]], instruction.two.src);
                break;
            case JANET_SYSOP_STORE:
                janet_formatb(buffer, "*((%s *) _r%u) = _r%u;\n", c_prim_names[ir->types[instruction.two.src]], instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_FIELD_GET:
                janet_formatb(buffer, "_r%u = _r%u._f%u;\n", instruction.field.r, instruction.field.st, instruction.field.field);
                break;
            case JANET_SYSOP_FIELD_SET:
                janet_formatb(buffer, "_r%u._f%u = _r%u;\n", instruction.field.st, instruction.field.field, instruction.field.r);
                break;
        }
    }

    janet_buffer_push_cstring(buffer, "}\n");
#undef EMITBINOP

}

static int sysir_gc(void *p, size_t s) {
    JanetSysIR *ir = (JanetSysIR *)p;
    (void) s;
    janet_free(ir->constants);
    janet_free(ir->types);
    janet_free(ir->instructions);
    janet_free(ir->type_defs);
    janet_free(ir->field_defs);
    return 0;
}

static int sysir_gcmark(void *p, size_t s) {
    JanetSysIR *ir = (JanetSysIR *)p;
    (void) s;
    for (uint32_t i = 0; i < ir->constant_count; i++) {
        janet_mark(ir->constants[i]);
    }
    if (ir->link_name != NULL) {
        janet_mark(janet_wrap_string(ir->link_name));
    }
    return 0;
}

static const JanetAbstractType janet_sysir_type = {
    "core/sysir",
    sysir_gc,
    sysir_gcmark,
    JANET_ATEND_GCMARK
};

JANET_CORE_FN(cfun_sysir_asm,
              "(sysir/asm assembly)",
              "Compile the system dialect IR into an object that can be manipulated, optimized, or lowered to other targets like C.") {
    janet_fixarity(argc, 1);
    JanetTable *tab = janet_gettable(argv, 0);
    JanetSysIR *sysir = janet_abstract(&janet_sysir_type, sizeof(JanetSysIR));
    janet_sys_ir_init_from_table(sysir, tab);
    return janet_wrap_abstract(sysir);
}

JANET_CORE_FN(cfun_sysir_toc,
              "(sysir/to-c sysir &opt buffer)",
              "Lower some IR to a C function. Return a modified buffer that can be passed to a C compiler.") {
    janet_arity(argc, 1, 2);
    JanetSysIR *ir = janet_getabstract(argv, 0, &janet_sysir_type);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 1, 0);
    janet_sys_ir_lower_to_c(ir, buffer);
    return janet_wrap_buffer(buffer);
}

void janet_lib_sysir(JanetTable *env) {
    JanetRegExt cfuns[] = {
        JANET_CORE_REG("sysir/asm", cfun_sysir_asm),
        JANET_CORE_REG("sysir/to-c", cfun_sysir_toc),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, cfuns);
}
