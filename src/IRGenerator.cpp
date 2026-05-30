#include "IRGenerator.h"

using namespace std;

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
      const auto *num = dynamic_cast<const NumAST*>(stmt->num.get());
      if (num) {
        // 创建返回指令
        auto ret = make_unique<Return>(make_unique<Integer>(num->num));
        entry_block->instructions.push_back(std::move(ret));
      }
    }
  }
  
  func->blocks.push_back(std::move(entry_block));
  program->functions.push_back(std::move(func));
  
  return program;
}
