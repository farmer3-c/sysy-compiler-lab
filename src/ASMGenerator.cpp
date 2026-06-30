#include "ASMGenerator.h"

// ==================== 辅助 ====================

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
            // return 的值是一个整数常量
            if (ret.value->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li a0, " << ret.value->kind.data.integer.value << "\n";
            }
        }
        os << "  j .L" << cur_func << "_exit\n";
        break;
    }
    default:
        break;
    }
}
