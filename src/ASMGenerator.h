#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include "koopa.h"

class ASMGenerator {
public:
    ASMGenerator(std::ostream &os) : os(os) {}
    void Generate(const koopa_raw_program_t &program);

private:
    // DFS 遍历族
    void Visit(const koopa_raw_program_t &program);
    void Visit(const koopa_raw_slice_t &slice);
    void Visit(const koopa_raw_function_t &func);
    void Visit(const koopa_raw_basic_block_t &bb);
    void Visit(const koopa_raw_value_t &value);

    // 辅助
    int AllocStackSlot();                        // 分配栈槽
    int GetOffset(const koopa_raw_value_t &v);   // 查值→栈偏移
    std::string NewLabel();                      // 生成唯一标签
    void EmitPrologue(const std::string &name);
    void EmitEpilogue(const std::string &name);

    // 共享状态
    std::ostream &os;
    int stack_frame_size = 0;                    // 当前函数栈帧 (raw, 不含 ra/s0)
    int frame_size = 0;                          // 对齐后的总栈帧大小
    int label_count = 0;                         // 全局标签计数器
    std::string cur_func;                        // 当前函数名
    const koopa_raw_function_data_t *cur_func_ptr = nullptr;   // 当前函数指针(解 FUNC_ARG_REF)
    const koopa_raw_basic_block_data_t *cur_bb_ptr = nullptr;  // 当前基本块指针(解 BLOCK_ARG_REF)
    std::unordered_map<const void*, int> val_offset;  // 值→栈偏移
    std::unordered_map<const void*, int> alloc_mem;   // alloc 指令→它指向的内存偏移
    std::unordered_map<const void*, std::string> bb_label;   // BB→汇编标签
};
