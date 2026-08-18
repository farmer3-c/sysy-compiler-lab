// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 farmer3-c
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <iostream>

// Value 是所有指令和常量的基类
class Value {
 public:
  virtual ~Value() = default;
  virtual void Dump() const = 0;
};

// 整数常量
class Integer : public Value {
 public:
  int value;

  Integer(int v) : value(v) {}

  void Dump() const override {
    std::cout << value;
  }
};

// 零初始化器 (用于全局变量 zeroinit)
class ZeroInit : public Value {
 public:
  void Dump() const override {
    std::cout << "zeroinit";
  }
};

// 寄存器引用, 用于引用前面的指令结果
// 例如 %0, %1, ...
class RegRef : public Value {
 public:
  int reg_id;

  RegRef(int id) : reg_id(id) {}

  void Dump() const override {
    std::cout << "%" << reg_id;
  }
};

// alloc 指令引用, 用于 load/store 中引用 alloc 分配的变量
// 也用于引用函数参数
// 例如 @x, @y
class AllocRef : public Value {
 public:
  std::string name;  // 带 @ 前缀, 如 "@x"

  AllocRef(const std::string &n) : name(n) {}

  void Dump() const override {
    std::cout << name;
  }
};

// 二元运算指令
// 例如 %0 = sub 0, 5
class BinaryInst : public Value {
 public:
  int dest;                          // 目标寄存器编号
  std::string op;                    // 操作符: "sub", "eq", "xor" 等
  std::unique_ptr<Value> lhs;        // 左操作数
  std::unique_ptr<Value> rhs;        // 右操作数

  BinaryInst(int d, const std::string &o,
             std::unique_ptr<Value> l, std::unique_ptr<Value> r)
    : dest(d), op(o), lhs(std::move(l)), rhs(std::move(r)) {}

  void Dump() const override {
    std::cout << "%" << dest << " = " << op << " ";
    lhs->Dump();
    std::cout << ", ";
    rhs->Dump();
  }
};

// getelemptr 指令: %dest = getelemptr src, index
// src 必须是数组指针 (如 *[i32, 3]), 结果是元素指针 (如 *i32)
class GetElemPtrInst : public Value {
 public:
  int dest;
  std::string src;  // 如 "@arr" 或 "%ptr"
  std::unique_ptr<Value> index;

  GetElemPtrInst(int d, const std::string &s, std::unique_ptr<Value> idx)
    : dest(d), src(s), index(std::move(idx)) {}

  void Dump() const override {
    std::cout << "%" << dest << " = getelemptr " << src << ", ";
    index->Dump();
  }
};

// getptr 指令: %dest = getptr src, index
// src 是指针 (如 *i32), 结果是偏移后的同类型指针
class GetPtrInst : public Value {
 public:
  int dest;
  std::string src;  // 如 "%ptr" 或 "@arr"
  std::unique_ptr<Value> index;

  GetPtrInst(int d, const std::string &s, std::unique_ptr<Value> idx)
    : dest(d), src(s), index(std::move(idx)) {}

  void Dump() const override {
    std::cout << "%" << dest << " = getptr " << src << ", ";
    index->Dump();
  }
};

// 聚合常量: {elem1, elem2, ...}
// 用于全局数组初始化
class Aggregate : public Value {
 public:
  std::vector<std::unique_ptr<Value>> elements;

  Aggregate() = default;
  Aggregate(std::vector<std::unique_ptr<Value>> elems)
    : elements(std::move(elems)) {}

  void Dump() const override {
    std::cout << "{";
    for (size_t i = 0; i < elements.size(); ++i) {
      if (i > 0) std::cout << ", ";
      elements[i]->Dump();
    }
    std::cout << "}";
  }
};

// alloc 指令: @x = alloc i32 或 @arr = alloc [i32, 3]
class AllocInst : public Value {
 public:
  std::string name;   // 带 @ 前缀, 如 "@x"
  std::string type;   // "i32", "[i32, 3]", "[[i32, 3], 2]", 等

  AllocInst(const std::string &n, const std::string &t = "i32") : name(n), type(t) {}

  void Dump() const override {
    std::cout << name << " = alloc " << type;
  }
};

// load 指令: %dest = load @src
class LoadInst : public Value {
 public:
  int dest;
  std::string src;  // 带 @ 前缀, 如 "@x"

  LoadInst(int d, const std::string &s) : dest(d), src(s) {}

  void Dump() const override {
    std::cout << "%" << dest << " = load " << src;
  }
};

// store 指令: store value, dest
class StoreInst : public Value {
 public:
  std::unique_ptr<Value> value;
  std::string dest;  // 带 @ 前缀, 如 "@x"

  StoreInst(std::unique_ptr<Value> v, const std::string &d)
    : value(std::move(v)), dest(d) {}

  void Dump() const override {
    std::cout << "store ";
    value->Dump();
    std::cout << ", " << dest;
  }
};

// 条件分支: br cond, %true_bb, %false_bb
class BranchInst : public Value {
 public:
  std::unique_ptr<Value> cond;
  std::string true_bb;
  std::string false_bb;

  BranchInst(std::unique_ptr<Value> c, const std::string &t, const std::string &f)
    : cond(std::move(c)), true_bb(t), false_bb(f) {}

  void Dump() const override {
    std::cout << "br ";
    cond->Dump();
    std::cout << ", %" << true_bb << ", %" << false_bb;
  }
};

// 无条件跳转: jump %target
class JumpInst : public Value {
 public:
  std::string target;

  JumpInst(const std::string &t) : target(t) {}

  void Dump() const override {
    std::cout << "jump %" << target;
  }
};

// 返回指令
class Return : public Value {
 public:
  std::unique_ptr<Value> value;  // nullptr 表示 void 返回

  Return(std::unique_ptr<Value> v) : value(std::move(v)) {}

  void Dump() const override {
    std::cout << "ret";
    if (value) {
      std::cout << " ";
      value->Dump();
    }
  }
};

// ==================== Lv8 新增: 全局内存分配 ====================
// global @var = alloc i32, zeroinit
// global @var = alloc i32, 42
// global @arr = alloc [i32, 3], {1, 2, 3}
class GlobalAllocInst : public Value {
 public:
  std::string name;  // 带 @ 前缀, 如 "@var"
  std::string type;  // "i32", "[i32, 3]", "[[i32, 3], 2]", 等
  std::unique_ptr<Value> init_val;

  GlobalAllocInst(const std::string &n, const std::string &t, std::unique_ptr<Value> init)
    : name(n), type(t), init_val(std::move(init)) {}

  void Dump() const override {
    std::cout << "global " << name << " = alloc " << type << ", ";
    init_val->Dump();
  }
};

// ==================== Lv8 新增: 函数调用 ====================
// %dest = call @func(args)   — 有返回值
// call @func(args)           — void 调用
class CallInst : public Value {
 public:
  int dest;  // -1 表示 void 调用 (无返回值)
  std::string func_name;  // 带 @ 前缀, 如 "@add"
  std::vector<std::unique_ptr<Value>> args;

  CallInst(int d, const std::string &f, std::vector<std::unique_ptr<Value>> a)
    : dest(d), func_name(f), args(std::move(a)) {}

  void Dump() const override {
    if (dest >= 0) std::cout << "%" << dest << " = ";
    std::cout << "call " << func_name << "(";
    for (size_t i = 0; i < args.size(); ++i) {
      if (i > 0) std::cout << ", ";
      args[i]->Dump();
    }
    std::cout << ")";
  }
};

// 基本块
class BasicBlock {
 public:
  std::string name;
  std::vector<std::unique_ptr<Value>> instructions;

  BasicBlock(const std::string &n) : name(n) {}

  void Dump() const {
    std::cout << "%" << name << ":" << std::endl;
    for (const auto &inst : instructions) {
      std::cout << "  ";
      inst->Dump();
      std::cout << std::endl;
    }
  }
};

// 函数参数
struct FuncParam {
  std::string name;  // 带 @ 前缀, 如 "@x"
  std::string type;  // "i32", "*i32", "*[i32, 10]", 等
};

// 函数 (定义或声明)
class Function {
 public:
  std::string name;                   // 带 @ 前缀, 如 "@main"
  bool is_decl;                       // true: decl 声明; false: fun 定义
  std::vector<FuncParam> params;      // 形式参数列表
  std::string ret_type;              // "i32" 或 "" (void)
  std::vector<std::unique_ptr<BasicBlock>> blocks;

  Function(const std::string &n) : name(n), is_decl(false) {}

  void Dump() const {
    if (is_decl) {
      // decl @getint(): i32
      std::cout << "decl " << name << "(";
      for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << params[i].type;
      }
      std::cout << ")";
      if (!ret_type.empty()) std::cout << ": " << ret_type;
      std::cout << std::endl;
    } else {
      // fun @main(@x: i32): i32 {
      std::cout << "fun " << name << "(";
      for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << params[i].name << ": " << params[i].type;
      }
      std::cout << ")";
      if (!ret_type.empty()) std::cout << ": " << ret_type;
      std::cout << " {" << std::endl;
      for (const auto &block : blocks) {
        block->Dump();
      }
      std::cout << "}" << std::endl;
    }
  }
};

// 程序
class Program {
 public:
  // 全局变量分配 (global alloc)
  std::vector<std::unique_ptr<Value>> global_allocs;
  // 函数声明 (decl)
  std::vector<std::unique_ptr<Function>> declarations;
  // 函数定义 (fun)
  std::vector<std::unique_ptr<Function>> functions;

  void Dump() const {
    for (const auto &decl : declarations) decl->Dump();
    for (const auto &ga : global_allocs) { ga->Dump(); std::cout << std::endl; }
    for (const auto &func : functions) func->Dump();
  }
};
