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

// ==================== CompUnit ====================
// CompUnit ::= [CompUnit] (Decl | FuncDef)
// 用 items 列表存储顶层声明/函数定义

// 前置声明
class CompUnitItemAST;

class CompUnitAST : public BaseAST {
 public:
  // 每个元素要么是 Decl (全局变量/常量), 要么是 FuncDef
  std::vector<std::unique_ptr<BaseAST>> items;

  void Dump() const override {
    std::cout << "CompUnitAST { ";
    for (auto &item : items) {
      item->Dump();
      std::cout << " ";
    }
    std::cout << "}";
  }
};

// ==================== FuncFParam ====================
// FuncFParam ::= BType IDENT ["[" "]" {"[" ConstExp "]"}]
class FuncFParamAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> btype;
  std::string ident;
  bool is_array;  // true: 数组参数 (第一维省略)
  // 已知维度: 对于 int arr[][10][20], dims 包含 10, 20
  // 对于 int arr[], dims 为空
  // 对于 int arr[10], dims 包含 10 (但这种情况语法约束第一维必须空)
  std::vector<std::unique_ptr<BaseAST>> dims;

  void Dump() const override {
    std::cout << "FuncFParamAST { ";
    btype->Dump();
    std::cout << ", " << ident;
    if (is_array) {
      std::cout << "[]";
      for (auto &d : dims) { std::cout << "["; d->Dump(); std::cout << "]"; }
    }
    std::cout << " }";
  }
};

// ==================== FuncDef (Lv8: 增加参数) ====================
// FuncDef ::= FuncType IDENT "(" [FuncFParams] ")" Block;
class FuncDefAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_type;
  std::string ident;
  bool has_params;  // true 表示有参数列表
  std::vector<std::unique_ptr<BaseAST>> params;  // FuncFParamAST 列表
  std::unique_ptr<BaseAST> block;

  void Dump() const override {
    std::cout << "FuncDefAST { ";
    func_type->Dump();
    std::cout << ", " << ident;
    if (has_params) {
      for (auto &p : params) {
        std::cout << ", ";
        p->Dump();
      }
    }
    std::cout << ", ";
    block->Dump();
    std::cout << " }";
  }
};

// FuncType ::= "void" | "int"
class FuncTypeAST : public BaseAST {
 public:
  std::string type;  // "void" 或 "int"
  void Dump() const override {
    std::cout << "FuncTypeAST { " << type << " }";
  }
};

// ==================== BType ====================
// BType ::= "int"
class BTypeAST : public BaseAST {
 public:
  std::string type;
  void Dump() const override {
    std::cout << "BTypeAST { " << type << " }";
  }
};

// ==================== LVal ====================
// LVal ::= IDENT {"[" Exp "]"}
class LValAST : public BaseAST {
 public:
  std::string ident;
  // 数组索引表达式列表 (为空表示标量)
  std::vector<std::unique_ptr<BaseAST>> indices;
  void Dump() const override {
    std::cout << "LValAST { " << ident;
    for (auto &idx : indices) {
      std::cout << "[";
      idx->Dump();
      std::cout << "]";
    }
    std::cout << " }";
  }
};

// ==================== ConstInitVal ====================
// ConstInitVal ::= ConstExp | "{" [ConstInitVal {"," ConstInitVal}] "}"
class ConstInitValAST : public BaseAST {
 public:
  bool is_list;  // true: items (列表); false: exp (标量)
  std::unique_ptr<BaseAST> exp;  // scalar ConstExp
  std::vector<std::unique_ptr<BaseAST>> items;  // list elements (ConstInitValAST)
  void Dump() const override {
    std::cout << "ConstInitValAST { ";
    if (is_list) {
      std::cout << "{";
      for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) std::cout << ", ";
        items[i]->Dump();
      }
      std::cout << "}";
    } else {
      exp->Dump();
    }
    std::cout << " }";
  }
};

// ==================== ConstDef ====================
// ConstDef ::= IDENT {"[" ConstExp "]"} "=" ConstInitVal
class ConstDefAST : public BaseAST {
 public:
  std::string ident;
  // 数组维度表达式列表 (ConstExp), 为空表示标量
  std::vector<std::unique_ptr<BaseAST>> dims;
  std::unique_ptr<BaseAST> init_val;
  void Dump() const override {
    std::cout << "ConstDefAST { " << ident;
    for (auto &d : dims) { std::cout << "["; d->Dump(); std::cout << "]"; }
    std::cout << ", ";
    init_val->Dump();
    std::cout << " }";
  }
};

// ==================== ConstDecl ====================
// ConstDecl ::= "const" BType ConstDef {"," ConstDef} ";"
class ConstDeclAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> btype;
  std::vector<std::unique_ptr<BaseAST>> const_defs;
  void Dump() const override {
    std::cout << "ConstDeclAST { ";
    btype->Dump();
    for (auto &def : const_defs) {
      std::cout << ", ";
      def->Dump();
    }
    std::cout << " }";
  }
};

// ==================== InitVal ====================
// InitVal ::= Exp | "{" [InitVal {"," InitVal}] "}"
class InitValAST : public BaseAST {
 public:
  bool is_list;  // true: items (列表); false: exp (标量)
  std::unique_ptr<BaseAST> exp;  // scalar Exp
  std::vector<std::unique_ptr<BaseAST>> items;  // list elements (each is InitValAST)
  void Dump() const override {
    std::cout << "InitValAST { ";
    if (is_list) {
      std::cout << "{";
      for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) std::cout << ", ";
        items[i]->Dump();
      }
      std::cout << "}";
    } else {
      exp->Dump();
    }
    std::cout << " }";
  }
};

// ==================== VarDef ====================
// VarDef ::= IDENT {"[" ConstExp "]"} ["=" InitVal]
class VarDefAST : public BaseAST {
 public:
  std::string ident;
  // 数组维度表达式列表 (ConstExp), 为空表示标量
  std::vector<std::unique_ptr<BaseAST>> dims;
  bool has_init;
  std::unique_ptr<BaseAST> init_val;
  void Dump() const override {
    std::cout << "VarDefAST { " << ident;
    for (auto &d : dims) { std::cout << "["; d->Dump(); std::cout << "]"; }
    if (has_init) {
      std::cout << ", ";
      init_val->Dump();
    }
    std::cout << " }";
  }
};

// ==================== VarDecl ====================
// VarDecl ::= BType VarDef {"," VarDef} ";"
class VarDeclAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> btype;
  std::vector<std::unique_ptr<BaseAST>> var_defs;
  void Dump() const override {
    std::cout << "VarDeclAST { ";
    btype->Dump();
    for (auto &def : var_defs) {
      std::cout << ", ";
      def->Dump();
    }
    std::cout << " }";
  }
};

// ==================== Decl ====================
// Decl ::= ConstDecl | VarDecl
class DeclAST : public BaseAST {
 public:
  bool is_const;  // true: ConstDecl; false: VarDecl
  std::unique_ptr<BaseAST> decl_body;
  void Dump() const override {
    std::cout << "DeclAST { ";
    decl_body->Dump();
    std::cout << " }";
  }
};

// ==================== BlockItem ====================
// BlockItem ::= Decl | Stmt
class BlockItemAST : public BaseAST {
 public:
  bool is_stmt;  // true: Stmt; false: Decl
  std::unique_ptr<BaseAST> item;
  void Dump() const override {
    std::cout << "BlockItemAST { ";
    item->Dump();
    std::cout << " }";
  }
};

// ==================== Block ====================
// Block ::= "{" {BlockItem} "}"
class BlockAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> items;

  void Dump() const override {
    std::cout << "BlockAST { ";
    for (auto &item : items) {
      item->Dump();
      std::cout << " ";
    }
    std::cout << "}";
  }
};

// Exp ::= LOrExp
class ExpAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> lor_exp;

  void Dump() const override {
    std::cout << "ExpAST { ";
    lor_exp->Dump();
    std::cout << " }";
  }
};

// ==================== FuncRParams ====================
// FuncRParams ::= Exp {"," Exp}
class FuncRParamsAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> exps;

  void Dump() const override {
    std::cout << "FuncRParamsAST { ";
    for (size_t i = 0; i < exps.size(); ++i) {
      if (i > 0) std::cout << ", ";
      exps[i]->Dump();
    }
    std::cout << " }";
  }
};

// AddExp ::= MulExp | AddExp ("+" | "-") MulExp
class AddExpAST : public BaseAST {
 public:
  bool is_mul;
  std::unique_ptr<BaseAST> mul_exp;
  std::unique_ptr<BaseAST> lhs;
  std::string op;
  std::unique_ptr<BaseAST> rhs;

  void Dump() const override {
    std::cout << "AddExpAST { ";
    if (is_mul) {
      mul_exp->Dump();
    } else {
      std::cout << op << ", ";
      lhs->Dump();
      std::cout << ", ";
      rhs->Dump();
    }
    std::cout << " }";
  }
};

// MulExp ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp
class MulExpAST : public BaseAST {
 public:
  bool is_unary;
  std::unique_ptr<BaseAST> unary_exp;
  std::unique_ptr<BaseAST> lhs;
  std::string op;
  std::unique_ptr<BaseAST> rhs;

  void Dump() const override {
    std::cout << "MulExpAST { ";
    if (is_unary) {
      unary_exp->Dump();
    } else {
      std::cout << op << ", ";
      lhs->Dump();
      std::cout << ", ";
      rhs->Dump();
    }
    std::cout << " }";
  }
};

// RelExp ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp
class RelExpAST : public BaseAST {
 public:
  bool is_add;
  std::unique_ptr<BaseAST> add_exp;
  std::unique_ptr<BaseAST> lhs;
  std::string op;
  std::unique_ptr<BaseAST> rhs;

  void Dump() const override {
    std::cout << "RelExpAST { ";
    if (is_add) {
      add_exp->Dump();
    } else {
      std::cout << op << ", ";
      lhs->Dump();
      std::cout << ", ";
      rhs->Dump();
    }
    std::cout << " }";
  }
};

// EqExp ::= RelExp | EqExp ("==" | "!=") RelExp
class EqExpAST : public BaseAST {
 public:
  bool is_rel;
  std::unique_ptr<BaseAST> rel_exp;
  std::unique_ptr<BaseAST> lhs;
  std::string op;
  std::unique_ptr<BaseAST> rhs;

  void Dump() const override {
    std::cout << "EqExpAST { ";
    if (is_rel) {
      rel_exp->Dump();
    } else {
      std::cout << op << ", ";
      lhs->Dump();
      std::cout << ", ";
      rhs->Dump();
    }
    std::cout << " }";
  }
};

// LAndExp ::= EqExp | LAndExp "&&" EqExp
class LAndExpAST : public BaseAST {
 public:
  bool is_eq;
  std::unique_ptr<BaseAST> eq_exp;
  std::unique_ptr<BaseAST> lhs;
  std::string op;
  std::unique_ptr<BaseAST> rhs;

  void Dump() const override {
    std::cout << "LAndExpAST { ";
    if (is_eq) {
      eq_exp->Dump();
    } else {
      std::cout << op << ", ";
      lhs->Dump();
      std::cout << ", ";
      rhs->Dump();
    }
    std::cout << " }";
  }
};

// LOrExp ::= LAndExp | LOrExp "||" LAndExp
class LOrExpAST : public BaseAST {
 public:
  bool is_land;
  std::unique_ptr<BaseAST> land_exp;
  std::unique_ptr<BaseAST> lhs;
  std::string op;
  std::unique_ptr<BaseAST> rhs;

  void Dump() const override {
    std::cout << "LOrExpAST { ";
    if (is_land) {
      land_exp->Dump();
    } else {
      std::cout << op << ", ";
      lhs->Dump();
      std::cout << ", ";
      rhs->Dump();
    }
    std::cout << " }";
  }
};

// PrimaryExp ::= "(" Exp ")" | LVal | Number
class PrimaryExpAST : public BaseAST {
 public:
  bool is_number;
  bool is_lval;
  std::unique_ptr<BaseAST> exp;
  int number;
  std::string ident;
  bool has_indices;  // true if LVal has array indices
  std::vector<std::unique_ptr<BaseAST>> indices;  // array index expressions (Exp)

  void Dump() const override {
    std::cout << "PrimaryExpAST { ";
    if (is_number) {
      std::cout << number;
    } else if (is_lval) {
      std::cout << ident;
      if (has_indices) {
        for (auto &idx : indices) {
          std::cout << "[";
          idx->Dump();
          std::cout << "]";
        }
      }
    } else {
      exp->Dump();
    }
    std::cout << " }";
  }
};

// UnaryExp ::= PrimaryExp
//            | IDENT "(" [FuncRParams] ")"   // 函数调用
//            | UnaryOp UnaryExp
class UnaryExpAST : public BaseAST {
 public:
  enum Kind { PRIMARY, CALL, UNARY_OP };
  Kind kind;

  // PRIMARY
  std::unique_ptr<BaseAST> primary_exp;

  // CALL
  std::string func_name;       // 被调用函数名
  bool has_args;               // 是否有实参
  std::vector<std::unique_ptr<BaseAST>> args;  // 实参列表 (ExpAST)

  // UNARY_OP
  std::string op;
  std::unique_ptr<BaseAST> unary_exp;

  void Dump() const override {
    std::cout << "UnaryExpAST { ";
    if (kind == PRIMARY) {
      primary_exp->Dump();
    } else if (kind == CALL) {
      std::cout << "call " << func_name << "(";
      for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) std::cout << ", ";
        args[i]->Dump();
      }
      std::cout << ")";
    } else {
      std::cout << op << ", ";
      unary_exp->Dump();
    }
    std::cout << " }";
  }
};

// Stmt ::= "return" [Exp] ";"
//         | LVal "=" Exp ";"
//         | [Exp] ";"
//         | Block
//         | "if" "(" Exp ")" Stmt ["else" Stmt]
//         | "while" "(" Exp ")" Stmt
//         | "break" ";"
//         | "continue" ";"
class StmtAST : public BaseAST {
 public:
  enum Kind { RETURN, ASSIGN, EXP_STMT, BLOCK, IF_ELSE, WHILE, BREAK, CONTINUE };
  Kind kind;
  std::unique_ptr<BaseAST> exp;        // RETURN: return 表达式 (nullptr = void return)
                                       // EXP_STMT: 表达式 (nullptr 表示空语句)
                                       // IF_ELSE: 条件表达式
                                       // WHILE: 循环条件
  bool has_ret_val;                    // RETURN: true 表示有返回值
  std::unique_ptr<BaseAST> lval;       // ASSIGN: 赋值左侧
  std::unique_ptr<BaseAST> assign_exp; // ASSIGN: 赋值右侧
  std::unique_ptr<BaseAST> block;      // BLOCK: 嵌套语句块
  std::unique_ptr<BaseAST> then_stmt;  // IF_ELSE: then 分支
  std::unique_ptr<BaseAST> else_stmt;  // IF_ELSE: else 分支 (nullptr 表示无 else)
  std::unique_ptr<BaseAST> body;       // WHILE: 循环体

  void Dump() const override {
    std::cout << "StmtAST { ";
    if (kind == RETURN) {
      std::cout << "return";
      if (has_ret_val) {
        std::cout << ", ";
        exp->Dump();
      }
    } else if (kind == ASSIGN) {
      lval->Dump();
      std::cout << " = ";
      assign_exp->Dump();
    } else if (kind == EXP_STMT) {
      if (exp) {
        exp->Dump();
      } else {
        std::cout << ";";
      }
    } else if (kind == BLOCK) {
      block->Dump();
    } else if (kind == IF_ELSE) {
      std::cout << "if (";
      exp->Dump();
      std::cout << ") ";
      then_stmt->Dump();
      if (else_stmt) {
        std::cout << " else ";
        else_stmt->Dump();
      }
    } else if (kind == WHILE) {
      std::cout << "while (";
      exp->Dump();
      std::cout << ") ";
      body->Dump();
    } else if (kind == BREAK) {
      std::cout << "break";
    } else if (kind == CONTINUE) {
      std::cout << "continue";
    }
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
