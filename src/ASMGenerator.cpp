#include "ASMGenerator.h"

// ==================== 辅助 ====================

// 分配一个新的临时寄存器 (t0-t6)
std::string ASMGenerator::AllocReg() {
    int idx = (next_reg++)%7;
    return "t" + std::to_string(idx);
}

// 将 Koopa IR 值加载到寄存器中, 返回寄存器名
std::string ASMGenerator::LoadValueToReg(koopa_raw_value_t value) {
    // 如果这个值已经有寄存器映射, 直接返回
    auto it = val_to_reg.find(value);
    if (it != val_to_reg.end()) {
        return it->second;
    }

    // 根据值的类型处理
    switch (value->kind.tag) {
    case KOOPA_RVT_INTEGER: {
        std::string reg = AllocReg();
        os << "  li " << reg << ", " << value->kind.data.integer.value << "\n";
        val_to_reg[value] = reg;
        return reg;
    }
    case KOOPA_RVT_BINARY: {
        // 递归处理: 先生成该二元运算的代码
        auto &bin = value->kind.data.binary;
        std::string lhs_reg = LoadValueToReg(bin.lhs);
        std::string rhs_reg = LoadValueToReg(bin.rhs);
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
            // eq: sub 然后 set if zero
            os << "  sub " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            os << "  sltiu " << dst_reg << ", " << dst_reg << ", 1\n";
            break;
        }
        case KOOPA_RBO_NOT_EQ: {
            // ne: sub 然后 sltu (set if not zero)
            os << "  sub " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            os << "  sltu " << dst_reg << ", x0, " << dst_reg << "\n";
            break;
        }
        case KOOPA_RBO_LT:
            // lt: set less than
            os << "  slt " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_GT:
            // gt: swap operands for slt (rs2 < rs1 → rs1 > rs2)
            os << "  slt " << dst_reg << ", " << rhs_reg << ", " << lhs_reg << "\n";
            break;
        case KOOPA_RBO_LE:
            // le: not (rs2 < rs1), 即 rs1 <= rs2
            os << "  slt " << dst_reg << ", " << rhs_reg << ", " << lhs_reg << "\n";
            os << "  xori " << dst_reg << ", " << dst_reg << ", 1\n";
            break;
        case KOOPA_RBO_GE:
            // ge: not (rs1 < rs2), 即 rs1 >= rs2
            os << "  slt " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            os << "  xori " << dst_reg << ", " << dst_reg << ", 1\n";
            break;
        case KOOPA_RBO_XOR:
            os << "  xor " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_AND:
            os << "  and " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_OR:
            os << "  or " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        default:
            break;
        }

        val_to_reg[value] = dst_reg;
        return dst_reg;
    }
    default:
        // 不支持的类型, 返回空字符串
        return "";
    }
}

void ASMGenerator::EmitPrologue() {
    os << "  .text\n";
    os << "  .globl " << cur_func << "\n";
    os << cur_func << ":\n";
    // 栈帧: 保存 ra(4) + s0(4), 对齐到 16 字节
    frame_size = 16;
    os << "  addi sp, sp, -" << frame_size << "\n";
    os << "  sw ra, " << (frame_size - 4) << "(sp)\n";
    os << "  sw s0, " << (frame_size - 8) << "(sp)\n";
    os << "  addi s0, sp, " << frame_size << "\n";
}

void ASMGenerator::EmitEpilogue() {
    os << ".L" << cur_func << "_exit:\n";
    os << "  lw ra, " << (frame_size - 4) << "(sp)\n";
    os << "  lw s0, " << (frame_size - 8) << "(sp)\n";
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
    case KOOPA_RVT_RETURN: {
        auto &ret = value->kind.data.ret;
        if (ret.value) {
            // 判断返回值类型: 整数常量或指令结果
            if (ret.value->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li a0, " << ret.value->kind.data.integer.value << "\n";
            } else {
                // 指令结果 (例如二元运算): 查找对应的寄存器
                std::string reg = LoadValueToReg(ret.value);
                os << "  mv a0, " << reg << "\n";
            }
        }
        os << "  j .L" << cur_func << "_exit\n";
        break;
    }
    case KOOPA_RVT_BINARY: {
        // 二元运算: 生成代码并记录结果寄存器
        auto &bin = value->kind.data.binary;
        std::string lhs_reg = LoadValueToReg(bin.lhs);
        std::string rhs_reg = LoadValueToReg(bin.rhs);
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
        case KOOPA_RBO_XOR:
            os << "  xor " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_AND:
            os << "  and " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        case KOOPA_RBO_OR:
            os << "  or " << dst_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
            break;
        default:
            break;
        }

        val_to_reg[value] = dst_reg;
        break;
    }
    default:
        break;
    }
}
