#include "ASMGenerator.h"
#include <cassert>

int ASMGenerator::AllocStackSlot() {
    int offset = stack_frame_size;
    stack_frame_size += 4;
    return offset;
}

int ASMGenerator::GetOffset(const koopa_raw_value_t &v) {
    // 解析 FUNC_ARG_REF → 函数参数的实际偏移
    if (v->kind.tag == KOOPA_RVT_FUNC_ARG_REF) {
        size_t idx = v->kind.data.func_arg_ref.index;
        auto param = reinterpret_cast<koopa_raw_value_t>(
            cur_func_ptr->params.buffer[idx]);
        auto it = val_offset.find(param);
        assert(it != val_offset.end());
        return it->second;
    }
    // 解析 BLOCK_ARG_REF → BB 参数的实际偏移
    if (v->kind.tag == KOOPA_RVT_BLOCK_ARG_REF) {
        size_t idx = v->kind.data.block_arg_ref.index;
        auto param = reinterpret_cast<koopa_raw_value_t>(
            cur_bb_ptr->params.buffer[idx]);
        auto it = val_offset.find(param);
        assert(it != val_offset.end());
        return it->second;
    }
    auto it = val_offset.find(v);
    assert(it != val_offset.end());
    return it->second;
}

std::string ASMGenerator::NewLabel() {
    return ".L" + std::to_string(label_count++);
}

void ASMGenerator::EmitPrologue(const std::string &name) {
    // 栈帧布局: ra(4) + s0(4) + stack_frame_size, 对齐到 16 字节
    int raw = 8 + stack_frame_size;
    frame_size = (raw + 15) & ~15;

    os << "  .text\n";
    os << "  .globl " << name << "\n";
    os << name << ":\n";
    os << "  addi sp, sp, " << -frame_size << "\n";
    os << "  sw ra, " << (frame_size - 4) << "(sp)\n";
    os << "  sw s0, " << (frame_size - 8) << "(sp)\n";
    os << "  addi s0, sp, " << frame_size << "\n";
}

void ASMGenerator::EmitEpilogue(const std::string &name) {
    os << ".L" << name << "_exit:\n";
    os << "  lw ra, " << (frame_size - 4) << "(sp)\n";
    os << "  lw s0, " << (frame_size - 8) << "(sp)\n";
    os << "  addi sp, sp, " << frame_size << "\n";
    os << "  ret\n";
}

// ==================== Visit 函数 ====================

void ASMGenerator::Generate(const koopa_raw_program_t &program) {
    Visit(program.values);
    Visit(program.funcs);
}

void ASMGenerator::Visit(const koopa_raw_program_t &program) {
    Visit(program.values);
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
    cur_func = func->name;
    if (!cur_func.empty() && cur_func[0] == '@') cur_func = cur_func.substr(1);
    cur_func_ptr = func;
    stack_frame_size = 0;
    frame_size = 0;
    val_offset.clear();
    alloc_mem.clear();
    bb_label.clear();

    // ===== Pre-scan: 分配栈槽 =====

    // 1) 函数参数
    for (size_t i = 0; i < func->params.len; ++i) {
        auto param = reinterpret_cast<koopa_raw_value_t>(
            func->params.buffer[i]);
        val_offset[param] = AllocStackSlot();
    }

    // 2) 遍历基本块
    for (size_t i = 0; i < func->bbs.len; ++i) {
        auto bb = reinterpret_cast<koopa_raw_basic_block_t>(
            func->bbs.buffer[i]);

        // BB 标签: 优先用 BB 名 (去 %), 然后自动生成
        std::string label;
        if (bb->name && bb->name[0] != '\0') {
            label = bb->name;
            if (label[0] == '%') label = label.substr(1);
        }
        if (label.empty()) label = NewLabel();
        bb_label[bb] = "." + cur_func + "_" + label;

        // BB 参数
        for (size_t j = 0; j < bb->params.len; ++j) {
            auto param = reinterpret_cast<koopa_raw_value_t>(
                bb->params.buffer[j]);
            val_offset[param] = AllocStackSlot();
        }

        // 指令: 被引用者分配栈槽
        for (size_t j = 0; j < bb->insts.len; ++j) {
            auto inst = reinterpret_cast<koopa_raw_value_t>(
                bb->insts.buffer[j]);

            // 如果此指令产出的值被其他指令使用，分配栈槽
            if (inst->used_by.len > 0) {
                val_offset[inst] = AllocStackSlot();
            }

            // alloc 指令额外需要一块指向的内存
            if (inst->kind.tag == KOOPA_RVT_ALLOC) {
                alloc_mem[inst] = AllocStackSlot();
            }
        }
    }

    // ===== 发射函数序言 =====
    EmitPrologue(cur_func);

    // ===== 保存函数参数到栈槽 (a0-a7) =====
    for (size_t i = 0; i < func->params.len && i < 8; ++i) {
        auto param = reinterpret_cast<koopa_raw_value_t>(
            func->params.buffer[i]);
        os << "  sw a" << i << ", " << GetOffset(param) << "(sp)\n";
    }

    // ===== 遍历基本块 =====
    for (size_t i = 0; i < func->bbs.len; ++i) {
        auto bb = reinterpret_cast<koopa_raw_basic_block_t>(
            func->bbs.buffer[i]);
        Visit(bb);
    }

    // ===== 发射函数尾声 (return 会 j 过来) =====
    EmitEpilogue(cur_func);
}

void ASMGenerator::Visit(const koopa_raw_basic_block_t &bb) {
    cur_bb_ptr = bb;

    // 发射标签
    os << bb_label[bb] << ":\n";

    // 遍历指令
    for (size_t i = 0; i < bb->insts.len; ++i) {
        auto inst = reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[i]);
        Visit(inst);
    }
}

void ASMGenerator::Visit(const koopa_raw_value_t &value) {
    // ---------- 全局上下文: program.values (全局变量) ----------
    if (cur_func.empty()) {
        if (value->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
            std::string gname = value->name ? value->name : "global";
            if (!gname.empty() && gname[0] == '@') gname = gname.substr(1);

            auto init = value->kind.data.global_alloc.init;
            int32_t init_val = 0;
            if (init && init->kind.tag == KOOPA_RVT_INTEGER) {
                init_val = init->kind.data.integer.value;
            }

            os << "  .data\n";
            os << "  .globl " << gname << "\n";
            os << gname << ":\n";
            os << "  .word " << init_val << "\n";
        }
        return;
    }

    // ---------- 局部上下文: 函数内的指令 ----------
    switch (value->kind.tag) {

    // -----------------------------------------------
    // 常量
    // -----------------------------------------------
    case KOOPA_RVT_INTEGER: {
        // 仅在需要栈槽时才存储 (如该常量被命名且被多处引用)
        if (val_offset.find(value) != val_offset.end()) {
            os << "  li t0, " << value->kind.data.integer.value << "\n";
            os << "  sw t0, " << GetOffset(value) << "(sp)\n";
        }
        break;
    }
    case KOOPA_RVT_ZERO_INIT: {
        if (val_offset.find(value) != val_offset.end()) {
            os << "  sw zero, " << GetOffset(value) << "(sp)\n";
        }
        break;
    }
    case KOOPA_RVT_UNDEF: {
        // 未定义值: 不需要代码
        break;
    }
    case KOOPA_RVT_AGGREGATE: {
        auto &agg = value->kind.data.aggregate;
        for (size_t i = 0; i < agg.elems.len; ++i) {
            auto elem = reinterpret_cast<koopa_raw_value_t>(
                agg.elems.buffer[i]);
            if (elem->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li t0, " << elem->kind.data.integer.value << "\n";
            } else if (elem->kind.tag == KOOPA_RVT_ZERO_INIT) {
                os << "  li t0, 0\n";
            } else {
                os << "  lw t0, " << GetOffset(elem) << "(sp)\n";
            }
            os << "  sw t0, " << (GetOffset(value) + (int)i * 4)
               << "(sp)\n";
        }
        break;
    }

    // -----------------------------------------------
    // 引用
    // -----------------------------------------------
    case KOOPA_RVT_FUNC_ARG_REF:
    case KOOPA_RVT_BLOCK_ARG_REF:
        // 仅为引用，实际值已在参数栈槽中，不需要额外代码
        break;

    // -----------------------------------------------
    // 内存分配
    // -----------------------------------------------
    case KOOPA_RVT_ALLOC: {
        // alloc 指令的值是一个指针，指向已分配的栈内存
        os << "  addi t0, sp, " << alloc_mem[value] << "\n";
        os << "  sw t0, " << GetOffset(value) << "(sp)\n";
        break;
    }
    case KOOPA_RVT_GLOBAL_ALLOC: {
        std::string gname = value->name ? value->name : "global";
        if (!gname.empty() && gname[0] == '@') gname = gname.substr(1);
        os << "  la t0, " << gname << "\n";
        os << "  sw t0, " << GetOffset(value) << "(sp)\n";
        break;
    }

    // -----------------------------------------------
    // 内存读写
    // -----------------------------------------------
    case KOOPA_RVT_LOAD: {
        auto src = value->kind.data.load.src;
        // 加载指针
        os << "  lw t0, " << GetOffset(src) << "(sp)\n";
        // 从指针指向的地址加载值
        os << "  lw t0, 0(t0)\n";
        // 存结果
        os << "  sw t0, " << GetOffset(value) << "(sp)\n";
        break;
    }
    case KOOPA_RVT_STORE: {
        auto dest = value->kind.data.store.dest;
        auto val   = value->kind.data.store.value;
        // 加载目标指针
        os << "  lw t0, " << GetOffset(dest) << "(sp)\n";
        // 加载要存储的值
        if (val->kind.tag == KOOPA_RVT_INTEGER) {
            os << "  li t1, " << val->kind.data.integer.value << "\n";
        } else if (val->kind.tag == KOOPA_RVT_ZERO_INIT) {
            os << "  li t1, 0\n";
        } else {
            os << "  lw t1, " << GetOffset(val) << "(sp)\n";
        }
        os << "  sw t1, 0(t0)\n";
        break;
    }

    // -----------------------------------------------
    // 指针计算
    // -----------------------------------------------
    case KOOPA_RVT_GET_PTR:
    case KOOPA_RVT_GET_ELEM_PTR: {
        koopa_raw_value_t src;
        koopa_raw_value_t index;
        if (value->kind.tag == KOOPA_RVT_GET_PTR) {
            src   = value->kind.data.get_ptr.src;
            index = value->kind.data.get_ptr.index;
        } else {
            src   = value->kind.data.get_elem_ptr.src;
            index = value->kind.data.get_elem_ptr.index;
        }
        // 加载基址
        os << "  lw t0, " << GetOffset(src) << "(sp)\n";
        // 加载索引
        if (index->kind.tag == KOOPA_RVT_INTEGER) {
            os << "  li t1, " << index->kind.data.integer.value << "\n";
        } else {
            os << "  lw t1, " << GetOffset(index) << "(sp)\n";
        }
        // 索引 × 4 (i32)
        os << "  slli t1, t1, 2\n";
        os << "  add t0, t0, t1\n";
        os << "  sw t0, " << GetOffset(value) << "(sp)\n";
        break;
    }

    // -----------------------------------------------
    // 二元运算
    // -----------------------------------------------
    case KOOPA_RVT_BINARY: {
        auto &bin = value->kind.data.binary;
        // 加载左操作数
        if (bin.lhs->kind.tag == KOOPA_RVT_INTEGER) {
            os << "  li t0, " << bin.lhs->kind.data.integer.value << "\n";
        } else if (bin.lhs->kind.tag == KOOPA_RVT_ZERO_INIT) {
            os << "  li t0, 0\n";
        } else {
            os << "  lw t0, " << GetOffset(bin.lhs) << "(sp)\n";
        }
        // 加载右操作数
        if (bin.rhs->kind.tag == KOOPA_RVT_INTEGER) {
            os << "  li t1, " << bin.rhs->kind.data.integer.value << "\n";
        } else if (bin.rhs->kind.tag == KOOPA_RVT_ZERO_INIT) {
            os << "  li t1, 0\n";
        } else {
            os << "  lw t1, " << GetOffset(bin.rhs) << "(sp)\n";
        }

        switch (bin.op) {
            // 算术
            case KOOPA_RBO_ADD: os << "  add t0, t0, t1\n"; break;
            case KOOPA_RBO_SUB: os << "  sub t0, t0, t1\n"; break;
            case KOOPA_RBO_MUL: os << "  mul t0, t0, t1\n"; break;
            case KOOPA_RBO_DIV: os << "  div t0, t0, t1\n"; break;
            case KOOPA_RBO_MOD: os << "  rem t0, t0, t1\n"; break;
            // 位运算
            case KOOPA_RBO_AND: os << "  and t0, t0, t1\n"; break;
            case KOOPA_RBO_OR:  os << "  or  t0, t0, t1\n"; break;
            case KOOPA_RBO_XOR: os << "  xor t0, t0, t1\n"; break;
            case KOOPA_RBO_SHL: os << "  sll t0, t0, t1\n"; break;
            case KOOPA_RBO_SHR: os << "  srl t0, t0, t1\n"; break;
            case KOOPA_RBO_SAR: os << "  sra t0, t0, t1\n"; break;
            // 比较
            case KOOPA_RBO_EQ:
                os << "  sub  t0, t0, t1\n";
                os << "  seqz t0, t0\n";
                break;
            case KOOPA_RBO_NOT_EQ:
                os << "  sub  t0, t0, t1\n";
                os << "  snez t0, t0\n";
                break;
            case KOOPA_RBO_LT:
                os << "  slt t0, t0, t1\n";
                break;
            case KOOPA_RBO_LE:
                // sle = ~(t0 > t1)  →  sgt; xori
                os << "  sgt  t0, t0, t1\n";
                os << "  xori t0, t0, 1\n";
                break;
            case KOOPA_RBO_GT:
                os << "  sgt t0, t0, t1\n";
                break;
            case KOOPA_RBO_GE:
                // sge = ~(t0 < t1)  →  slt; xori
                os << "  slt  t0, t0, t1\n";
                os << "  xori t0, t0, 1\n";
                break;
        }
        os << "  sw t0, " << GetOffset(value) << "(sp)\n";
        break;
    }

    // -----------------------------------------------
    // 函数调用
    // -----------------------------------------------
    case KOOPA_RVT_CALL: {
        auto &call = value->kind.data.call;
        for (size_t i = 0; i < call.args.len && i < 8; ++i) {
            auto arg = reinterpret_cast<koopa_raw_value_t>(
                call.args.buffer[i]);
            if (arg->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li a" << i << ", "
                   << arg->kind.data.integer.value << "\n";
            } else if (arg->kind.tag == KOOPA_RVT_ZERO_INIT) {
                os << "  li a" << i << ", 0\n";
            } else {
                os << "  lw a" << i << ", "
                   << GetOffset(arg) << "(sp)\n";
            }
        }
        std::string callee_name = call.callee->name;
        if (!callee_name.empty() && callee_name[0] == '@')
            callee_name = callee_name.substr(1);
        os << "  call " << callee_name << "\n";
        if (value->used_by.len > 0) {
            os << "  sw a0, " << GetOffset(value) << "(sp)\n";
        }
        break;
    }

    // -----------------------------------------------
    // 条件分支
    // -----------------------------------------------
    case KOOPA_RVT_BRANCH: {
        auto &br = value->kind.data.branch;
        // 加载条件
        if (br.cond->kind.tag == KOOPA_RVT_INTEGER) {
            os << "  li t0, " << br.cond->kind.data.integer.value << "\n";
        } else {
            os << "  lw t0, " << GetOffset(br.cond) << "(sp)\n";
        }
        // 条件为真时跳转
        std::string true_label = ".L" + cur_func + "_branch_"
                                 + std::to_string(label_count++);
        os << "  bnez t0, " << true_label << "\n";

        // === 假路径 (条件为 0, 顺序执行) ===
        for (size_t i = 0; i < br.false_args.len; ++i) {
            auto arg   = reinterpret_cast<koopa_raw_value_t>(
                br.false_args.buffer[i]);
            auto param = reinterpret_cast<koopa_raw_value_t>(
                br.false_bb->params.buffer[i]);
            if (arg->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li t1, " << arg->kind.data.integer.value << "\n";
            } else {
                os << "  lw t1, " << GetOffset(arg) << "(sp)\n";
            }
            os << "  sw t1, " << GetOffset(param) << "(sp)\n";
        }
        os << "  j " << bb_label[br.false_bb] << "\n";

        // === 真路径 ===
        os << true_label << ":\n";
        for (size_t i = 0; i < br.true_args.len; ++i) {
            auto arg   = reinterpret_cast<koopa_raw_value_t>(
                br.true_args.buffer[i]);
            auto param = reinterpret_cast<koopa_raw_value_t>(
                br.true_bb->params.buffer[i]);
            if (arg->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li t1, " << arg->kind.data.integer.value << "\n";
            } else {
                os << "  lw t1, " << GetOffset(arg) << "(sp)\n";
            }
            os << "  sw t1, " << GetOffset(param) << "(sp)\n";
        }
        os << "  j " << bb_label[br.true_bb] << "\n";
        break;
    }

    // -----------------------------------------------
    // 无条件跳转
    // -----------------------------------------------
    case KOOPA_RVT_JUMP: {
        auto &jmp = value->kind.data.jump;
        for (size_t i = 0; i < jmp.args.len; ++i) {
            auto arg   = reinterpret_cast<koopa_raw_value_t>(
                jmp.args.buffer[i]);
            auto param = reinterpret_cast<koopa_raw_value_t>(
                jmp.target->params.buffer[i]);
            if (arg->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li t1, " << arg->kind.data.integer.value << "\n";
            } else {
                os << "  lw t1, " << GetOffset(arg) << "(sp)\n";
            }
            os << "  sw t1, " << GetOffset(param) << "(sp)\n";
        }
        os << "  j " << bb_label[jmp.target] << "\n";
        break;
    }

    // -----------------------------------------------
    // 返回
    // -----------------------------------------------
    case KOOPA_RVT_RETURN: {
        auto &ret = value->kind.data.ret;
        if (ret.value) {
            if (ret.value->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  li a0, "
                   << ret.value->kind.data.integer.value << "\n";
            } else if (ret.value->kind.tag == KOOPA_RVT_ZERO_INIT) {
                os << "  li a0, 0\n";
            } else {
                os << "  lw a0, " << GetOffset(ret.value) << "(sp)\n";
            }
        }
        os << "  j .L" << cur_func << "_exit\n";
        break;
    }

    default:
        break;
    }
}
