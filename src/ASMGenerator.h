#pragma once

#include <iostream>
#include <string>
#include <map>
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

    // 寄存器分配辅助
    std::string AllocReg();
    std::string LoadValueToReg(koopa_raw_value_t value);

    // 共享状态
    std::ostream &os;
    int frame_size = 0;                  // 对齐后的栈帧大小
    std::string cur_func;                // 当前函数名（已去 @）

    // 寄存器追踪: 每个 IR value 分配到的寄存器名
    std::map<koopa_raw_value_t, std::string> val_to_reg;
    int next_reg = 0;                    // 下一个可用临时寄存器编号
};
