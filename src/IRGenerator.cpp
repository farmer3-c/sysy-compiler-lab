#include "IRGenerator.h"
#include <unordered_map>
#include <vector>
#include <iostream>

using namespace std;

// ==================== 作用域符号表 ====================

struct SymbolInfo {
  bool is_const;        // true: 常量; false: 变量
  int const_val;        // 常量值
  string alloc_name;    // 变量对应的 alloc 指令名, 如 "@x"
};

// 符号表栈: 索引 0 是最外层作用域, back() 是当前作用域
static vector<unordered_map<string, SymbolInfo>> scope_stack;

// alloc 指令计数器, 确保每个 @name 全局唯一
static int alloc_counter = 0;

// 进入新作用域 (进入 Block 时调用)
static void EnterScope() {
  scope_stack.emplace_back();
}

// 退出当前作用域 (离开 Block 时调用)
static void ExitScope() {
  if (!scope_stack.empty()) scope_stack.pop_back();
}

// 在当前作用域插入常量
static void AddConst(const string &name, int val) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) {
    cerr << "error: duplicate symbol " << name << endl;
    return;
  }
  cur[name] = {true, val, ""};
}

// 在当前作用域插入变量, 同时生成 alloc 指令
static void AddVar(const string &name, BasicBlock &block, int &reg_counter) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) {
    cerr << "error: duplicate symbol " << name << endl;
    return;
  }
  string alloc_name = "@" + name + "_" + std::to_string(alloc_counter++);
  cur[name] = {false, 0, alloc_name};
  block.instructions.push_back(make_unique<AllocInst>(alloc_name));
}

// 跨作用域查询符号: 从内向外逐层查找
static SymbolInfo *Lookup(const string &name) {
  for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return &found->second;
  }
  return nullptr;
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

static unique_ptr<Value> EvaluateUnaryExp(const BaseAST &ast,
                                          BasicBlock &block, int &reg_counter) {
  const auto *unary = dynamic_cast<const UnaryExpAST*>(&ast);
  if (!unary) return nullptr;

  if (unary->is_primary) {
    return EvaluatePrimaryExp(*unary->primary_exp, block, reg_counter);
  } else {
    auto operand = EvaluateUnaryExp(*unary->unary_exp, block, reg_counter);
    if (!operand) return nullptr;

    if (unary->op == "+") {
      return operand;
    } else if (unary->op == "-") {
      int dest = reg_counter++;
      auto inst = make_unique<BinaryInst>(
          dest, "sub", make_unique<Integer>(0), std::move(operand));
      block.instructions.push_back(std::move(inst));
      return make_unique<RegRef>(dest);
    } else if (unary->op == "!") {
      int dest = reg_counter++;
      auto inst = make_unique<BinaryInst>(
          dest, "eq", std::move(operand), make_unique<Integer>(0));
      block.instructions.push_back(std::move(inst));
      return make_unique<RegRef>(dest);
    }
  }
  return nullptr;
}

static unique_ptr<Value> EvaluatePrimaryExp(const BaseAST &ast,
                                            BasicBlock &block, int &reg_counter) {
  const auto *primary = dynamic_cast<const PrimaryExpAST*>(&ast);
  if (!primary) return nullptr;

  if (primary->is_number) {
    return make_unique<Integer>(primary->number);
  } else if (primary->is_lval) {
    auto *info = Lookup(primary->ident);
    if (!info) {
      cerr << "error: undefined symbol " << primary->ident << endl;
      return make_unique<Integer>(0);
    }
    if (info->is_const) {
      return make_unique<Integer>(info->const_val);
    } else {
      int dest = reg_counter++;
      auto inst = make_unique<LoadInst>(dest, info->alloc_name);
      block.instructions.push_back(std::move(inst));
      return make_unique<RegRef>(dest);
    }
  } else {
    return EvaluateExp(*primary->exp, block, reg_counter);
  }
}

static unique_ptr<Value> EvaluateMulExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter) {
  const auto *mul = dynamic_cast<const MulExpAST*>(&ast);
  if (!mul) return nullptr;

  if (mul->is_unary) {
    return EvaluateUnaryExp(*mul->unary_exp, block, reg_counter);
  } else {
    auto lhs_val = EvaluateMulExp(*mul->lhs, block, reg_counter);
    auto rhs_val = EvaluateUnaryExp(*mul->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

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

static unique_ptr<Value> EvaluateAddExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter) {
  const auto *add = dynamic_cast<const AddExpAST*>(&ast);
  if (!add) return nullptr;

  if (add->is_mul) {
    return EvaluateMulExp(*add->mul_exp, block, reg_counter);
  } else {
    auto lhs_val = EvaluateAddExp(*add->lhs, block, reg_counter);
    auto rhs_val = EvaluateMulExp(*add->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

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

static unique_ptr<Value> EvaluateRelExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter) {
  const auto *rel = dynamic_cast<const RelExpAST*>(&ast);
  if (!rel) return nullptr;

  if (rel->is_add) {
    return EvaluateAddExp(*rel->add_exp, block, reg_counter);
  } else {
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

static unique_ptr<Value> EvaluateEqExp(const BaseAST &ast,
                                       BasicBlock &block, int &reg_counter) {
  const auto *eq = dynamic_cast<const EqExpAST*>(&ast);
  if (!eq) return nullptr;

  if (eq->is_rel) {
    return EvaluateRelExp(*eq->rel_exp, block, reg_counter);
  } else {
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

static unique_ptr<Value> EvaluateLAndExp(const BaseAST &ast,
                                         BasicBlock &block, int &reg_counter) {
  const auto *land = dynamic_cast<const LAndExpAST*>(&ast);
  if (!land) return nullptr;

  if (land->is_eq) {
    return EvaluateEqExp(*land->eq_exp, block, reg_counter);
  } else {
    auto lhs_val = EvaluateLAndExp(*land->lhs, block, reg_counter);
    auto rhs_val = EvaluateEqExp(*land->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

    int t1 = reg_counter++;
    auto inst1 = make_unique<BinaryInst>(
        t1, "ne", std::move(lhs_val), make_unique<Integer>(0));
    block.instructions.push_back(std::move(inst1));
    int t2 = reg_counter++;
    auto inst2 = make_unique<BinaryInst>(
        t2, "ne", std::move(rhs_val), make_unique<Integer>(0));
    block.instructions.push_back(std::move(inst2));
    int dest = reg_counter++;
    auto inst3 = make_unique<BinaryInst>(
        dest, "and", make_unique<RegRef>(t1), make_unique<RegRef>(t2));
    block.instructions.push_back(std::move(inst3));
    return make_unique<RegRef>(dest);
  }
}

static unique_ptr<Value> EvaluateLOrExp(const BaseAST &ast,
                                        BasicBlock &block, int &reg_counter) {
  const auto *lor = dynamic_cast<const LOrExpAST*>(&ast);
  if (!lor) return nullptr;

  if (lor->is_land) {
    return EvaluateLAndExp(*lor->land_exp, block, reg_counter);
  } else {
    auto lhs_val = EvaluateLOrExp(*lor->lhs, block, reg_counter);
    auto rhs_val = EvaluateLAndExp(*lor->rhs, block, reg_counter);
    if (!lhs_val || !rhs_val) return nullptr;

    int t1 = reg_counter++;
    auto inst1 = make_unique<BinaryInst>(
        t1, "ne", std::move(lhs_val), make_unique<Integer>(0));
    block.instructions.push_back(std::move(inst1));
    int t2 = reg_counter++;
    auto inst2 = make_unique<BinaryInst>(
        t2, "ne", std::move(rhs_val), make_unique<Integer>(0));
    block.instructions.push_back(std::move(inst2));
    int dest = reg_counter++;
    auto inst3 = make_unique<BinaryInst>(
        dest, "or", make_unique<RegRef>(t1), make_unique<RegRef>(t2));
    block.instructions.push_back(std::move(inst3));
    return make_unique<RegRef>(dest);
  }
}

static unique_ptr<Value> EvaluateExp(const BaseAST &ast,
                                     BasicBlock &block, int &reg_counter) {
  const auto *exp = dynamic_cast<const ExpAST*>(&ast);
  if (!exp) return nullptr;
  return EvaluateLOrExp(*exp->lor_exp, block, reg_counter);
}

// ==================== 处理声明 ====================

static void ProcessConstDecl(const BaseAST &ast, BasicBlock &block, int &reg_counter) {
  const auto *decl = dynamic_cast<const DeclAST*>(&ast);
  if (!decl || !decl->is_const) return;

  const auto *const_decl = dynamic_cast<const ConstDeclAST*>(decl->decl_body.get());
  if (!const_decl) return;

  for (auto &def : const_decl->const_defs) {
    const auto *const_def = dynamic_cast<const ConstDefAST*>(def.get());
    if (!const_def) continue;

    const auto *init = dynamic_cast<const ConstInitValAST*>(const_def->init_val.get());
    if (!init) continue;

    int val = EvaluateConstExp(*init->exp);
    AddConst(const_def->ident, val);
  }
}

static void ProcessVarDecl(const BaseAST &ast, BasicBlock &block, int &reg_counter) {
  const auto *decl = dynamic_cast<const DeclAST*>(&ast);
  if (!decl || decl->is_const) return;

  const auto *var_decl = dynamic_cast<const VarDeclAST*>(decl->decl_body.get());
  if (!var_decl) return;

  for (auto &def : var_decl->var_defs) {
    const auto *var_def = dynamic_cast<const VarDefAST*>(def.get());
    if (!var_def) continue;

    AddVar(var_def->ident, block, reg_counter);

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

// ==================== 处理语句 ====================

// 处理赋值: LVal = Exp
static void ProcessAssign(const BaseAST &ast, BasicBlock &block, int &reg_counter) {
  const auto *stmt = dynamic_cast<const StmtAST*>(&ast);
  if (!stmt || stmt->kind != StmtAST::ASSIGN) return;

  const auto *lval = dynamic_cast<const LValAST*>(stmt->lval.get());
  if (!lval) return;

  auto *info = Lookup(lval->ident);
  if (!info) {
    cerr << "error: undefined symbol " << lval->ident << endl;
    return;
  }
  if (info->is_const) {
    cerr << "error: cannot assign to constant " << lval->ident << endl;
    return;
  }

  auto val = EvaluateExp(*stmt->assign_exp, block, reg_counter);
  if (!val) return;

  block.instructions.push_back(
      make_unique<StoreInst>(std::move(val), info->alloc_name));
}

// 递归处理 Block (处理所有 BlockItem)，支持嵌套作用域
static void ProcessBlock(const BaseAST &ast, BasicBlock &block, int &reg_counter);

static void ProcessBlock(const BaseAST &ast, BasicBlock &block, int &reg_counter) {
  const auto *block_ast = dynamic_cast<const BlockAST*>(&ast);
  if (!block_ast) return;

  // 进入新的作用域
  EnterScope();

  for (auto &item : block_ast->items) {
    const auto *block_item = dynamic_cast<const BlockItemAST*>(item.get());
    if (!block_item) continue;

    if (block_item->is_stmt) {
      const auto *stmt = dynamic_cast<const StmtAST*>(block_item->item.get());
      if (!stmt) continue;

      switch (stmt->kind) {
      case StmtAST::RETURN: {
        auto ret_val = EvaluateExp(*stmt->exp, block, reg_counter);
        if (ret_val) {
          auto ret = make_unique<Return>(std::move(ret_val));
          block.instructions.push_back(std::move(ret));
        }
        break;
      }
      case StmtAST::ASSIGN: {
        ProcessAssign(*stmt, block, reg_counter);
        break;
      }
      case StmtAST::EXP_STMT: {
        // 表达式语句: 求值并丢弃结果 (nullptr 表示空语句)
        if (stmt->exp) {
          EvaluateExp(*stmt->exp, block, reg_counter);
        }
        break;
      }
      case StmtAST::BLOCK: {
        // 嵌套 Block: 递归处理, 自动进入/退出作用域
        ProcessBlock(*stmt->block, block, reg_counter);
        break;
      }
      }
    } else {
      const auto *decl = dynamic_cast<const DeclAST*>(block_item->item.get());
      if (!decl) continue;

      if (decl->is_const) {
        ProcessConstDecl(*decl, block, reg_counter);
      } else {
        ProcessVarDecl(*decl, block, reg_counter);
      }
    }
  }

  // 退出当前作用域
  ExitScope();
}

// ==================== 主入口 ====================

unique_ptr<Program> GenerateIR(const BaseAST &ast) {
  auto program = make_unique<Program>();

  // 清空作用域栈 (ProcessBlock 会创建函数级作用域)
  scope_stack.clear();
  alloc_counter = 0;

  const auto *comp_unit = dynamic_cast<const CompUnitAST*>(&ast);
  if (!comp_unit) {
    return program;
  }

  const auto *func_def = dynamic_cast<const FuncDefAST*>(comp_unit->func_def.get());
  if (!func_def) {
    return program;
  }

  auto func = make_unique<Function>(func_def->ident);
  auto entry_block = make_unique<BasicBlock>("entry");

  int reg_counter = 0;
  ProcessBlock(*func_def->block, *entry_block, reg_counter);

  func->blocks.push_back(std::move(entry_block));
  program->functions.push_back(std::move(func));

  return program;
}
