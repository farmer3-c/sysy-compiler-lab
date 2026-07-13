#include "IRGenerator.h"

using namespace std;

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

// 计算基本表达式: Number 或 ( Exp )
static unique_ptr<Value> EvaluatePrimaryExp(const BaseAST &ast,
                                            BasicBlock &block, int &reg_counter) {
  const auto *primary = dynamic_cast<const PrimaryExpAST*>(&ast);
  if (!primary) return nullptr;

  if (primary->is_number) {
    // PrimaryExp ::= Number
    return make_unique<Integer>(primary->number);
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

unique_ptr<Program> GenerateIR(const BaseAST &ast) {
  auto program = make_unique<Program>();

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

  // 获取返回值
  const auto *block = dynamic_cast<const BlockAST*>(func_def->block.get());
  if (block && !block->system_category.empty()) {
    const auto *stmt = dynamic_cast<const StmtAST*>(block->system_category[0].get());
    if (stmt) {
      int reg_counter = 0;
      auto ret_val = EvaluateExp(*stmt->exp, *entry_block, reg_counter);
      if (ret_val) {
        // 创建返回指令
        auto ret = make_unique<Return>(std::move(ret_val));
        entry_block->instructions.push_back(std::move(ret));
      }
    }
  }

  func->blocks.push_back(std::move(entry_block));
  program->functions.push_back(std::move(func));

  return program;
}
