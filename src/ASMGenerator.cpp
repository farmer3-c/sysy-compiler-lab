#include "ASMGenerator.h"
#include <vector>

// ==================== 类型大小计算 ====================

// 递归计算 Koopa IR 类型的大小 (RV32I: 指针 = 4 字节)
static int GetTypeSize(koopa_raw_type_t ty) {
  switch (ty->tag) {
    case KOOPA_RTT_INT32:    return 4;
    case KOOPA_RTT_UNIT:     return 0;
    case KOOPA_RTT_POINTER:  return 4;  // RV32I 指针 4 字节
    case KOOPA_RTT_ARRAY: {
      int elem_size = GetTypeSize(ty->data.array.base);
      return elem_size * (int)ty->data.array.len;
    }
    case KOOPA_RTT_FUNCTION: return 0;
    default: return 0;
  }
}

// 递归输出 aggregate 元素
static void EmitAggregate(std::ostream &os, koopa_raw_value_t agg_val) {
  auto &agg = agg_val->kind.data.aggregate;
  for (size_t i = 0; i < agg.elems.len; i++) {
    auto elem = reinterpret_cast<koopa_raw_value_t>(agg.elems.buffer[i]);
    if (elem->kind.tag == KOOPA_RVT_INTEGER) {
      os << "  .word " << elem->kind.data.integer.value << "\n";
    } else if (elem->kind.tag == KOOPA_RVT_AGGREGATE) {
      EmitAggregate(os, elem);
    } else if (elem->kind.tag == KOOPA_RVT_ZERO_INIT) {
      // zeroinit inside aggregate: output zero words
      int size = GetTypeSize(elem->ty);
      int words = size / 4;
      for (int w = 0; w < words; w++)
        os << "  .word 0\n";
    }
  }
}

// ==================== 辅助 ====================

std::string ASMGenerator::AllocReg() {
    int idx = (next_reg++) % 7;
    return "t" + std::to_string(idx);
}

// ==================== 大栈帧辅助 ====================

void ASMGenerator::EmitLoadSp(const std::string &dst, int offset) {
    if (offset >= -2048 && offset <= 2047) {
        os << "  lw " << dst << ", " << offset << "(sp)\n";
    } else {
        std::string addr = AllocReg();
        os << "  li " << addr << ", " << offset << "\n";
        os << "  add " << addr << ", sp, " << addr << "\n";
        os << "  lw " << dst << ", 0(" << addr << ")\n";
    }
}

void ASMGenerator::EmitStoreSp(const std::string &src, int offset) {
    if (offset >= -2048 && offset <= 2047) {
        os << "  sw " << src << ", " << offset << "(sp)\n";
    } else {
        // 将值存入 a7 (固定 scratch 寄存器), 避免 src 被地址计算的 li t5 覆盖
        if (src != "a7") os << "  mv a7, " << src << "\n";
        os << "  li t5, " << offset << "\n";
        os << "  add t5, sp, t5\n";
        os << "  sw a7, 0(t5)\n";
    }
}

void ASMGenerator::EmitSpAdd(const std::string &dst, int offset) {
    if (offset >= -2048 && offset <= 2047) {
        os << "  addi " << dst << ", sp, " << offset << "\n";
    } else {
        std::string off_reg = AllocReg();
        os << "  li " << off_reg << ", " << offset << "\n";
        os << "  add " << dst << ", sp, " << off_reg << "\n";
    }
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
            EmitLoadSp(reg, offset);
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
        EmitLoadSp(reg, it->second);
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
            if (inst->kind.tag == KOOPA_RVT_ALLOC) {
                // alloc: 为整个类型分配空间 (不只是 4 字节指针)
                int size = GetTypeSize(inst->ty->data.pointer.base);
                // 对齐到 4 字节
                size = (size + 3) & ~3;
                stack_offsets[inst] = offset;
                offset += size;
                slot_count++;
            } else if (HasReturnValue(inst)) {
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
        if (frame_size <= 2047) {
            os << "  addi sp, sp, -" << frame_size << "\n";
        } else {
            std::string tmp = AllocReg();
            os << "  li " << tmp << ", " << frame_size << "\n";
            os << "  sub sp, sp, " << tmp << "\n";
        }
    }
    // 非叶子函数保存 ra
    if (!is_leaf) {
        EmitStoreSp("ra", frame_size - 4);
    }
}

void ASMGenerator::EmitEpilogue() {
    os << ".L" << cur_func << "_exit:\n";
    // 非叶子函数恢复 ra
    if (!is_leaf) {
        EmitLoadSp("ra", frame_size - 4);
    }
    if (frame_size > 0) {
        if (frame_size <= 2047) {
            os << "  addi sp, sp, " << frame_size << "\n";
        } else {
            std::string tmp = AllocReg();
            os << "  li " << tmp << ", " << frame_size << "\n";
            os << "  add sp, sp, " << tmp << "\n";
        }
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
                int size = GetTypeSize(val->ty->data.pointer.base);
                os << "  .zero " << size << "\n";
            } else if (init->kind.tag == KOOPA_RVT_INTEGER) {
                os << "  .word " << init->kind.data.integer.value << "\n";
            } else if (init->kind.tag == KOOPA_RVT_AGGREGATE) {
                EmitAggregate(os, init);
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
        } else if (load.src->kind.tag == KOOPA_RVT_ALLOC) {
            // 局部 alloc: lw 直接从栈上加载
            int src_offset = stack_offsets[load.src];
            EmitLoadSp(r, src_offset);
        } else {
            // getelemptr/getptr/其他: src 的值是一个指针
            // 先加载指针值, 再通过指针加载数据
            int src_offset = stack_offsets[load.src];
            std::string ptr_reg = AllocReg();
            EmitLoadSp(ptr_reg, src_offset);
            os << "  lw " << r << ", 0(" << ptr_reg << ")\n";
        }
        EmitStoreSp(r, dst_offset);
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
        } else if (store.dest->kind.tag == KOOPA_RVT_ALLOC) {
            // 局部 alloc: sw 直接存到栈上
            std::string val_reg = GetOperand(store.value);
            int dest_offset = stack_offsets[store.dest];
            EmitStoreSp(val_reg, dest_offset);
        } else {
            // getelemptr/getptr/其他: dest 的值是一个指针
            // 先加载指针值, 再通过指针存储
            std::string val_reg = GetOperand(store.value);
            int dest_offset = stack_offsets[store.dest];
            std::string ptr_reg = AllocReg();
            EmitLoadSp(ptr_reg, dest_offset);
            os << "  sw " << val_reg << ", 0(" << ptr_reg << ")\n";
        }
        break;
    }

    case KOOPA_RVT_GET_ELEM_PTR: {
        auto &gep = value->kind.data.get_elem_ptr;

        // 获取源地址: 数组指针
        std::string src_reg;
        if (gep.src->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
            std::string name = gep.src->name;
            if (!name.empty() && name[0] == '@') name = name.substr(1);
            src_reg = AllocReg();
            os << "  la " << src_reg << ", " << name << "\n";
        } else if (gep.src->kind.tag == KOOPA_RVT_ALLOC) {
            // alloc 指令: 从栈上取地址
            auto it = stack_offsets.find(gep.src);
            if (it != stack_offsets.end()) {
                src_reg = AllocReg();
                EmitSpAdd(src_reg, it->second);
            }
        } else {
            // 其他情况: 从栈上加载地址值
            auto it = stack_offsets.find(gep.src);
            if (it != stack_offsets.end()) {
                src_reg = AllocReg();
                EmitLoadSp(src_reg, it->second);
            }
        }

        // 获取索引值
        std::string idx_reg = GetOperand(gep.index);

        // 计算元素大小: src 是 *[T, N] 数组指针
        int elem_size = 0;
        if (gep.src->ty->tag == KOOPA_RTT_POINTER &&
            gep.src->ty->data.pointer.base->tag == KOOPA_RTT_ARRAY) {
            elem_size = GetTypeSize(gep.src->ty->data.pointer.base->data.array.base);
        }

        // 计算偏移量: index * elem_size
        std::string offset_reg = AllocReg();
        if (elem_size == 1) {
            os << "  mv " << offset_reg << ", " << idx_reg << "\n";
        } else if (elem_size == 2) {
            os << "  slli " << offset_reg << ", " << idx_reg << ", 1\n";
        } else if (elem_size == 4) {
            os << "  slli " << offset_reg << ", " << idx_reg << ", 2\n";
        } else if (elem_size == 8) {
            os << "  slli " << offset_reg << ", " << idx_reg << ", 3\n";
        } else {
            std::string size_reg = AllocReg();
            os << "  li " << size_reg << ", " << elem_size << "\n";
            os << "  mul " << offset_reg << ", " << idx_reg << ", " << size_reg << "\n";
        }

        // 计算目标地址: src + offset
        std::string result_reg = AllocReg();
        os << "  add " << result_reg << ", " << src_reg << ", " << offset_reg << "\n";

        // 保存结果到栈
        int dst_offset = stack_offsets[value];
        EmitStoreSp(result_reg, dst_offset);
        break;
    }

    case KOOPA_RVT_GET_PTR: {
        auto &gp = value->kind.data.get_ptr;

        // 获取源地址 (指针)
        std::string src_reg;
        if (gp.src->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
            std::string name = gp.src->name;
            if (!name.empty() && name[0] == '@') name = name.substr(1);
            src_reg = AllocReg();
            os << "  la " << src_reg << ", " << name << "\n";
        } else if (gp.src->kind.tag == KOOPA_RVT_ALLOC) {
            auto it = stack_offsets.find(gp.src);
            if (it != stack_offsets.end()) {
                src_reg = AllocReg();
                EmitSpAdd(src_reg, it->second);
            }
        } else {
            auto it = stack_offsets.find(gp.src);
            if (it != stack_offsets.end()) {
                src_reg = AllocReg();
                EmitLoadSp(src_reg, it->second);
            }
        }

        // 获取索引值
        std::string idx_reg = GetOperand(gp.index);

        // 计算元素大小: src 是 *T 指针, 取 base 作为元素
        int elem_size = 0;
        if (gp.src->ty->tag == KOOPA_RTT_POINTER) {
            elem_size = GetTypeSize(gp.src->ty->data.pointer.base);
        }

        // 计算偏移量: index * elem_size
        std::string offset_reg = AllocReg();
        if (elem_size == 1) {
            os << "  mv " << offset_reg << ", " << idx_reg << "\n";
        } else if (elem_size == 2) {
            os << "  slli " << offset_reg << ", " << idx_reg << ", 1\n";
        } else if (elem_size == 4) {
            os << "  slli " << offset_reg << ", " << idx_reg << ", 2\n";
        } else if (elem_size == 8) {
            os << "  slli " << offset_reg << ", " << idx_reg << ", 3\n";
        } else {
            std::string size_reg = AllocReg();
            os << "  li " << size_reg << ", " << elem_size << "\n";
            os << "  mul " << offset_reg << ", " << idx_reg << ", " << size_reg << "\n";
        }

        // 计算目标地址: src + offset
        std::string result_reg = AllocReg();
        os << "  add " << result_reg << ", " << src_reg << ", " << offset_reg << "\n";

        // 保存结果到栈
        int dst_offset = stack_offsets[value];
        EmitStoreSp(result_reg, dst_offset);
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
        EmitStoreSp(dst_reg, dst_offset);
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
        int extra_args = num_args > 8 ? num_args - 8 : 0;

        // 1. 前 8 个参数: 加载后立即 mv 到 a0-a7 (sp 未被修改, 偏移正确)
        for (int i = 0; i < num_args && i < 8; ++i) {
            auto arg = reinterpret_cast<koopa_raw_value_t>(call.args.buffer[i]);
            std::string arg_reg = GetOperand(arg);
            os << "  mv a" << i << ", " << arg_reg << "\n";
        }

        // 2. 超过 8 个的参数: 先加载值 (sp 未被修改), 再调整 sp, 再存入栈
        std::vector<std::string> extra_regs;
        for (int i = 8; i < num_args; ++i) {
            auto arg = reinterpret_cast<koopa_raw_value_t>(call.args.buffer[i]);
            extra_regs.push_back(GetOperand(arg));
        }
        if (extra_args > 0) {
            os << "  addi sp, sp, -" << (extra_args * 4) << "\n";
            for (int i = 0; i < extra_args; ++i) {
                os << "  sw " << extra_regs[i] << ", " << (i * 4) << "(sp)\n";
            }
        }

        // 3. 调用
        std::string func_name = call.callee->name;
        if (!func_name.empty() && func_name[0] == '@') func_name = func_name.substr(1);
        os << "  call " << func_name << "\n";

        // 4. 恢复 sp
        if (extra_args > 0) {
            os << "  addi sp, sp, " << (extra_args * 4) << "\n";
        }

        // 保存返回值 (如果有)
        if (HasReturnValue(value)) {
            int dst_offset = stack_offsets[value];
            EmitStoreSp("a0", dst_offset);
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
                EmitLoadSp("a0", offset);
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
