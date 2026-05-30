#pragma once

#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// 所有 AST 的基类
class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void Dump() const = 0;
};

// CompUnit 是 BaseAST
class CompUnitAST : public BaseAST {
 public:
  // 用智能指针管理对象
  std::unique_ptr<BaseAST> func_def;

    void Dump() const override {
    std::cout << "CompUnitAST { ";
    func_def->Dump();
    std::cout << " }";
  }
};

// FuncDef 也是 BaseAST
class FuncDefAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_type;
  std::string ident;
  std::unique_ptr<BaseAST> block;

  void Dump() const override {
    std::cout << "FuncDefAST { ";
    func_type->Dump();
    std::cout << ", " << ident << ", ";
    block->Dump();
    std::cout << " }";
  }
};

// 其他 AST 类的定义, 例如 FuncTypeAST, BlockAST, StmtAST 等等

class FuncTypeAST : public BaseAST {
 public:
  std::string type;  
  void Dump() const override {
    std::cout << "FuncTypeAST { " << type << " }";
  }
};

class BlockAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> system_category;

  void Dump() const override {
    std::cout << "BlockAST { ";
    for (auto &system_category : system_category) {
      system_category->Dump();
    }
    std::cout << " }";
  }
};

class StmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> num;

  void Dump() const override {
    std::cout << "StmtAST { ";
    num->Dump();
    std::cout << " }";
  }
};

class NumAST : public BaseAST {
 public:
  int num;
  void Dump() const override {
    std::cout << num;
  }
};