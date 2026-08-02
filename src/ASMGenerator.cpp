#include "ASMGenerator.h"

// ==================== 辅助 ====================

std::string ASMGenerator::AllocReg() {
    int idx = (next_reg++) % 7;
    return "t" + std::to_string(idx);
}

static bool HasReturnValue(koopa_raw_value_t value) {
    if (!value || !value->ty) return false;
    return value->ty->tag != KOOPA_RTT_UNIT;
}

std::string ASMGenerator::GetOperand(koopa_raw_value_t value) {
    if (value->kind.tag == KOOPA_RVT_INTEGER) {
        std::string reg = AllocReg();
        os << "  li " << reg << ", " << value->kind.data.integer.value << "\n";
        return reg;
    }

    if (value->kind.tag == KOOPA_RVT_FUNC_ARG_REF) {
        // 函数参数引用: 从 a0-a7 寄存器或栈上加载
        auto &arg_ref = value->kind.data.func_arg_ref;
        std::string reg = AllocReg();
        if (arg_ref.index < 8) {
            os << "  mv " << reg << ", a" << arg_ref.index << "\n";
        } else {
            // 参数在调用者栈帧中, 位于当前 sp + frame_size 之上
            int offset = frame_size + (int)(arg_ref.index - 8) * 4;
            os << "  lw " << reg << ", " << offset << "(sp)\n";
        }
        return reg;
    }

    if (value->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
        // 全局变量: la 加载地址, lw 加载值
        std::string reg = AllocReg();
        std::string name = value->name;
        if (!name.empty() && name[0] == '@') name = name.substr(1);
        os << "  la " << reg << ", " << name << "\n";
        os << "  lw " << reg << ", 0(" << reg << ")" << "\n";
        return reg;
    }

    // 其他: 从栈帧中加载
    auto it = stack_offsets.find(value);
    if (it != stack_offsets.end()) {
        std::string reg = AllocReg();
        os << "  lw " << reg << ", " << it->second << "(sp)\n";
        return reg;
    }

    return "";
}

// ==================== 栈帧预扫描 ====================

void ASMGenerator::AssignStackOffsets(const koopa_raw_function_t &func) {
    stack_offsets.clear();
    slot_count = 0;
    int offset = 0;
    is_leaf = true;

    for (size_t i = 0; i < func->bbs.len; ++i) {
        auto bb = reinterpret_cast<koopa_raw_basic_block_t>(func->bbs.buffer[i]);
        for (size_t j = 0; j < bb->insts.len; ++j) {
            auto inst = reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[j]);

            // 检测是否有 call 指令 (用于判断是否为叶子函数)
            if (inst->kind.tag == KOOPA_RVT_CALL) {
                is_leaf = false;
            }

            // 为 alloc 和所有有返回值的指令分配栈槽
            if (inst->kind.tag == KOOPA_RVT_ALLOC || HasReturnValue(inst)) {
                stack_offsets[inst] = offset;
                offset += 4;
                slot_count++;
            }
        }
    }

    // 计算总帧大小: 栈槽 + ra (4 字节, 仅非叶子函数), 对齐到 16
    int total = offset;
    if (!is_leaf) {
        total += 4;  // ra 保存空间
    }
    frame_size = (total + 15) & ~15;
}

// ==================== 序言 / 尾声 ====================

void ASMGenerator::EmitPrologue() {
    os << "  .text\n";
    os << "  .globl " << cur_func << "\n";
    os << cur_func << ":\n";
    if (frame_size > 0) {
        os << "  addi sp, sp, -" << frame_size << "\n";
    }
    // 非叶子函数保存 ra
    if (!is_leaf) {
        os << "  sw ra, " << (frame_size - 4) << "(sp)\n";
    }
}

void ASMGenerator::EmitEpilogue() {
    os << ".L" << cur_func << "_exit:\n";
    // 非叶子函数恢复 ra
    if (!is_leaf) {
        os << "  lw ra, " << (frame_size - 4) << "(sp)\n";
    }
    if (frame_size > 0) {
        os << "  addi sp, sp, " << frame_size << "\n";
    }
    os << "  ret\n";
}

// ==================== Visit 函数 ====================

void ASMGenerator::Generate(const koopa_raw_program_t &program) {
    Visit(program);
}

void ASMGenerator::Visit(const koopa_raw_program_t &program) {
    // 1. 先输出全局变量 (.data 段)
    bool has_global = false;
    for (size_t i = 0; i < program.values.len; ++i) {
        auto val = reinterpret_cast<koopa_raw_value_t>(program.values.buffer[i]);
        if (val->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
            if (!has_global) {
                os << "  .data\n";
                has_global = true;
            }
            std::string name = val->name;
            if (!name.empty() && name[0] == '@') name = name.substr(1);
            os << "  .globl " << name << "\n";
            os << name << ":\n";

            auto init = val->kind.data.global_alloc.init;
            if (init->kind.tag == KOOPA_RVT_ZERO_INIT) {
                os << "  .zero 4\n";
            } else if (init->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  .word " << init->kind.data.integer.value << "\n";
            }
        }
    }

    // 2. 遍历所有函数 (跳过声明)
    for (size_t i = 0; i < program.funcs.len; ++i) {
        auto func = reinterpret_cast<koopa_raw_function_t>(program.funcs.buffer[i]);
        // 跳过函数声明 (bbs 为空)
        if (func->bbs.len == 0) continue;
        Visit(func);
    }
}

void ASMGenerator::Visit(const koopa_raw_slice_t &slice) {
    for (size_t i = 0; i < slice.len; ++i) {
        auto ptr = slice.buffer[i];
        switch (slice.kind) {
            case KOOPA_RSIK_FUNCTION:
                Visit(reinterpret_cast<koopa_raw_function_t>(ptr));
                break;
            case KOOPA_RSIK_BASIC_BLOCK:
                Visit(reinterpret_cast<koopa_raw_basic_block_t>(ptr));
                break;
            case KOOPA_RSIK_VALUE:
                Visit(reinterpret_cast<koopa_raw_value_t>(ptr));
                break;
            default:
                break;
        }
    }
}

void ASMGenerator::Visit(const koopa_raw_function_t &func) {
    cur_func = func->name;
    if (!cur_func.empty() && cur_func[0] == '@')
        cur_func = cur_func.substr(1);

    // 函数体为空（仅声明）则跳过
    if (func->bbs.len == 0)
        return;

    // 栈帧预扫描
    AssignStackOffsets(func);

    // 序言
    EmitPrologue();

    // 遍历基本块
    for (size_t i = 0; i < func->bbs.len; ++i) {
        auto bb = reinterpret_cast<koopa_raw_basic_block_t>(
            func->bbs.buffer[i]);
        Visit(bb);
    }

    // 尾声
    EmitEpilogue();
}

void ASMGenerator::Visit(const koopa_raw_basic_block_t &bb) {
    if (bb->name && bb->name[0] != '\0') {
        std::string label = bb->name;
        if (label[0] == '%') label = label.substr(1);
        // 基本块标签加函数名前缀, 确保多函数时全局唯一
        os << "." << cur_func << "_" << label << ":\n";
    }

    for (size_t i = 0; i < bb->insts.len; ++i) {
        auto inst = reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[i]);
        Visit(inst);
    }
}

void ASMGenerator::Visit(const koopa_raw_value_t &value) {
    switch (value->kind.tag) {
    case KOOPA_RVT_ALLOC:
        // 栈空间已在 AssignStackOffsets 中预留, 无需生成代码
        break;

    case KOOPA_RVT_LOAD: {
        auto &load = value->kind.data.load;
        int dst_offset = stack_offsets[value];
        std::string r = AllocReg();

        if (load.src->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
            // 全局变量加载: la + lw
            std::string name = load.src->name;
            if (!name.empty() && name[0] == '@') name = name.substr(1);
            os << "  la " << r << ", " << name << "\n";
            os << "  lw " << r << ", 0(" << r << ")" << "\n";
        } else {
            // 局部变量加载
            int src_offset = stack_offsets[load.src];
            os << "  lw " << r << ", " << src_offset << "(sp)\n";
        }
        os << "  sw " << r << ", " << dst_offset << "(sp)\n";
        break;
    }

    case KOOPA_RVT_STORE: {
        auto &store = value->kind.data.store;

        if (store.dest->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
            // 全局变量存储: la + sw
            std::string val_reg = GetOperand(store.value);
            std::string r = AllocReg();
            std::string name = store.dest->name;
            if (!name.empty() && name[0] == '@') name = name.substr(1);
            os << "  la " << r << ", " << name << "\n";
            os << "  sw " << val_reg << ", 0(" << r << ")\n";
        } else {
            // 局部变量存储
            std::string val_reg = GetOperand(store.value);
            int dest_offset = stack_offsets[store.dest];
            os << "  sw " << val_reg << ", " << dest_offset << "(sp)\n";
        }
        break;
    }

    case KOOPA_RVT_BINARY: {
        auto &bin = value->kind.data.binary;
        std::string lhs_reg = GetOperand(bin.lhs);
        std::string rhs_reg = GetOperand(bin.rhs);
        std::string dst_reg = AllocReg();

        switch (bin.op) {
        case KOOPA_RBO_ADD:
            os << "  add " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_SUB:
            os << "  sub " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_MUL:
            os << "  mul " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_DIV:
            os << "  div " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_MOD:
            os << "  rem " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_EQ: {
            os << "  sub " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            os << "  sltiu " << dst_reg << ", " << dst_reg << ", 1\n";
            break;
        }
        case KOOPA_RBO_NOT_EQ: {
            os << "  sub " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            os << "  sltu " << dst_reg << ", x0, " << dst_reg << "\n";
            break;
        }
        case KOOPA_RBO_LT:
            os << "  slt " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_GT:
            os << "  slt " << dst_reg << ", " << rhs_reg << ", " << lhs_reg << "\n";
            break;
        case KOOPA_RBO_LE:
            os << "  slt " << dst_reg << ", " << rhs_reg << ", " << lhs_reg << "\n";
            os << "  xori " << dst_reg << ", " << dst_reg << ", 1\n";
            break;
        case KOOPA_RBO_GE:
            os << "  slt " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            os << "  xori " << dst_reg << ", " << dst_reg << ", 1\n";
            break;
        case KOOPA_RBO_AND:
            os << "  and " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_OR:
            os << "  or " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_XOR:
            os << "  xor " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        default:
            break;
        }

        int dst_offset = stack_offsets[value];
        os << "  sw " << dst_reg << ", " << dst_offset << "(sp)\n";
        break;
    }

    case KOOPA_RVT_BRANCH: {
        auto &br = value->kind.data.branch;
        std::string cond_reg = GetOperand(br.cond);

        std::string true_label = br.true_bb->name;
        if (!true_label.empty() && true_label[0] == '%') true_label = true_label.substr(1);

        std::string false_label = br.false_bb->name;
        if (!false_label.empty() && false_label[0] == '%') false_label = false_label.substr(1);

        os << "  bnez " << cond_reg << ", ." << cur_func << "_" << true_label << "\n";
        os << "  j ." << cur_func << "_" << false_label << "\n";
        break;
    }

    case KOOPA_RVT_JUMP: {
        auto &jump = value->kind.data.jump;
        std::string target_label = jump.target->name;
        if (!target_label.empty() && target_label[0] == '%') target_label = target_label.substr(1);
        os << "  j ." << cur_func << "_" << target_label << "\n";
        break;
    }

    case KOOPA_RVT_CALL: {
        // 函数调用
        auto &call = value->kind.data.call;
        int num_args = (int)call.args.len;

        // 对于超过 8 个的参数, 需要在栈上分配空间
        int extra_args = num_args > 8 ? num_args - 8 : 0;
        if (extra_args > 0) {
            os << "  addi sp, sp, -" << (extra_args * 4) << "\n";
            for (int i = 8; i < num_args; ++i) {
                auto arg = reinterpret_cast<koopa_raw_value_t>(call.args.buffer[i]);
                std::string arg_reg = GetOperand(arg);
                os << "  sw " << arg_reg << ", " << ((i - 8) * 4) << "(sp)\n";
            }
        }

        // 前 8 个参数放入 a0-a7
        for (int i = 0; i < num_args && i < 8; ++i) {
            auto arg = reinterpret_cast<koopa_raw_value_t>(call.args.buffer[i]);
            std::string arg_reg = GetOperand(arg);
            os << "  mv a" << i << ", " << arg_reg << "\n";
        }

        // 调用
        std::string func_name = call.callee->name;
        if (!func_name.empty() && func_name[0] == '@') func_name = func_name.substr(1);
        os << "  call " << func_name << "\n";

        // 恢复 sp
        if (extra_args > 0) {
            os << "  addi sp, sp, " << (extra_args * 4) << "\n";
        }

        // 保存返回值 (如果有)
        if (HasReturnValue(value)) {
            int dst_offset = stack_offsets[value];
            os << "  sw a0, " << dst_offset << "(sp)\n";
        }
        break;
    }

    case KOOPA_RVT_RETURN: {
        auto &ret = value->kind.data.ret;
        if (ret.value) {
            // 有返回值: 加载到 a0
            if (ret.value->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li a0, " << ret.value->kind.data.integer.value << "\n";
            } else {
                int offset = stack_offsets[ret.value];
                os << "  lw a0, " << offset << "(sp)\n";
            }
        }
        // 跳转到函数出口
        os << "  j .L" << cur_func << "_exit\n";
        break;
    }

    default:
        break;
    }
}
