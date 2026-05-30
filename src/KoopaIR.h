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
