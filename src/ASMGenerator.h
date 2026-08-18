// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 farmer3-c
#pragma once

#include <iostream>
#include <string>
#include <map>
#include <unordered_map>
#include <set>
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

    // 栈帧: 预扫描分配偏移
    void AssignStackOffsets(const koopa_raw_function_t &func);

    // 辅助
    void EmitPrologue();
    void EmitEpilogue();

    // 获取操作数: 将 IR 值加载到寄存器
    std::string GetOperand(koopa_raw_value_t value);
    // 分配临时寄存器
    std::string AllocReg();

    // 大栈帧辅助: 处理 offset > 2047 的 sp 相对访问
    void EmitLoadSp(const std::string &dst, int offset);
    void EmitStoreSp(const std::string &src, int offset);
    void EmitSpAdd(const std::string &dst, int offset);

    // 共享状态
    std::ostream &os;
    int frame_size = 0;                  // 对齐后的栈帧大小
    std::string cur_func;                // 当前函数名（已去 @）
    bool is_leaf = true;                 // 叶子函数 (不含 call 指令)

    // 值 → 栈偏移映射 (偏移从 sp 起算)
    std::unordered_map<koopa_raw_value_t, int> stack_offsets;
    int slot_count = 0;                  // 总栈槽数

    // 临时寄存器池
    int next_reg = 0;
};
