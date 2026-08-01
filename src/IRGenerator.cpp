#include "IRGenerator.h"
#include <unordered_map>
#include <iostream>

using namespace std;

// ==================== 符号表 ====================

struct SymbolInfo {
  bool is_const;        // true: 常量; false: 变量
  int const_val;        // 常量值
  string alloc_name;    // 变量对应的 alloc 指令名, 如 "@x"
};

static unordered_map<string, SymbolInfo> symtab;

static void AddConst(const string &name, int val) {
  if (symtab.count(name)) {
    cerr << "error: duplicate symbol " << name << endl;
    return;
  }
  symtab[name] = {true, val, ""};
}

static void AddVar(const string &name, BasicBlock &block, int &reg_counter) {
  if (symtab.count(name)) {
    cerr << "error: duplicate symbol " << name << endl;
    return;
  }
  string alloc_name = "@" + name;
  symtab[name] = {false, 0, alloc_name};
  // 生成 alloc 指令
  block.instructions.push_back(make_unique<AllocInst>(alloc_name));
}

static SymbolInfo *Lookup(const string &name) {
  auto it = symtab.find(name);
  if (it == symtab.end()) return nullptr;
  return &it->second;
}

// ==================== 常量求值 ====================

static int EvaluateConstExp(const BaseAST &ast);
static int EvaluateConstLOrExp(const BaseAST &ast);
static int EvaluateConstLAndExp(const BaseAST &ast);
static int EvaluateConstEqExp(const BaseAST &ast);
static int EvaluateConstRelExp(const BaseAST &ast);
static int EvaluateConstAddExp(const BaseAST &ast);
static int EvaluateConstMulExp(const BaseAST &ast);
static int EvaluateConstUnaryExp(const BaseAST &ast);
static int EvaluateConstPrimaryExp(const BaseAST &ast);

static int EvaluateConstPrimaryExp(const BaseAST &ast) {
  const auto *primary = dynamic_cast<const PrimaryExpAST*>(&ast);
  if (!primary) return 0;

  if (primary->is_number) {
    return primary->number;
  } else if (primary->is_lval) {
    auto *info = Lookup(primary->ident);
    if (!info) {
      cerr << "error: undefined symbol " << primary->ident << endl;
      return 0;
    }
    if (!info->is_const) {
      cerr << "error: " << primary->ident
           << " is not a constant, cannot be used in ConstExp" << endl;
      return 0;
    }
    return info->const_val;
  } else {
    return EvaluateConstExp(*primary->exp);
  }
}

static int EvaluateConstUnaryExp(const BaseAST &ast) {
  const auto *unary = dynamic_cast<const UnaryExpAST*>(&ast);
  if (!unary) return 0;

  if (unary->is_primary) {
    return EvaluateConstPrimaryExp(*unary->primary_exp);
  } else {
    int val = EvaluateConstUnaryExp(*unary->unary_exp);
    if (unary->op == "+") return val;
    if (unary->op == "-") return -val;
    if (unary->op == "!") return (val == 0) ? 1 : 0;
    return 0;
  }
}

static int EvaluateConstMulExp(const BaseAST &ast) {
  const auto *mul = dynamic_cast<const MulExpAST*>(&ast);
  if (!mul) return 0;

  if (mul->is_unary) {
    return EvaluateConstUnaryExp(*mul->unary_exp);
  } else {
    int lhs = EvaluateConstMulExp(*mul->lhs);
    int rhs = EvaluateConstUnaryExp(*mul->rhs);
    if (mul->op == "*") return lhs * rhs;
    if (mul->op == "/") return lhs / rhs;
    if (mul->op == "%") return lhs % rhs;
    return 0;
  }
}

static int EvaluateConstAddExp(const BaseAST &ast) {
  const auto *add = dynamic_cast<const AddExpAST*>(&ast);
  if (!add) return 0;

  if (add->is_mul) {
    return EvaluateConstMulExp(*add->mul_exp);
  } else {
    int lhs = EvaluateConstAddExp(*add->lhs);
    int rhs = EvaluateConstMulExp(*add->rhs);
    if (add->op == "+") return lhs + rhs;
    if (add->op == "-") return lhs - rhs;
    return 0;
  }
}

static int EvaluateConstRelExp(const BaseAST &ast) {
  const auto *rel = dynamic_cast<const RelExpAST*>(&ast);
  if (!rel) return 0;

  if (rel->is_add) {
    return EvaluateConstAddExp(*rel->add_exp);
  } else {
    int lhs = EvaluateConstRelExp(*rel->lhs);
    int rhs = EvaluateConstAddExp(*rel->rhs);
    if (rel->op == "<") return (lhs < rhs) ? 1 : 0;
    if (rel->op == ">") return (lhs > rhs) ? 1 : 0;
    if (rel->op == "<=") return (lhs <= rhs) ? 1 : 0;
    if (rel->op == ">=") return (lhs >= rhs) ? 1 : 0;
    return 0;
  }
}

static int EvaluateConstEqExp(const BaseAST &ast) {
  const auto *eq = dynamic_cast<const EqExpAST*>(&ast);
  if (!eq) return 0;

  if (eq->is_rel) {
    return EvaluateConstRelExp(*eq->rel_exp);
  } else {
    int lhs = EvaluateConstEqExp(*eq->lhs);
    int rhs = EvaluateConstRelExp(*eq->rhs);
    if (eq->op == "==") return (lhs == rhs) ? 1 : 0;
    if (eq->op == "!=") return (lhs != rhs) ? 1 : 0;
    return 0;
  }
}

static int EvaluateConstLAndExp(const BaseAST &ast) {
  const auto *land = dynamic_cast<const LAndExpAST*>(&ast);
  if (!land) return 0;

  if (land->is_eq) {
    return EvaluateConstEqExp(*land->eq_exp);
  } else {
    int lhs = EvaluateConstLAndExp(*land->lhs);
    int rhs = EvaluateConstEqExp(*land->rhs);
    // &&: 两边均非零则为 1, 否则为 0
    return ((lhs != 0) && (rhs != 0)) ? 1 : 0;
  }
}

static int EvaluateConstLOrExp(const BaseAST &ast) {
  const auto *lor = dynamic_cast<const LOrExpAST*>(&ast);
  if (!lor) return 0;

  if (lor->is_land) {
    return EvaluateConstLAndExp(*lor->land_exp);
  } else {
    int lhs = EvaluateConstLOrExp(*lor->lhs);
    int rhs = EvaluateConstLAndExp(*lor->rhs);
    // ||: 任一边非零则为 1, 否则为 0
    return ((lhs != 0) || (rhs != 0)) ? 1 : 0;
  }
}

static int EvaluateConstExp(const BaseAST &ast) {
  const auto *exp = dynamic_cast<const ExpAST*>(&ast);
  if (!exp) return 0;
  return EvaluateConstLOrExp(*exp->lor_exp);
}

// ==================== IR 生成: 表达式求值 ====================

// 前向声明
static unique_ptr<Value> EvaluateExp(const BaseAST &ast,
                                     BasicBlock &block, int &reg_counter);
static unique_ptr<Value> EvaluateLOrExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter);
static unique_ptr<Value> EvaluateLAndExp(const BaseAST &ast,
                                         BasicBlock &block, int &reg_counter);
static unique_ptr<Value> EvaluateEqExp(const BaseAST &ast,
                                       BasicBlock &block, int &reg_counter);
static unique_ptr<Value> EvaluateRelExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter);
static unique_ptr<Value> EvaluateAddExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter);
static unique_ptr<Value> EvaluateMulExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter);
static unique_ptr<Value> EvaluateUnaryExp(const BaseAST &ast,
                                          BasicBlock &block, int &reg_counter);
static unique_ptr<Value> EvaluatePrimaryExp(const BaseAST &ast,
                                            BasicBlock &block, int &reg_counter);

// 计算一元表达式, 返回一个 Value (Integer 或 RegRef)
// 同时将生成的指令添加到 block 中
static unique_ptr<Value> EvaluateUnaryExp(const BaseAST &ast,
                                          BasicBlock &block, int &reg_counter) {
  const auto *unary = dynamic_cast<const UnaryExpAST*>(&ast);
  if (!unary) return nullptr;

  if (unary->is_primary) {
    // UnaryExp ::= PrimaryExp
    return EvaluatePrimaryExp(*unary->primary_exp, block, reg_counter);
  } else {
    // UnaryExp ::= UnaryOp UnaryExp
    auto operand = EvaluateUnaryExp(*unary->unary_exp, block, reg_counter);
    if (!operand) return nullptr;

    if (unary->op == "+") {
      // +X 等价于 X, 无需生成指令
      return operand;
    } else if (unary->op == "-") {
      // -X 等价于 sub 0, X
      int dest = reg_counter++;
      auto inst = make_unique<BinaryInst>(
          dest, "sub", make_unique<Integer>(0), std::move(operand));
      block.instructions.push_back(std::move(inst));
      return make_unique<RegRef>(dest);
    } else if (unary->op == "!") {
      // !X 等价于 eq X, 0
      int dest = reg_counter++;
      auto inst = make_unique<BinaryInst>(
          dest, "eq", std::move(operand), make_unique<Integer>(0));
      block.instructions.push_back(std::move(inst));
      return make_unique<RegRef>(dest);
    }
  }
  return nullptr;
}

// 计算基本表达式: Number 或 ( Exp ) 或 LVal
static unique_ptr<Value> EvaluatePrimaryExp(const BaseAST &ast,
                                            BasicBlock &block, int &reg_counter) {
  const auto *primary = dynamic_cast<const PrimaryExpAST*>(&ast);
  if (!primary) return nullptr;

  if (primary->is_number) {
    // PrimaryExp ::= Number
    return make_unique<Integer>(primary->number);
  } else if (primary->is_lval) {
    // PrimaryExp ::= LVal
    auto *info = Lookup(primary->ident);
    if (!info) {
      cerr << "error: undefined symbol " << primary->ident << endl;
      return make_unique<Integer>(0);
    }
    if (info->is_const) {
      // 常量: 直接返回常量值
      return make_unique<Integer>(info->const_val);
    } else {
      // 变量: 生成 load 指令
      int dest = reg_counter++;
      auto inst = make_unique<LoadInst>(dest, info->alloc_name);
      block.instructions.push_back(std::move(inst));
      return make_unique<RegRef>(dest);
    }
  } else {
    // PrimaryExp ::= "(" Exp ")"
    return EvaluateExp(*primary->exp, block, reg_counter);
  }
}

// 计算乘除模表达式: UnaryExp 或 MulExp op UnaryExp
static unique_ptr<Value> EvaluateMulExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter) {
  const auto *mul = dynamic_cast<const MulExpAST*>(&ast);
  if (!mul) return nullptr;

  if (mul->is_unary) {
    // MulExp ::= UnaryExp
    return EvaluateUnaryExp(*mul->unary_exp, block, reg_counter);
  } else {
    // MulExp ::= MulExp ("*" | "/" | "%") UnaryExp
    auto lhs_val = EvaluateMulExp(*mul->lhs, block, reg_counter);
    auto rhs_val = EvaluateUnaryExp(*mul->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

    // 映射操作符到 Koopa IR 操作码
    std::string koopa_op;
    if (mul->op == "*") koopa_op = "mul";
    else if (mul->op == "/") koopa_op = "div";
    else if (mul->op == "%") koopa_op = "mod";

    int dest = reg_counter++;
    auto inst = make_unique<BinaryInst>(
        dest, koopa_op, std::move(lhs_val), std::move(rhs_val));
    block.instructions.push_back(std::move(inst));
    return make_unique<RegRef>(dest);
  }
}

// 计算加减表达式: MulExp 或 AddExp ("+" | "-") MulExp
static unique_ptr<Value> EvaluateAddExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter) {
  const auto *add = dynamic_cast<const AddExpAST*>(&ast);
  if (!add) return nullptr;

  if (add->is_mul) {
    // AddExp ::= MulExp
    return EvaluateMulExp(*add->mul_exp, block, reg_counter);
  } else {
    // AddExp ::= AddExp ("+" | "-") MulExp
    auto lhs_val = EvaluateAddExp(*add->lhs, block, reg_counter);
    auto rhs_val = EvaluateMulExp(*add->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

    // 映射操作符到 Koopa IR 操作码
    std::string koopa_op;
    if (add->op == "+") koopa_op = "add";
    else if (add->op == "-") koopa_op = "sub";

    int dest = reg_counter++;
    auto inst = make_unique<BinaryInst>(
        dest, koopa_op, std::move(lhs_val), std::move(rhs_val));
    block.instructions.push_back(std::move(inst));
    return make_unique<RegRef>(dest);
  }
}

// 计算比较表达式: AddExp 或 RelExp ("<" | ">" | "<=" | ">=") AddExp
static unique_ptr<Value> EvaluateRelExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter) {
  const auto *rel = dynamic_cast<const RelExpAST*>(&ast);
  if (!rel) return nullptr;

  if (rel->is_add) {
    // RelExp ::= AddExp
    return EvaluateAddExp(*rel->add_exp, block, reg_counter);
  } else {
    // RelExp ::= RelExp ("<" | ">" | "<=" | ">=") AddExp
    auto lhs_val = EvaluateRelExp(*rel->lhs, block, reg_counter);
    auto rhs_val = EvaluateAddExp(*rel->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

    std::string koopa_op;
    if (rel->op == "<") koopa_op = "lt";
    else if (rel->op == ">") koopa_op = "gt";
    else if (rel->op == "<=") koopa_op = "le";
    else if (rel->op == ">=") koopa_op = "ge";

    int dest = reg_counter++;
    auto inst = make_unique<BinaryInst>(
        dest, koopa_op, std::move(lhs_val), std::move(rhs_val));
    block.instructions.push_back(std::move(inst));
    return make_unique<RegRef>(dest);
  }
}

// 计算相等表达式: RelExp 或 EqExp ("==" | "!=") RelExp
static unique_ptr<Value> EvaluateEqExp(const BaseAST &ast,
                                       BasicBlock &block, int &reg_counter) {
  const auto *eq = dynamic_cast<const EqExpAST*>(&ast);
  if (!eq) return nullptr;

  if (eq->is_rel) {
    // EqExp ::= RelExp
    return EvaluateRelExp(*eq->rel_exp, block, reg_counter);
  } else {
    // EqExp ::= EqExp ("==" | "!=") RelExp
    auto lhs_val = EvaluateEqExp(*eq->lhs, block, reg_counter);
    auto rhs_val = EvaluateRelExp(*eq->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

    std::string koopa_op;
    if (eq->op == "==") koopa_op = "eq";
    else if (eq->op == "!=") koopa_op = "ne";

    int dest = reg_counter++;
    auto inst = make_unique<BinaryInst>(
        dest, koopa_op, std::move(lhs_val), std::move(rhs_val));
    block.instructions.push_back(std::move(inst));
    return make_unique<RegRef>(dest);
  }
}

// 计算逻辑与表达式: EqExp 或 LAndExp "&&" EqExp
// X && Y  →  and (ne X, 0), (ne Y, 0)
static unique_ptr<Value> EvaluateLAndExp(const BaseAST &ast,
                                         BasicBlock &block, int &reg_counter) {
  const auto *land = dynamic_cast<const LAndExpAST*>(&ast);
  if (!land) return nullptr;

  if (land->is_eq) {
    // LAndExp ::= EqExp
    return EvaluateEqExp(*land->eq_exp, block, reg_counter);
  } else {
    // LAndExp ::= LAndExp "&&" EqExp
    auto lhs_val = EvaluateLAndExp(*land->lhs, block, reg_counter);
    auto rhs_val = EvaluateEqExp(*land->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

    // 将左右操作数分别与 0 比较不等, 结果进行按位与
    // %t1 = ne lhs, 0
    int t1 = reg_counter++;
    auto inst1 = make_unique<BinaryInst>(
        t1, "ne", std::move(lhs_val), make_unique<Integer>(0));
    block.instructions.push_back(std::move(inst1));
    // %t2 = ne rhs, 0
    int t2 = reg_counter++;
    auto inst2 = make_unique<BinaryInst>(
        t2, "ne", std::move(rhs_val), make_unique<Integer>(0));
    block.instructions.push_back(std::move(inst2));
    // %t3 = and %t1, %t2
    int dest = reg_counter++;
    auto inst3 = make_unique<BinaryInst>(
        dest, "and", make_unique<RegRef>(t1), make_unique<RegRef>(t2));
    block.instructions.push_back(std::move(inst3));
    return make_unique<RegRef>(dest);
  }
}

// 计算逻辑或表达式: LAndExp 或 LOrExp "||" LAndExp
// X || Y  →  or (ne X, 0), (ne Y, 0)
static unique_ptr<Value> EvaluateLOrExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter) {
  const auto *lor = dynamic_cast<const LOrExpAST*>(&ast);
  if (!lor) return nullptr;

  if (lor->is_land) {
    // LOrExp ::= LAndExp
    return EvaluateLAndExp(*lor->land_exp, block, reg_counter);
  } else {
    // LOrExp ::= LOrExp "||" LAndExp
    auto lhs_val = EvaluateLOrExp(*lor->lhs, block, reg_counter);
    auto rhs_val = EvaluateLAndExp(*lor->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

    // 将左右操作数分别与 0 比较不等, 结果进行按位或
    // %t1 = ne lhs, 0
    int t1 = reg_counter++;
    auto inst1 = make_unique<BinaryInst>(
        t1, "ne", std::move(lhs_val), make_unique<Integer>(0));
    block.instructions.push_back(std::move(inst1));
    // %t2 = ne rhs, 0
    int t2 = reg_counter++;
    auto inst2 = make_unique<BinaryInst>(
        t2, "ne", std::move(rhs_val), make_unique<Integer>(0));
    block.instructions.push_back(std::move(inst2));
    // %t3 = or %t1, %t2
    int dest = reg_counter++;
    auto inst3 = make_unique<BinaryInst>(
        dest, "or", make_unique<RegRef>(t1), make_unique<RegRef>(t2));
    block.instructions.push_back(std::move(inst3));
    return make_unique<RegRef>(dest);
  }
}

// 计算表达式: LOrExp
static unique_ptr<Value> EvaluateExp(const BaseAST &ast,
                                     BasicBlock &block, int &reg_counter) {
  const auto *exp = dynamic_cast<const ExpAST*>(&ast);
  if (!exp) return nullptr;

  return EvaluateLOrExp(*exp->lor_exp, block, reg_counter);
}

// ==================== 处理声明 ====================

// 处理常量声明: 在编译期求值, 插入符号表
static void ProcessConstDecl(const BaseAST &ast, BasicBlock &block, int &reg_counter) {
  const auto *decl = dynamic_cast<const DeclAST*>(&ast);
  if (!decl || !decl->is_const) return;

  const auto *const_decl = dynamic_cast<const ConstDeclAST*>(decl->decl_body.get());
  if (!const_decl) return;

  for (auto &def : const_decl->const_defs) {
    const auto *const_def = dynamic_cast<const ConstDefAST*>(def.get());
    if (!const_def) continue;

    // 编译期求值 ConstInitVal
    const auto *init = dynamic_cast<const ConstInitValAST*>(const_def->init_val.get());
    if (!init) continue;

    int val = EvaluateConstExp(*init->exp);
    AddConst(const_def->ident, val);
  }
}

// 处理变量声明: 生成 alloc + 可能的 store, 插入符号表
static void ProcessVarDecl(const BaseAST &ast, BasicBlock &block, int &reg_counter) {
  const auto *decl = dynamic_cast<const DeclAST*>(&ast);
  if (!decl || decl->is_const) return;

  const auto *var_decl = dynamic_cast<const VarDeclAST*>(decl->decl_body.get());
  if (!var_decl) return;

  for (auto &def : var_decl->var_defs) {
    const auto *var_def = dynamic_cast<const VarDefAST*>(def.get());
    if (!var_def) continue;

    // 插入符号表并生成 alloc
    AddVar(var_def->ident, block, reg_counter);

    // 如果有初始值, 生成 store
    if (var_def->has_init) {
      const auto *init_val = dynamic_cast<const InitValAST*>(var_def->init_val.get());
      if (!init_val) continue;

      auto val = EvaluateExp(*init_val->exp, block, reg_counter);
      if (!val) continue;

      auto *info = Lookup(var_def->ident);
      block.instructions.push_back(
          make_unique<StoreInst>(std::move(val), info->alloc_name));
    }
  }
}

// ==================== 处理赋值语句 ====================

static void ProcessAssign(const BaseAST &ast, BasicBlock &block, int &reg_counter) {
  const auto *stmt = dynamic_cast<const StmtAST*>(&ast);
  if (!stmt || stmt->is_return) return;

  // 左侧 LVal
  const auto *lval = dynamic_cast<const LValAST*>(stmt->lval.get());
  if (!lval) return;

  // 语义检查: 符号必须已定义
  auto *info = Lookup(lval->ident);
  if (!info) {
    cerr << "error: undefined symbol " << lval->ident << endl;
    return;
  }
  // 语义检查: 不能给常量赋值
  if (info->is_const) {
    cerr << "error: cannot assign to constant " << lval->ident << endl;
    return;
  }

  // 求值右侧表达式
  auto val = EvaluateExp(*stmt->assign_exp, block, reg_counter);
  if (!val) return;

  // 生成 store 指令
  block.instructions.push_back(
      make_unique<StoreInst>(std::move(val), info->alloc_name));
}

// ==================== 主入口 ====================

unique_ptr<Program> GenerateIR(const BaseAST &ast) {
  auto program = make_unique<Program>();

  // 清空符号表
  symtab.clear();

  // 遍历 AST 生成 IR
  const auto *comp_unit = dynamic_cast<const CompUnitAST*>(&ast);
  if (!comp_unit) {
    return program;
  }

  const auto *func_def = dynamic_cast<const FuncDefAST*>(comp_unit->func_def.get());
  if (!func_def) {
    return program;
  }

  // 创建函数
  auto func = make_unique<Function>(func_def->ident);

  // 创建入口基本块
  auto entry_block = make_unique<BasicBlock>("entry");

  // 遍历函数体中的所有 BlockItem
  int reg_counter = 0;
  const auto *block_ast = dynamic_cast<const BlockAST*>(func_def->block.get());
  if (block_ast) {
    for (auto &item : block_ast->items) {
      const auto *block_item = dynamic_cast<const BlockItemAST*>(item.get());
      if (!block_item) continue;

      if (block_item->is_stmt) {
        // Stmt: return 或 assignment
        const auto *stmt = dynamic_cast<const StmtAST*>(block_item->item.get());
        if (!stmt) continue;

        if (stmt->is_return) {
          auto ret_val = EvaluateExp(*stmt->exp, *entry_block, reg_counter);
          if (ret_val) {
            auto ret = make_unique<Return>(std::move(ret_val));
            entry_block->instructions.push_back(std::move(ret));
          }
        } else {
          ProcessAssign(*stmt, *entry_block, reg_counter);
        }
      } else {
        // Decl: ConstDecl 或 VarDecl
        const auto *decl = dynamic_cast<const DeclAST*>(block_item->item.get());
        if (!decl) continue;

        if (decl->is_const) {
          ProcessConstDecl(*decl, *entry_block, reg_counter);
        } else {
          ProcessVarDecl(*decl, *entry_block, reg_counter);
        }
      }
    }
  }

  func->blocks.push_back(std::move(entry_block));
  program->functions.push_back(std::move(func));

  return program;
}
