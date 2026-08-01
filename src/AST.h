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
// 以及新增的 ExpAST, PrimaryExpAST, UnaryExpAST 等

class FuncTypeAST : public BaseAST {
 public:
  std::string type;
  void Dump() const override {
    std::cout << "FuncTypeAST { " << type << " }";
  }
};

// ==================== Lv4 新增: BType ====================
// BType ::= "int"
class BTypeAST : public BaseAST {
 public:
  std::string type;
  void Dump() const override {
    std::cout << "BTypeAST { " << type << " }";
  }
};

// ==================== Lv4 新增: LVal ====================
// LVal ::= IDENT
class LValAST : public BaseAST {
 public:
  std::string ident;
  void Dump() const override {
    std::cout << "LValAST { " << ident << " }";
  }
};

// ==================== Lv4 新增: ConstInitVal ====================
// ConstInitVal ::= ConstExp
class ConstInitValAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> exp;
  void Dump() const override {
    std::cout << "ConstInitValAST { ";
    exp->Dump();
    std::cout << " }";
  }
};

// ==================== Lv4 新增: ConstDef ====================
// ConstDef ::= IDENT "=" ConstInitVal
class ConstDefAST : public BaseAST {
 public:
  std::string ident;
  std::unique_ptr<BaseAST> init_val;
  void Dump() const override {
    std::cout << "ConstDefAST { " << ident << ", ";
    init_val->Dump();
    std::cout << " }";
  }
};

// ==================== Lv4 新增: ConstDecl ====================
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

// ==================== Lv4 新增: InitVal ====================
// InitVal ::= Exp
class InitValAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> exp;
  void Dump() const override {
    std::cout << "InitValAST { ";
    exp->Dump();
    std::cout << " }";
  }
};

// ==================== Lv4 新增: VarDef ====================
// VarDef ::= IDENT | IDENT "=" InitVal
class VarDefAST : public BaseAST {
 public:
  std::string ident;
  bool has_init;
  std::unique_ptr<BaseAST> init_val;
  void Dump() const override {
    std::cout << "VarDefAST { " << ident;
    if (has_init) {
      std::cout << ", ";
      init_val->Dump();
    }
    std::cout << " }";
  }
};

// ==================== Lv4 新增: VarDecl ====================
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

// ==================== Lv4 新增: Decl ====================
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

// ==================== Lv4 新增: BlockItem ====================
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

// ==================== 修改: Block ====================
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
// 表达式现在从 LOrExp 开始, 涵盖所有二元运算和逻辑运算
class ExpAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> lor_exp;

  void Dump() const override {
    std::cout << "ExpAST { ";
    lor_exp->Dump();
    std::cout << " }";
  }
};

// AddExp ::= MulExp | AddExp ("+" | "-") MulExp
// 只设计一种 AST 涵盖右侧三种规则, 用 is_mul 区分
class AddExpAST : public BaseAST {
 public:
  bool is_mul;                        // true: MulExp; false: AddExp ("+"|"-") MulExp
  std::unique_ptr<BaseAST> mul_exp;  // 当 is_mul == true 时使用
  std::unique_ptr<BaseAST> lhs;      // 当 is_mul == false 时使用 (左 AddExp)
  std::string op;                     // 当 is_mul == false 时使用: "+" / "-"
  std::unique_ptr<BaseAST> rhs;      // 当 is_mul == false 时使用 (右 MulExp)

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
// 只设计一种 AST 涵盖右侧四种规则, 用 is_unary 区分
class MulExpAST : public BaseAST {
 public:
  bool is_unary;                         // true: UnaryExp; false: MulExp ("*"|"/"|"%") UnaryExp
  std::unique_ptr<BaseAST> unary_exp;   // 当 is_unary == true 时使用
  std::unique_ptr<BaseAST> lhs;         // 当 is_unary == false 时使用 (左 MulExp)
  std::string op;                        // 当 is_unary == false 时使用: "*" / "/" / "%"
  std::unique_ptr<BaseAST> rhs;         // 当 is_unary == false 时使用 (右 UnaryExp)

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
// 只设计一种 AST 涵盖右侧五种规则, 用 is_add 区分
class RelExpAST : public BaseAST {
 public:
  bool is_add;                        // true: AddExp; false: RelExp op AddExp
  std::unique_ptr<BaseAST> add_exp;  // 当 is_add == true 时使用
  std::unique_ptr<BaseAST> lhs;      // 当 is_add == false 时使用 (左 RelExp)
  std::string op;                     // 当 is_add == false 时使用: "<" / ">" / "<=" / ">="
  std::unique_ptr<BaseAST> rhs;      // 当 is_add == false 时使用 (右 AddExp)

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
// 只设计一种 AST 涵盖右侧三种规则, 用 is_rel 区分
class EqExpAST : public BaseAST {
 public:
  bool is_rel;                        // true: RelExp; false: EqExp ("=="|"!=") RelExp
  std::unique_ptr<BaseAST> rel_exp;  // 当 is_rel == true 时使用
  std::unique_ptr<BaseAST> lhs;      // 当 is_rel == false 时使用 (左 EqExp)
  std::string op;                     // 当 is_rel == false 时使用: "==" / "!="
  std::unique_ptr<BaseAST> rhs;      // 当 is_rel == false 时使用 (右 RelExp)

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
// 只设计一种 AST 涵盖右侧两种规则, 用 is_eq 区分
class LAndExpAST : public BaseAST {
 public:
  bool is_eq;                         // true: EqExp; false: LAndExp "&&" EqExp
  std::unique_ptr<BaseAST> eq_exp;   // 当 is_eq == true 时使用
  std::unique_ptr<BaseAST> lhs;      // 当 is_eq == false 时使用 (左 LAndExp)
  std::string op;                     // 当 is_eq == false 时使用: "&&"
  std::unique_ptr<BaseAST> rhs;      // 当 is_eq == false 时使用 (右 EqExp)

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
// 只设计一种 AST 涵盖右侧两种规则, 用 is_land 区分
class LOrExpAST : public BaseAST {
 public:
  bool is_land;                         // true: LAndExp; false: LOrExp "||" LAndExp
  std::unique_ptr<BaseAST> land_exp;   // 当 is_land == true 时使用
  std::unique_ptr<BaseAST> lhs;        // 当 is_land == false 时使用 (左 LOrExp)
  std::string op;                       // 当 is_land == false 时使用: "||"
  std::unique_ptr<BaseAST> rhs;        // 当 is_land == false 时使用 (右 LAndExp)

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
// 支持三种情况: Number / (Exp) / LVal
class PrimaryExpAST : public BaseAST {
 public:
  bool is_number;                    // true: Number
  bool is_lval;                      // true: LVal (仅当 is_number == false 时有效)
  std::unique_ptr<BaseAST> exp;     // 当 is_number == false && is_lval == false 时使用: ( Exp )
  int number;                        // 当 is_number == true 时使用
  std::string ident;                 // 当 is_lval == true 时使用

  void Dump() const override {
    std::cout << "PrimaryExpAST { ";
    if (is_number) {
      std::cout << number;
    } else if (is_lval) {
      std::cout << ident;
    } else {
      exp->Dump();
    }
    std::cout << " }";
  }
};

// UnaryExp ::= PrimaryExp | UnaryOp UnaryExp
// 只设计一种 AST 涵盖右侧两种规则, 用 is_primary 区分
class UnaryExpAST : public BaseAST {
 public:
  bool is_primary;                    // true: PrimaryExp; false: UnaryOp UnaryExp
  std::unique_ptr<BaseAST> primary_exp;  // 当 is_primary == true 时使用
  std::string op;                        // 当 is_primary == false 时使用: "+" / "-" / "!"
  std::unique_ptr<BaseAST> unary_exp;    // 当 is_primary == false 时使用

  void Dump() const override {
    std::cout << "UnaryExpAST { ";
    if (is_primary) {
      primary_exp->Dump();
    } else {
      std::cout << op << ", ";
      unary_exp->Dump();
    }
    std::cout << " }";
  }
};

// Stmt ::= "return" Exp ";" | LVal "=" Exp ";"
//         | [Exp] ";" | Block
// 支持 return、赋值、表达式语句、空语句、嵌套语句块
class StmtAST : public BaseAST {
 public:
  enum Kind { RETURN, ASSIGN, EXP_STMT, BLOCK };
  Kind kind;
  std::unique_ptr<BaseAST> exp;       // RETURN: return 表达式
                                      // EXP_STMT: 表达式 (nullptr 表示空语句)
  std::unique_ptr<BaseAST> lval;      // ASSIGN: 赋值左侧
  std::unique_ptr<BaseAST> assign_exp; // ASSIGN: 赋值右侧
  std::unique_ptr<BaseAST> block;     // BLOCK: 嵌套语句块

  void Dump() const override {
    std::cout << "StmtAST { ";
    if (kind == RETURN) {
      std::cout << "return, ";
      exp->Dump();
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
