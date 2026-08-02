#include "ASMGenerator.h"

// ==================== 辅助 ====================

// 分配一个新的临时寄存器 (t0-t6)
std::string ASMGenerator::AllocReg() {
    int idx = (next_reg++) % 7;
    return "t" + std::to_string(idx);
}

// 判断一个值是否具有返回值 (非 unit 类型)
static bool HasReturnValue(koopa_raw_value_t value) {
    if (!value || !value->ty) return false;
    return value->ty->tag != KOOPA_RTT_UNIT;
}

// 将 Koopa IR 值加载到寄存器中, 返回寄存器名
// 如果值是整数常量: li 加载
// 否则: 从值的栈槽中 lw 加载
std::string ASMGenerator::GetOperand(koopa_raw_value_t value) {
    if (value->kind.tag == KOOPA_RVT_INTEGER) {
        // 整数常量: 直接 li
        std::string reg = AllocReg();
        os << "  li " << reg << ", " << value->kind.data.integer.value << "\n";
        return reg;
    }

    // 其他: 从栈帧中加载
    auto it = stack_offsets.find(value);
    if (it != stack_offsets.end()) {
        std::string reg = AllocReg();
        os << "  lw " << reg << ", " << it->second << "(sp)\n";
        return reg;
    }

    // 未找到: 返回空 (理论上不应到达这里)
    return "";
}

// ==================== 栈帧预扫描 ====================

void ASMGenerator::AssignStackOffsets(const koopa_raw_function_t &func) {
    stack_offsets.clear();
    slot_count = 0;
    int offset = 0;

    // 遍历所有基本块和指令
    for (size_t i = 0; i < func->bbs.len; ++i) {
        auto bb = reinterpret_cast<koopa_raw_basic_block_t>(func->bbs.buffer[i]);
        for (size_t j = 0; j < bb->insts.len; ++j) {
            auto inst = reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[j]);
            // 为 alloc 和所有有返回值的指令分配栈槽
            if (inst->kind.tag == KOOPA_RVT_ALLOC || HasReturnValue(inst)) {
                stack_offsets[inst] = offset;
                offset += 4;
                slot_count++;
            }
        }
    }

    // 计算总帧大小: 栈槽 + ra (4 字节), 对齐到 16
    int total = offset + 4;  // +4 for ra
    frame_size = (total + 15) & ~15;
}

// ==================== 序言 / 尾声 ====================

void ASMGenerator::EmitPrologue() {
    os << "  .text\n";
    os << "  .globl " << cur_func << "\n";
    os << cur_func << ":\n";
    os << "  addi sp, sp, -" << frame_size << "\n";
    // 保存 ra
    os << "  sw ra, " << (frame_size - 4) << "(sp)\n";
}

void ASMGenerator::EmitEpilogue() {
    os << ".L" << cur_func << "_exit:\n";
    os << "  lw ra, " << (frame_size - 4) << "(sp)\n";
    os << "  addi sp, sp, " << frame_size << "\n";
    os << "  ret\n";
}

// ==================== Visit 函数 ====================

void ASMGenerator::Generate(const koopa_raw_program_t &program) {
    Visit(program.funcs);
}

void ASMGenerator::Visit(const koopa_raw_program_t &program) {
    Visit(program.funcs);
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
    // 函数名去 @
    cur_func = func->name;
    if (!cur_func.empty() && cur_func[0] == '@')
        cur_func = cur_func.substr(1);

    // 函数体为空（仅有声明）则跳过
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
    // 发射基本块标签
    if (bb->name && bb->name[0] != '\0') {
        std::string label = bb->name;
        if (label[0] == '%') label = label.substr(1);
        os << "." << label << ":\n";
    }

    // 遍历指令
    for (size_t i = 0; i < bb->insts.len; ++i) {
        auto inst = reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[i]);
        Visit(inst);
    }
}

void ASMGenerator::Visit(const koopa_raw_value_t &value) {
    switch (value->kind.tag) {
    case KOOPA_RVT_ALLOC:
        // alloc: 栈空间已在 AssignStackOffsets 中预留, 无需生成代码
        break;

    case KOOPA_RVT_LOAD: {
        // %x = load %y: 从 %y (alloc) 的栈槽读取, 存入 %x 的栈槽
        auto &load = value->kind.data.load;
        int src_offset = stack_offsets[load.src];
        int dst_offset = stack_offsets[value];
        std::string r = AllocReg();
        os << "  lw " << r << ", " << src_offset << "(sp)\n";
        os << "  sw " << r << ", " << dst_offset << "(sp)\n";
        break;
    }

    case KOOPA_RVT_STORE: {
        // store %val, %dest: 将 %val 的值写入 %dest (alloc) 的栈槽
        auto &store = value->kind.data.store;
        std::string val_reg = GetOperand(store.value);
        int dest_offset = stack_offsets[store.dest];
        os << "  sw " << val_reg << ", " << dest_offset << "(sp)\n";
        break;
    }

    case KOOPA_RVT_BINARY: {
        // 二元运算: 加载操作数, 计算结果, 存入当前值的栈槽
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

        // 将结果存入栈槽
        int dst_offset = stack_offsets[value];
        os << "  sw " << dst_reg << ", " << dst_offset << "(sp)\n";
        break;
    }

    case KOOPA_RVT_BRANCH: {
        // br %cond, %true, %false
        auto &br = value->kind.data.branch;
        std::string cond_reg = GetOperand(br.cond);

        // 目标基本块名 (去 % 前缀)
        std::string true_label = br.true_bb->name;
        if (!true_label.empty() && true_label[0] == '%') true_label = true_label.substr(1);

        std::string false_label = br.false_bb->name;
        if (!false_label.empty() && false_label[0] == '%') false_label = false_label.substr(1);

        os << "  bnez " << cond_reg << ", ." << true_label << "\n";
        os << "  j ." << false_label << "\n";
        break;
    }

    case KOOPA_RVT_JUMP: {
        // jump %target
        auto &jump = value->kind.data.jump;
        std::string target_label = jump.target->name;
        if (!target_label.empty() && target_label[0] == '%') target_label = target_label.substr(1);

        os << "  j ." << target_label << "\n";
        break;
    }

    case KOOPA_RVT_RETURN: {
        auto &ret = value->kind.data.ret;
        if (ret.value) {
            if (ret.value->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li a0, " << ret.value->kind.data.integer.value << "\n";
            } else {
                int offset = stack_offsets[ret.value];
                os << "  lw a0, " << offset << "(sp)\n";
            }
        }
        os << "  j .L" << cur_func << "_exit\n";
        break;
    }

    default:
        break;
    }
}
