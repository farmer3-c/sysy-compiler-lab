#pragma once

#include <iostream>
#include <string>
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
    void EmitPrologue();
    void EmitEpilogue();

    // 共享状态
    std::ostream &os;
    int frame_size = 0;                  // 对齐后的栈帧大小
    std::string cur_func;                // 当前函数名（已去 @）
};
