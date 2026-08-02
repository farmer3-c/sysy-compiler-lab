#include "IRGenerator.h"
#include <unordered_map>
#include <vector>
#include <iostream>

using namespace std;

// ==================== 作用域符号表 ====================

struct SymbolInfo {
  bool is_const;
  int const_val;
  string alloc_name;
};

static vector<unordered_map<string, SymbolInfo>> scope_stack;
static int alloc_counter = 0;

// 当前正在构建的函数 (用于在表达式求值过程中创建新基本块)
static Function *current_func = nullptr;
static BasicBlock *entry_block = nullptr;  // 入口基本块，所有 alloc 必须在此
static int bb_counter = 0;

static void EnterScope() { scope_stack.emplace_back(); }
static void ExitScope() { if (!scope_stack.empty()) scope_stack.pop_back(); }

static void AddConst(const string &name, int val) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) { cerr << "error: duplicate symbol " << name << endl; return; }
  cur[name] = {true, val, ""};
}

// 前向声明 (定义在下方, 但 AddVar 中就需要用到)
static bool IsTerminated(const BasicBlock &bb);

static void AddVar(const string &name, BasicBlock * /*block*/, int &reg_counter) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) { cerr << "error: duplicate symbol " << name << endl; return; }
  string alloc_name = "@" + name + "_" + std::to_string(alloc_counter++);
  cur[name] = {false, 0, alloc_name};
  // alloc 必须放在 entry 基本块中, 否则 libkoopa 可能无法解析
  auto &insts = entry_block->instructions;
  // 如果 entry 块已被终止, 插入到终止指令之前; 否则追加到末尾
  if (!insts.empty() && IsTerminated(*entry_block)) {
    insts.insert(insts.end() - 1, make_unique<AllocInst>(alloc_name));
  } else {
    insts.push_back(make_unique<AllocInst>(alloc_name));
  }
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

// 检查基本块是否已终止 (最后一条指令是 br/jump/ret)
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
  if (unary->is_primary) return EvaluateConstPrimaryExp(*unary->primary_exp);
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
// 所有 Evaluate* 函数的 block 参数是 BasicBlock *& (指针引用),
// 短路求值可能创建新基本块并更新 block 指向新块.

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
  if (unary->is_primary) return EvaluatePrimaryExp(*unary->primary_exp, block, reg_counter);
  auto operand = EvaluateUnaryExp(*unary->unary_exp, block, reg_counter);
  if (!operand) return nullptr;
  if (unary->op == "+") return operand;
  if (unary->op == "-") {
    int dest = reg_counter++;
    block->instructions.push_back(make_unique<BinaryInst>(dest, "sub", make_unique<Integer>(0), std::move(operand)));
    return make_unique<RegRef>(dest);
  }
  if (unary->op == "!") {
    int dest = reg_counter++;
    block->instructions.push_back(make_unique<BinaryInst>(dest, "eq", std::move(operand), make_unique<Integer>(0)));
    return make_unique<RegRef>(dest);
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
//   @sc = alloc i32; store 0, @sc
//   求值 X → lhs; br lhs, %rhs_bb, %end_bb
// %rhs_bb: 求值 Y → rhs; %r = ne rhs, 0; store %r, @sc; jump %end_bb
// %end_bb: %result = load @sc
// block 更新为 %end_bb (结果所在块)
static unique_ptr<Value> EvaluateLAndExp(const BaseAST &ast,
                                         BasicBlock *&block, int &reg_counter) {
  const auto *land = dynamic_cast<const LAndExpAST*>(&ast);
  if (!land) return nullptr;
  if (land->is_eq) return EvaluateEqExp(*land->eq_exp, block, reg_counter);

  string sc_alloc = "@sc_and_" + to_string(alloc_counter++);
  // alloc 必须放在 entry 块, 否则 libkoopa 可能无法解析
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

  // 分支: LHS 非0则进入 RHS, 否则短路到 end
  block->instructions.push_back(make_unique<BranchInst>(std::move(lhs_val), rhs_name, end_name));

  // RHS 基本块
  auto rhs_val = EvaluateEqExp(*land->rhs, rhs_bb, reg_counter);
  if (rhs_val) {
    int r = reg_counter++;
    rhs_bb->instructions.push_back(make_unique<BinaryInst>(r, "ne", std::move(rhs_val), make_unique<Integer>(0)));
    rhs_bb->instructions.push_back(make_unique<StoreInst>(make_unique<RegRef>(r), sc_alloc));
  }
  rhs_bb->instructions.push_back(make_unique<JumpInst>(end_name));

  // End 基本块: 加载结果, 返回在 end_bb 中
  int result = reg_counter++;
  end_bb->instructions.push_back(make_unique<LoadInst>(result, sc_alloc));
  block = end_bb;  // ★ 更新 block 指针到结果所在基本块
  return make_unique<RegRef>(result);
}

// 短路求值 ||
//   @sc = alloc i32; store 1, @sc
//   求值 X → lhs; br lhs, %end_bb, %rhs_bb
// %rhs_bb: 求值 Y → rhs; %r = ne rhs, 0; store %r, @sc; jump %end_bb
// %end_bb: %result = load @sc
// block 更新为 %end_bb (结果所在块)
static unique_ptr<Value> EvaluateLOrExp(const BaseAST &ast,
                                        BasicBlock *&block, int &reg_counter) {
  const auto *lor = dynamic_cast<const LOrExpAST*>(&ast);
  if (!lor) return nullptr;
  if (lor->is_land) return EvaluateLAndExp(*lor->land_exp, block, reg_counter);

  string sc_alloc = "@sc_or_" + to_string(alloc_counter++);
  // alloc 必须放在 entry 块, 否则 libkoopa 可能无法解析
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
  block = end_bb;  // ★ 更新 block 指针到结果所在基本块
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
  for (auto &def : var_decl->var_defs) {
    const auto *var_def = dynamic_cast<const VarDefAST*>(def.get());
    if (!var_def) continue;
    AddVar(var_def->ident, block, reg_counter);
    if (var_def->has_init) {
      const auto *init_val = dynamic_cast<const InitValAST*>(var_def->init_val.get());
      if (!init_val) continue;
      auto val = EvaluateExp(*init_val->exp, block, reg_counter);
      auto *info = Lookup(var_def->ident);
      if (val && info) block->instructions.push_back(make_unique<StoreInst>(std::move(val), info->alloc_name));
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

// 前向声明
static BasicBlock *ProcessStmt(const StmtAST &stmt, BasicBlock *cur, int &reg_counter);

// 处理 Block 内所有 BlockItem, 支持作用域嵌套和控制流
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
    auto ret_val = EvaluateExp(*stmt.exp, cur, reg_counter);
    if (ret_val) cur->instructions.push_back(make_unique<Return>(std::move(ret_val)));
    return nullptr;
  }
  case StmtAST::ASSIGN: {
    ProcessAssign(stmt, cur, reg_counter);
    return cur;
  }
  case StmtAST::EXP_STMT: {
    if (stmt.exp) EvaluateExp(*stmt.exp, cur, reg_counter);
    return cur;
  }
  case StmtAST::BLOCK: {
    return ProcessBlockItems(*stmt.block, cur, reg_counter);
  }
  case StmtAST::IF_ELSE: {
    // EvaluateExp 可能因短路求值而更新 cur 指针
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
  }
  return cur;
}

// ==================== 主入口 ====================

unique_ptr<Program> GenerateIR(const BaseAST &ast) {
  auto program = make_unique<Program>();

  scope_stack.clear();
  alloc_counter = 0;
  bb_counter = 0;

  const auto *comp_unit = dynamic_cast<const CompUnitAST*>(&ast);
  if (!comp_unit) return program;

  const auto *func_def = dynamic_cast<const FuncDefAST*>(comp_unit->func_def.get());
  if (!func_def) return program;

  auto func = make_unique<Function>(func_def->ident);
  current_func = func.get();

  auto *entry_bb = NewBB("entry");
  entry_block = entry_bb;

  int reg_counter = 0;
  ProcessBlockItems(*func_def->block, entry_bb, reg_counter);

  // 如果最后一个基本块未被终止, 补一条 ret 0
  if (!func->blocks.empty()) {
    auto *last_bb = func->blocks.back().get();
    if (!IsTerminated(*last_bb)) {
      last_bb->instructions.push_back(make_unique<Return>(make_unique<Integer>(0)));
    }
  }

  program->functions.push_back(std::move(func));
  current_func = nullptr;
  entry_block = nullptr;

  return program;
}
