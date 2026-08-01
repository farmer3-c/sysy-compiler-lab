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

// alloc 指令: @x = alloc i32
class AllocInst : public Value {
 public:
  std::string name;  // 带 @ 前缀, 如 "@x"

  AllocInst(const std::string &n) : name(n) {}

  void Dump() const override {
    std::cout << name << " = alloc i32";
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

// 返回指令
class Return : public Value {
 public:
  std::unique_ptr<Value> value;

  Return(std::unique_ptr<Value> v) : value(std::move(v)) {}

  void Dump() const override {
    std::cout << "ret ";
    value->Dump();
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

// 函数
class Function {
 public:
  std::string name;
  std::vector<std::unique_ptr<BasicBlock>> blocks;

  Function(const std::string &n) : name(n) {}

  void Dump() const {
    std::cout << "fun @" << name << "(): i32 {" << std::endl;
    for (const auto &block : blocks) {
      block->Dump();
    }
    std::cout << "}" << std::endl;
  }
};

// 程序
class Program {
 public:
  std::vector<std::unique_ptr<Function>> functions;

  void Dump() const {
    for (const auto &func : functions) {
      func->Dump();
    }
  }
};
