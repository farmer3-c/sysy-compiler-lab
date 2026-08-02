#include "IRGenerator.h"
#include <unordered_map>
#include <vector>
#include <iostream>

using namespace std;

// ==================== 符号表 ====================

struct SymbolInfo {
  bool is_const;
  int const_val;
  string alloc_name;       // 局部变量: "@name_N"; 全局变量: "@name"
  bool is_func;            // true: 函数符号
  string func_ret_type;   // "i32" 或 "void"
  int func_param_count;   // 形参个数
  bool is_global;         // true: 全局变量 (alloc 在 Program 级别)
};

static vector<unordered_map<string, SymbolInfo>> scope_stack;

// 当前正在构建的 Program、Function 等上下文
static Program *current_program = nullptr;
static Function *current_func = nullptr;
static BasicBlock *entry_block = nullptr;  // 入口基本块, alloc 指令必须在此
static int alloc_counter = 0;
static int bb_counter = 0;

// 循环上下文栈
static vector<string> break_targets;
static vector<string> continue_targets;

// ==================== 作用域管理 ====================

static void EnterScope() { scope_stack.emplace_back(); }
static void ExitScope() { if (!scope_stack.empty()) scope_stack.pop_back(); }

static void AddConst(const string &name, int val) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) { cerr << "error: duplicate symbol " << name << endl; return; }
  cur[name] = {true, val, "", false, "", 0, false};
}

static bool IsTerminated(const BasicBlock &bb);

// 添加变量: 局部变量在 entry_block 中生成 alloc 指令
// 全局变量在 Program 中生成 global alloc
static void AddVar(const string &name, BasicBlock * /*block*/, int &reg_counter) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) { cerr << "error: duplicate symbol " << name << endl; return; }

  // 判断是否在全局作用域 (仅全局作用域一层, 即 scope_stack.size() == 1)
  bool is_global = (scope_stack.size() == 1 &&
                    current_func == nullptr);

  if (is_global) {
    // 全局变量: 生成 global alloc, 放在 Program 中
    string global_name = "@" + name;
    cur[name] = {false, 0, global_name, false, "", 0, true};
    // 默认初始化为 zeroinit
    current_program->global_allocs.push_back(
        make_unique<GlobalAllocInst>(global_name, make_unique<ZeroInit>()));
  } else {
    // 局部变量: alloc 在 entry_block 中
    string alloc_name = "@" + name + "_" + to_string(alloc_counter++);
    cur[name] = {false, 0, alloc_name, false, "", 0, false};
    auto &insts = entry_block->instructions;
    if (!insts.empty() && IsTerminated(*entry_block)) {
      insts.insert(insts.end() - 1, make_unique<AllocInst>(alloc_name));
    } else {
      insts.push_back(make_unique<AllocInst>(alloc_name));
    }
  }
}

// 添加函数符号到当前作用域
static void AddFunc(const string &name, const string &ret_type, int param_count) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) { cerr << "error: duplicate symbol " << name << endl; return; }
  cur[name] = {false, 0, "", true, ret_type, param_count, false};
}

static SymbolInfo *Lookup(const string &name) {
  for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return &found->second;
  }
  return nullptr;
}

// 创建新的基本块并加入当前函数
static BasicBlock *NewBB(const string &name) {
  auto bb = make_unique<BasicBlock>(name);
  auto *ptr = bb.get();
  current_func->blocks.push_back(std::move(bb));
  return ptr;
}

static bool IsTerminated(const BasicBlock &bb) {
  if (bb.instructions.empty()) return false;
  auto *last = bb.instructions.back().get();
  return dynamic_cast<const Return*>(last) ||
         dynamic_cast<const BranchInst*>(last) ||
         dynamic_cast<const JumpInst*>(last);
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
  if (primary->is_number) return primary->number;
  if (primary->is_lval) {
    auto *info = Lookup(primary->ident);
    if (!info) { cerr << "error: undefined symbol " << primary->ident << endl; return 0; }
    if (!info->is_const) { cerr << "error: " << primary->ident << " is not a constant" << endl; return 0; }
    return info->const_val;
  }
  return EvaluateConstExp(*primary->exp);
}

static int EvaluateConstUnaryExp(const BaseAST &ast) {
  const auto *unary = dynamic_cast<const UnaryExpAST*>(&ast);
  if (!unary) return 0;
  if (unary->kind == UnaryExpAST::PRIMARY) return EvaluateConstPrimaryExp(*unary->primary_exp);
  // 函数调用和一元运算不能在常量中出现
  if (unary->kind == UnaryExpAST::CALL) {
    cerr << "error: function call in constant expression" << endl; return 0;
  }
  int val = EvaluateConstUnaryExp(*unary->unary_exp);
  if (unary->op == "+") return val;
  if (unary->op == "-") return -val;
  if (unary->op == "!") return (val == 0) ? 1 : 0;
  return 0;
}

static int EvaluateConstMulExp(const BaseAST &ast) {
  const auto *mul = dynamic_cast<const MulExpAST*>(&ast);
  if (!mul) return 0;
  if (mul->is_unary) return EvaluateConstUnaryExp(*mul->unary_exp);
  int lhs = EvaluateConstMulExp(*mul->lhs);
  int rhs = EvaluateConstUnaryExp(*mul->rhs);
  if (mul->op == "*") return lhs * rhs;
  if (mul->op == "/") return lhs / rhs;
  if (mul->op == "%") return lhs % rhs;
  return 0;
}

static int EvaluateConstAddExp(const BaseAST &ast) {
  const auto *add = dynamic_cast<const AddExpAST*>(&ast);
  if (!add) return 0;
  if (add->is_mul) return EvaluateConstMulExp(*add->mul_exp);
  int lhs = EvaluateConstAddExp(*add->lhs);
  int rhs = EvaluateConstMulExp(*add->rhs);
  if (add->op == "+") return lhs + rhs;
  if (add->op == "-") return lhs - rhs;
  return 0;
}

static int EvaluateConstRelExp(const BaseAST &ast) {
  const auto *rel = dynamic_cast<const RelExpAST*>(&ast);
  if (!rel) return 0;
  if (rel->is_add) return EvaluateConstAddExp(*rel->add_exp);
  int lhs = EvaluateConstRelExp(*rel->lhs);
  int rhs = EvaluateConstAddExp(*rel->rhs);
  if (rel->op == "<") return (lhs < rhs) ? 1 : 0;
  if (rel->op == ">") return (lhs > rhs) ? 1 : 0;
  if (rel->op == "<=") return (lhs <= rhs) ? 1 : 0;
  if (rel->op == ">=") return (lhs >= rhs) ? 1 : 0;
  return 0;
}

static int EvaluateConstEqExp(const BaseAST &ast) {
  const auto *eq = dynamic_cast<const EqExpAST*>(&ast);
  if (!eq) return 0;
  if (eq->is_rel) return EvaluateConstRelExp(*eq->rel_exp);
  int lhs = EvaluateConstEqExp(*eq->lhs);
  int rhs = EvaluateConstRelExp(*eq->rhs);
  if (eq->op == "==") return (lhs == rhs) ? 1 : 0;
  if (eq->op == "!=") return (lhs != rhs) ? 1 : 0;
  return 0;
}

static int EvaluateConstLAndExp(const BaseAST &ast) {
  const auto *land = dynamic_cast<const LAndExpAST*>(&ast);
  if (!land) return 0;
  if (land->is_eq) return EvaluateConstEqExp(*land->eq_exp);
  int lhs = EvaluateConstLAndExp(*land->lhs);
  int rhs = EvaluateConstEqExp(*land->rhs);
  return ((lhs != 0) && (rhs != 0)) ? 1 : 0;
}

static int EvaluateConstLOrExp(const BaseAST &ast) {
  const auto *lor = dynamic_cast<const LOrExpAST*>(&ast);
  if (!lor) return 0;
  if (lor->is_land) return EvaluateConstLAndExp(*lor->land_exp);
  int lhs = EvaluateConstLOrExp(*lor->lhs);
  int rhs = EvaluateConstLAndExp(*lor->rhs);
  return ((lhs != 0) || (rhs != 0)) ? 1 : 0;
}

static int EvaluateConstExp(const BaseAST &ast) {
  const auto *exp = dynamic_cast<const ExpAST*>(&ast);
  if (!exp) return 0;
  return EvaluateConstLOrExp(*exp->lor_exp);
}

// ==================== IR 生成: 表达式求值 ====================

static unique_ptr<Value> EvaluateExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);
static unique_ptr<Value> EvaluateLOrExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);
static unique_ptr<Value> EvaluateLAndExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);
static unique_ptr<Value> EvaluateEqExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);
static unique_ptr<Value> EvaluateRelExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);
static unique_ptr<Value> EvaluateAddExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);
static unique_ptr<Value> EvaluateMulExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);
static unique_ptr<Value> EvaluateUnaryExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);
static unique_ptr<Value> EvaluatePrimaryExp(const BaseAST &ast, BasicBlock *&block, int &reg_counter);

static unique_ptr<Value> EvaluateUnaryExp(const BaseAST &ast,
                                          BasicBlock *&block, int &reg_counter) {
  const auto *unary = dynamic_cast<const UnaryExpAST*>(&ast);
  if (!unary) return nullptr;

  if (unary->kind == UnaryExpAST::PRIMARY) {
    return EvaluatePrimaryExp(*unary->primary_exp, block, reg_counter);
  }

  if (unary->kind == UnaryExpAST::CALL) {
    // 函数调用: %dest = call @func(args)  或  call @func(args) (void)
    auto *func_info = Lookup(unary->func_name);
    if (!func_info || !func_info->is_func) {
      cerr << "error: undefined function " << unary->func_name << endl;
      return make_unique<Integer>(0);
    }

    // 求值实参
    vector<unique_ptr<Value>> args;
    for (auto &arg : unary->args) {
      const auto *arg_exp = dynamic_cast<const ExpAST*>(arg.get());
      if (arg_exp) {
        auto val = EvaluateExp(*arg_exp, block, reg_counter);
        if (val) args.push_back(std::move(val));
      }
    }

    string func_ref = "@" + unary->func_name;
    if (func_info->func_ret_type == "void") {
      // void 调用: 无返回值
      block->instructions.push_back(
          make_unique<CallInst>(-1, func_ref, std::move(args)));
      return nullptr;  // void 调用无值
    } else {
      // int 调用: 有返回值
      int dest = reg_counter++;
      block->instructions.push_back(
          make_unique<CallInst>(dest, func_ref, std::move(args)));
      return make_unique<RegRef>(dest);
    }
  }

  // UNARY_OP
  if (unary->kind == UnaryExpAST::UNARY_OP) {
    auto operand = EvaluateUnaryExp(*unary->unary_exp, block, reg_counter);
    if (!operand) return nullptr;
    if (unary->op == "+") return operand;
    if (unary->op == "-") {
      int dest = reg_counter++;
      block->instructions.push_back(
          make_unique<BinaryInst>(dest, "sub", make_unique<Integer>(0), std::move(operand)));
      return make_unique<RegRef>(dest);
    }
    if (unary->op == "!") {
      int dest = reg_counter++;
      block->instructions.push_back(
          make_unique<BinaryInst>(dest, "eq", std::move(operand), make_unique<Integer>(0)));
      return make_unique<RegRef>(dest);
    }
  }

  return nullptr;
}

static unique_ptr<Value> EvaluatePrimaryExp(const BaseAST &ast,
                                            BasicBlock *&block, int &reg_counter) {
  const auto *primary = dynamic_cast<const PrimaryExpAST*>(&ast);
  if (!primary) return nullptr;
  if (primary->is_number) return make_unique<Integer>(primary->number);
  if (primary->is_lval) {
    auto *info = Lookup(primary->ident);
    if (!info) { cerr << "error: undefined symbol " << primary->ident << endl; return make_unique<Integer>(0); }
    if (info->is_const) return make_unique<Integer>(info->const_val);
    int dest = reg_counter++;
    block->instructions.push_back(make_unique<LoadInst>(dest, info->alloc_name));
    return make_unique<RegRef>(dest);
  }
  return EvaluateExp(*primary->exp, block, reg_counter);
}

static unique_ptr<Value> EvaluateMulExp(const BaseAST &ast,
                                        BasicBlock *&block, int &reg_counter) {
  const auto *mul = dynamic_cast<const MulExpAST*>(&ast);
  if (!mul) return nullptr;
  if (mul->is_unary) return EvaluateUnaryExp(*mul->unary_exp, block, reg_counter);
  auto lhs = EvaluateMulExp(*mul->lhs, block, reg_counter);
  auto rhs = EvaluateUnaryExp(*mul->rhs, block, reg_counter);
  if (!lhs || !rhs) return nullptr;
  string op;
  if (mul->op == "*") op = "mul"; else if (mul->op == "/") op = "div"; else op = "mod";
  int dest = reg_counter++;
  block->instructions.push_back(make_unique<BinaryInst>(dest, op, std::move(lhs), std::move(rhs)));
  return make_unique<RegRef>(dest);
}

static unique_ptr<Value> EvaluateAddExp(const BaseAST &ast,
                                        BasicBlock *&block, int &reg_counter) {
  const auto *add = dynamic_cast<const AddExpAST*>(&ast);
  if (!add) return nullptr;
  if (add->is_mul) return EvaluateMulExp(*add->mul_exp, block, reg_counter);
  auto lhs = EvaluateAddExp(*add->lhs, block, reg_counter);
  auto rhs = EvaluateMulExp(*add->rhs, block, reg_counter);
  if (!lhs || !rhs) return nullptr;
  string op = (add->op == "+") ? "add" : "sub";
  int dest = reg_counter++;
  block->instructions.push_back(make_unique<BinaryInst>(dest, op, std::move(lhs), std::move(rhs)));
  return make_unique<RegRef>(dest);
}

static unique_ptr<Value> EvaluateRelExp(const BaseAST &ast,
                                        BasicBlock *&block, int &reg_counter) {
  const auto *rel = dynamic_cast<const RelExpAST*>(&ast);
  if (!rel) return nullptr;
  if (rel->is_add) return EvaluateAddExp(*rel->add_exp, block, reg_counter);
  auto lhs = EvaluateRelExp(*rel->lhs, block, reg_counter);
  auto rhs = EvaluateAddExp(*rel->rhs, block, reg_counter);
  if (!lhs || !rhs) return nullptr;
  string op;
  if (rel->op == "<") op = "lt"; else if (rel->op == ">") op = "gt";
  else if (rel->op == "<=") op = "le"; else op = "ge";
  int dest = reg_counter++;
  block->instructions.push_back(make_unique<BinaryInst>(dest, op, std::move(lhs), std::move(rhs)));
  return make_unique<RegRef>(dest);
}

static unique_ptr<Value> EvaluateEqExp(const BaseAST &ast,
                                       BasicBlock *&block, int &reg_counter) {
  const auto *eq = dynamic_cast<const EqExpAST*>(&ast);
  if (!eq) return nullptr;
  if (eq->is_rel) return EvaluateRelExp(*eq->rel_exp, block, reg_counter);
  auto lhs = EvaluateEqExp(*eq->lhs, block, reg_counter);
  auto rhs = EvaluateRelExp(*eq->rhs, block, reg_counter);
  if (!lhs || !rhs) return nullptr;
  string op = (eq->op == "==") ? "eq" : "ne";
  int dest = reg_counter++;
  block->instructions.push_back(make_unique<BinaryInst>(dest, op, std::move(lhs), std::move(rhs)));
  return make_unique<RegRef>(dest);
}

// 短路求值 &&
static unique_ptr<Value> EvaluateLAndExp(const BaseAST &ast,
                                         BasicBlock *&block, int &reg_counter) {
  const auto *land = dynamic_cast<const LAndExpAST*>(&ast);
  if (!land) return nullptr;
  if (land->is_eq) return EvaluateEqExp(*land->eq_exp, block, reg_counter);

  string sc_alloc = "@sc_and_" + to_string(alloc_counter++);
  {
    auto &entry_insts = entry_block->instructions;
    if (!entry_insts.empty() && IsTerminated(*entry_block))
      entry_insts.insert(entry_insts.end() - 1, make_unique<AllocInst>(sc_alloc));
    else
      entry_insts.push_back(make_unique<AllocInst>(sc_alloc));
  }
  block->instructions.push_back(make_unique<StoreInst>(make_unique<Integer>(0), sc_alloc));

  auto lhs_val = EvaluateLAndExp(*land->lhs, block, reg_counter);
  if (!lhs_val) return nullptr;

  string rhs_name = "sc_and_rhs_" + to_string(bb_counter++);
  string end_name = "sc_and_end_" + to_string(bb_counter++);
  auto *rhs_bb = NewBB(rhs_name);
  auto *end_bb = NewBB(end_name);

  block->instructions.push_back(make_unique<BranchInst>(std::move(lhs_val), rhs_name, end_name));

  auto rhs_val = EvaluateEqExp(*land->rhs, rhs_bb, reg_counter);
  if (rhs_val) {
    int r = reg_counter++;
    rhs_bb->instructions.push_back(make_unique<BinaryInst>(r, "ne", std::move(rhs_val), make_unique<Integer>(0)));
    rhs_bb->instructions.push_back(make_unique<StoreInst>(make_unique<RegRef>(r), sc_alloc));
  }
  rhs_bb->instructions.push_back(make_unique<JumpInst>(end_name));

  int result = reg_counter++;
  end_bb->instructions.push_back(make_unique<LoadInst>(result, sc_alloc));
  block = end_bb;
  return make_unique<RegRef>(result);
}

// 短路求值 ||
static unique_ptr<Value> EvaluateLOrExp(const BaseAST &ast,
                                        BasicBlock *&block, int &reg_counter) {
  const auto *lor = dynamic_cast<const LOrExpAST*>(&ast);
  if (!lor) return nullptr;
  if (lor->is_land) return EvaluateLAndExp(*lor->land_exp, block, reg_counter);

  string sc_alloc = "@sc_or_" + to_string(alloc_counter++);
  {
    auto &entry_insts = entry_block->instructions;
    if (!entry_insts.empty() && IsTerminated(*entry_block))
      entry_insts.insert(entry_insts.end() - 1, make_unique<AllocInst>(sc_alloc));
    else
      entry_insts.push_back(make_unique<AllocInst>(sc_alloc));
  }
  block->instructions.push_back(make_unique<StoreInst>(make_unique<Integer>(1), sc_alloc));

  auto lhs_val = EvaluateLOrExp(*lor->lhs, block, reg_counter);
  if (!lhs_val) return nullptr;

  string rhs_name = "sc_or_rhs_" + to_string(bb_counter++);
  string end_name = "sc_or_end_" + to_string(bb_counter++);
  auto *rhs_bb = NewBB(rhs_name);
  auto *end_bb = NewBB(end_name);

  block->instructions.push_back(make_unique<BranchInst>(std::move(lhs_val), end_name, rhs_name));

  auto rhs_val = EvaluateLAndExp(*lor->rhs, rhs_bb, reg_counter);
  if (rhs_val) {
    int r = reg_counter++;
    rhs_bb->instructions.push_back(make_unique<BinaryInst>(r, "ne", std::move(rhs_val), make_unique<Integer>(0)));
    rhs_bb->instructions.push_back(make_unique<StoreInst>(make_unique<RegRef>(r), sc_alloc));
  }
  rhs_bb->instructions.push_back(make_unique<JumpInst>(end_name));

  int result = reg_counter++;
  end_bb->instructions.push_back(make_unique<LoadInst>(result, sc_alloc));
  block = end_bb;
  return make_unique<RegRef>(result);
}

static unique_ptr<Value> EvaluateExp(const BaseAST &ast,
                                     BasicBlock *&block, int &reg_counter) {
  const auto *exp = dynamic_cast<const ExpAST*>(&ast);
  if (!exp) return nullptr;
  return EvaluateLOrExp(*exp->lor_exp, block, reg_counter);
}

// ==================== 处理声明 ====================

static void ProcessConstDecl(const BaseAST &ast, BasicBlock *, int &) {
  const auto *decl = dynamic_cast<const DeclAST*>(&ast);
  if (!decl || !decl->is_const) return;
  const auto *const_decl = dynamic_cast<const ConstDeclAST*>(decl->decl_body.get());
  if (!const_decl) return;
  for (auto &def : const_decl->const_defs) {
    const auto *const_def = dynamic_cast<const ConstDefAST*>(def.get());
    if (!const_def) continue;
    const auto *init = dynamic_cast<const ConstInitValAST*>(const_def->init_val.get());
    if (!init) continue;
    AddConst(const_def->ident, EvaluateConstExp(*init->exp));
  }
}

static void ProcessVarDecl(const BaseAST &ast, BasicBlock *&block, int &reg_counter) {
  const auto *decl = dynamic_cast<const DeclAST*>(&ast);
  if (!decl || decl->is_const) return;
  const auto *var_decl = dynamic_cast<const VarDeclAST*>(decl->decl_body.get());
  if (!var_decl) return;

  bool is_global = (scope_stack.size() == 1 && current_func == nullptr);

  for (auto &def : var_decl->var_defs) {
    const auto *var_def = dynamic_cast<const VarDefAST*>(def.get());
    if (!var_def) continue;
    AddVar(var_def->ident, block, reg_counter);

    if (var_def->has_init) {
      const auto *init_val = dynamic_cast<const InitValAST*>(var_def->init_val.get());
      if (!init_val) continue;
      auto *info = Lookup(var_def->ident);
      if (!info) continue;

      unique_ptr<Value> val;
      if (is_global) {
        // 全局变量初始值必须是常量
        int const_init = EvaluateConstExp(*init_val->exp);
        val = make_unique<Integer>(const_init);
      } else {
        val = EvaluateExp(*init_val->exp, block, reg_counter);
      }

      if (val && info) {
        if (is_global) {
          // 更新全局 alloc 的初始值 (替换默认的 zeroinit)
          for (auto &ga : current_program->global_allocs) {
            auto *g = dynamic_cast<GlobalAllocInst*>(ga.get());
            if (g && g->name == info->alloc_name) {
              g->init_val = std::move(val);
              break;
            }
          }
        } else {
          block->instructions.push_back(
              make_unique<StoreInst>(std::move(val), info->alloc_name));
        }
      }
    }
  }
}

// ==================== 处理语句 ====================

static void ProcessAssign(const BaseAST &ast, BasicBlock *&block, int &reg_counter) {
  const auto *stmt = dynamic_cast<const StmtAST*>(&ast);
  if (!stmt || stmt->kind != StmtAST::ASSIGN) return;
  const auto *lval = dynamic_cast<const LValAST*>(stmt->lval.get());
  if (!lval) return;
  auto *info = Lookup(lval->ident);
  if (!info) { cerr << "error: undefined symbol " << lval->ident << endl; return; }
  if (info->is_const) { cerr << "error: cannot assign to constant " << lval->ident << endl; return; }
  auto val = EvaluateExp(*stmt->assign_exp, block, reg_counter);
  if (val) block->instructions.push_back(make_unique<StoreInst>(std::move(val), info->alloc_name));
}

static BasicBlock *ProcessStmt(const StmtAST &stmt, BasicBlock *cur, int &reg_counter);

static BasicBlock *ProcessBlockItems(const BaseAST &ast, BasicBlock *cur, int &reg_counter) {
  const auto *block_ast = dynamic_cast<const BlockAST*>(&ast);
  if (!block_ast) return cur;

  EnterScope();

  for (auto &item : block_ast->items) {
    if (!cur) break;
    const auto *block_item = dynamic_cast<const BlockItemAST*>(item.get());
    if (!block_item) continue;

    if (block_item->is_stmt) {
      const auto *stmt = dynamic_cast<const StmtAST*>(block_item->item.get());
      if (stmt) cur = ProcessStmt(*stmt, cur, reg_counter);
    } else {
      const auto *decl = dynamic_cast<const DeclAST*>(block_item->item.get());
      if (!decl) continue;
      if (decl->is_const) ProcessConstDecl(*decl, cur, reg_counter);
      else ProcessVarDecl(*decl, cur, reg_counter);
    }
  }

  ExitScope();
  return cur;
}

static BasicBlock *ProcessStmt(const StmtAST &stmt, BasicBlock *cur, int &reg_counter) {
  switch (stmt.kind) {
  case StmtAST::RETURN: {
    if (stmt.has_ret_val) {
      auto ret_val = EvaluateExp(*stmt.exp, cur, reg_counter);
      if (ret_val) cur->instructions.push_back(make_unique<Return>(std::move(ret_val)));
    } else {
      cur->instructions.push_back(make_unique<Return>(nullptr));
    }
    return nullptr;
  }
  case StmtAST::ASSIGN: {
    ProcessAssign(stmt, cur, reg_counter);
    return cur;
  }
  case StmtAST::EXP_STMT: {
    if (stmt.exp) {
      auto *exp = dynamic_cast<const ExpAST*>(stmt.exp.get());
      if (exp) {
        // 表达式语句: 求值但丢弃结果 (仅对有副作用的调用有用)
        auto val = EvaluateExp(*stmt.exp, cur, reg_counter);
        (void)val;  // 丢弃结果
      }
    }
    return cur;
  }
  case StmtAST::BLOCK: {
    return ProcessBlockItems(*stmt.block, cur, reg_counter);
  }
  case StmtAST::IF_ELSE: {
    auto cond = EvaluateExp(*stmt.exp, cur, reg_counter);
    if (!cond) return cur;

    string then_name = "then_" + to_string(bb_counter++);
    string else_name = "else_" + to_string(bb_counter++);
    string end_name = "end_" + to_string(bb_counter++);

    auto *then_bb = NewBB(then_name);
    auto *end_bb = NewBB(end_name);

    if (stmt.else_stmt) {
      auto *else_bb = NewBB(else_name);
      cur->instructions.push_back(make_unique<BranchInst>(std::move(cond), then_name, else_name));

      auto *then_cont = ProcessStmt(*dynamic_cast<const StmtAST*>(stmt.then_stmt.get()), then_bb, reg_counter);
      if (then_cont && !IsTerminated(*then_cont))
        then_cont->instructions.push_back(make_unique<JumpInst>(end_name));

      auto *else_cont = ProcessStmt(*dynamic_cast<const StmtAST*>(stmt.else_stmt.get()), else_bb, reg_counter);
      if (else_cont && !IsTerminated(*else_cont))
        else_cont->instructions.push_back(make_unique<JumpInst>(end_name));
    } else {
      cur->instructions.push_back(make_unique<BranchInst>(std::move(cond), then_name, end_name));

      auto *then_cont = ProcessStmt(*dynamic_cast<const StmtAST*>(stmt.then_stmt.get()), then_bb, reg_counter);
      if (then_cont && !IsTerminated(*then_cont))
        then_cont->instructions.push_back(make_unique<JumpInst>(end_name));
    }

    return end_bb;
  }
  case StmtAST::WHILE: {
    string cond_name = "while_cond_" + to_string(bb_counter++);
    string body_name = "while_body_" + to_string(bb_counter++);
    string end_name  = "while_end_"  + to_string(bb_counter++);

    auto *cond_bb = NewBB(cond_name);
    auto *body_bb = NewBB(body_name);
    auto *end_bb  = NewBB(end_name);

    if (!IsTerminated(*cur))
      cur->instructions.push_back(make_unique<JumpInst>(cond_name));

    auto cond = EvaluateExp(*stmt.exp, cond_bb, reg_counter);
    if (cond) cond_bb->instructions.push_back(make_unique<BranchInst>(std::move(cond), body_name, end_name));

    break_targets.push_back(end_name);
    continue_targets.push_back(cond_name);

    const auto *body_stmt = dynamic_cast<const StmtAST*>(stmt.body.get());
    if (body_stmt) {
      auto *body_cont = ProcessStmt(*body_stmt, body_bb, reg_counter);
      if (body_cont && !IsTerminated(*body_cont))
        body_cont->instructions.push_back(make_unique<JumpInst>(cond_name));
    }

    break_targets.pop_back();
    continue_targets.pop_back();

    return end_bb;
  }
  case StmtAST::BREAK: {
    if (break_targets.empty()) {
      cerr << "error: break statement outside of a loop" << endl;
      return cur;
    }
    cur->instructions.push_back(make_unique<JumpInst>(break_targets.back()));
    return nullptr;
  }
  case StmtAST::CONTINUE: {
    if (continue_targets.empty()) {
      cerr << "error: continue statement outside of a loop" << endl;
      return cur;
    }
    cur->instructions.push_back(make_unique<JumpInst>(continue_targets.back()));
    return nullptr;
  }
  }
  return cur;
}

// ==================== 库函数声明 ====================

static void AddLibraryFunctions(Program &program) {
  // 添加库函数到全局符号表 (在 GenerateIR 中 scope_stack[0] 已存在)
  auto &global = scope_stack[0];

  global["getint"]    = {false, 0, "", true, "i32", 0, false};
  global["getch"]     = {false, 0, "", true, "i32", 0, false};
  global["getarray"]  = {false, 0, "", true, "i32", 1, false};
  global["putint"]    = {false, 0, "", true, "void", 1, false};
  global["putch"]     = {false, 0, "", true, "void", 1, false};
  global["putarray"]  = {false, 0, "", true, "void", 2, false};
  global["starttime"] = {false, 0, "", true, "void", 0, false};
  global["stoptime"]  = {false, 0, "", true, "void", 0, false};

  // 生成 Koopa IR 中的 decl 语句
  auto make_decl = [&](const string &name, const vector<string> &param_types, const string &ret) {
    auto decl = make_unique<Function>("@" + name);
    decl->is_decl = true;
    for (auto &pt : param_types) {
      decl->params.push_back({"", pt});  // decl 中参数不需要名字
    }
    decl->ret_type = ret;
    program.declarations.push_back(std::move(decl));
  };

  make_decl("getint",    {},           "i32");
  make_decl("getch",     {},           "i32");
  make_decl("getarray",  {"*i32"},     "i32");
  make_decl("putint",    {"i32"},      "");
  make_decl("putch",     {"i32"},      "");
  make_decl("putarray",  {"i32", "*i32"}, "");
  make_decl("starttime", {},           "");
  make_decl("stoptime",  {},           "");
}

// ==================== 处理单个函数定义 ====================

static void ProcessFuncDef(const BaseAST &ast) {
  const auto *func_def = dynamic_cast<const FuncDefAST*>(&ast);
  if (!func_def) return;

  const auto *func_type = dynamic_cast<const FuncTypeAST*>(func_def->func_type.get());
  if (!func_type) return;

  string ret_type = func_type->type;  // "int" or "void"
  string koopa_ret = (ret_type == "int") ? "i32" : "";

  // 添加函数到全局作用域
  int param_count = func_def->has_params ? (int)func_def->params.size() : 0;
  AddFunc(func_def->ident, ret_type, param_count);

  // 创建函数
  auto func = make_unique<Function>("@" + func_def->ident);
  func->ret_type = koopa_ret;

  // 处理形参
  vector<string> param_names;
  if (func_def->has_params) {
    for (auto &p : func_def->params) {
      const auto *fparam = dynamic_cast<const FuncFParamAST*>(p.get());
      if (fparam) {
        func->params.push_back({"@" + fparam->ident, "i32"});
        param_names.push_back(fparam->ident);
      }
    }
  }

  current_func = func.get();

  // 创建入口基本块
  auto *entry_bb = NewBB("entry");
  entry_block = entry_bb;
  int reg_counter = 0;

  // 进入函数作用域: 为每个形参分配局部空间并存储
  EnterScope();
  for (size_t i = 0; i < param_names.size(); ++i) {
    const string &pname = param_names[i];
    // 分配局部变量空间
    string alloc_name = "@" + pname + "_" + to_string(alloc_counter++);
    auto &cur = scope_stack.back();
    cur[pname] = {false, 0, alloc_name, false, "", 0, false};
    // alloc
    entry_bb->instructions.push_back(make_unique<AllocInst>(alloc_name));
    // store 形参到局部变量
    string param_ref = "@" + pname;
    entry_bb->instructions.push_back(
        make_unique<StoreInst>(make_unique<AllocRef>(param_ref), alloc_name));
  }

  // 处理函数体
  ProcessBlockItems(*func_def->block, entry_bb, reg_counter);
  ExitScope();

  // 为所有未终止的基本块补 ret 指令
  for (auto &bb : func->blocks) {
    if (!IsTerminated(*bb)) {
      if (ret_type == "void") {
        bb->instructions.push_back(make_unique<Return>(nullptr));
      } else {
        bb->instructions.push_back(make_unique<Return>(make_unique<Integer>(0)));
      }
    }
  }

  current_program->functions.push_back(std::move(func));
  current_func = nullptr;
  entry_block = nullptr;
}

// ==================== 主入口 ====================

unique_ptr<Program> GenerateIR(const BaseAST &ast) {
  auto program = make_unique<Program>();
  current_program = program.get();

  scope_stack.clear();
  alloc_counter = 0;
  bb_counter = 0;
  break_targets.clear();
  continue_targets.clear();

  const auto *comp_unit = dynamic_cast<const CompUnitAST*>(&ast);
  if (!comp_unit) return program;

  // 1. 建立全局作用域
  EnterScope();

  // 2. 添加库函数声明
  AddLibraryFunctions(*program);

  // 3. 遍历 CompUnit 中的每一项
  // 全局声明的 dummy 变量 (不实际使用, 仅满足参数引用要求)
  BasicBlock *dummy_bb = nullptr;
  int dummy_reg = 0;

  for (auto &item : comp_unit->items) {
    auto *decl = dynamic_cast<const DeclAST*>(item.get());
    auto *func_def = dynamic_cast<const FuncDefAST*>(item.get());

    if (decl) {
      if (decl->is_const) {
        ProcessConstDecl(*decl, dummy_bb, dummy_reg);
      } else {
        ProcessVarDecl(*decl, dummy_bb, dummy_reg);
      }
    } else if (func_def) {
      ProcessFuncDef(*func_def);
    }
  }

  current_program = nullptr;
  return program;
}
