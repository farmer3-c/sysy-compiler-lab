// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 farmer3-c
#include "IRGenerator.h"
#include <unordered_map>
#include <vector>
#include <iostream>
#include <functional>

using namespace std;

// ==================== 符号表 ====================

struct SymbolInfo {
  bool is_const = false;
  int const_val = 0;
  string alloc_name;       // 局部变量: "@name_N"; 全局变量: "@name"
  bool is_func = false;    // true: 函数符号
  string func_ret_type;    // "i32" 或 "void"
  int func_param_count = 0; // 形参个数
  bool is_global = false;  // true: 全局变量 (alloc 在 Program 级别)
  bool is_array = false;   // true: 数组变量
  vector<int> dims;        // 数组各维长度 (标量时为空)
  bool is_array_param = false; // true: 函数数组形参 (第一维省略, 实际是指针)
  int param_index = 0;     // 形参在函数中的位置 (从 0 开始)
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
  cur[name] = {true, val, "", false, "", 0, false, false, {}, false, 0};
}

static bool IsTerminated(const BasicBlock &bb);

// ==================== 类型/数组辅助函数 ====================

// 构建 Koopa IR 类型字符串
static string MakeArrayType(const vector<int> &dims) {
  string t = "i32";
  for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
    t = "[" + t + ", " + to_string(*it) + "]";
  }
  return t;
}

static string MakeFuncArrayParamType(const vector<int> &dims) {
  string t = "i32";
  for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
    t = "[" + t + ", " + to_string(*it) + "]";
  }
  return "*" + t;
}

// 添加变量: 局部变量在 entry_block 中生成 alloc 指令
// 全局变量在 Program 中生成 global alloc
// dims: 数组维度 (标量时为空)
static void AddVar(const string &name, const vector<int> &dims,
                   BasicBlock * /*block*/, int &reg_counter) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) { cerr << "error: duplicate symbol " << name << endl; return; }

  // 判断是否在全局作用域 (仅全局作用域一层, 即 scope_stack.size() == 1)
  bool is_global = (scope_stack.size() == 1 && current_func == nullptr);
  string koopa_type = dims.empty() ? "i32" : MakeArrayType(dims);

  if (is_global) {
    // 全局变量: 生成 global alloc, 放在 Program 中
    string global_name = "@" + name;
    SymbolInfo info;
    info.is_const = false;
    info.const_val = 0;
    info.alloc_name = global_name;
    info.is_func = false;
    info.is_global = true;
    info.is_array = !dims.empty();
    info.dims = dims;
    info.is_array_param = false;  // 全局变量不是函数形参
    cur[name] = info;
    // 默认初始化为 zeroinit, 后续由 ProcessVarDecl 替换
    current_program->global_allocs.push_back(
        make_unique<GlobalAllocInst>(global_name, koopa_type, make_unique<ZeroInit>()));
  } else {
    // 局部变量: alloc 在 entry_block 中
    string alloc_name = "@" + name + "_" + to_string(alloc_counter++);
    SymbolInfo info;
    info.is_const = false;
    info.const_val = 0;
    info.alloc_name = alloc_name;
    info.is_func = false;
    info.is_global = false;
    info.is_array = !dims.empty();
    info.dims = dims;
    info.is_array_param = false;  // 局部变量不是函数形参
    cur[name] = info;
    auto &insts = entry_block->instructions;
    if (!insts.empty() && IsTerminated(*entry_block)) {
      insts.insert(insts.end() - 1, make_unique<AllocInst>(alloc_name, koopa_type));
    } else {
      insts.push_back(make_unique<AllocInst>(alloc_name, koopa_type));
    }
  }
}

// // 重载: 标量版本 (向后兼容)
// static void AddVar(const string &name, BasicBlock *block, int &reg_counter) {
//   AddVar(name, {}, block, reg_counter);
// }

// 添加函数符号到当前作用域
static void AddFunc(const string &name, const string &ret_type, int param_count) {
  if (scope_stack.empty()) EnterScope();
  auto &cur = scope_stack.back();
  if (cur.count(name)) { cerr << "error: duplicate symbol " << name << endl; return; }
  cur[name] = {false, 0, "", true, ret_type, param_count, false, false, {}, false, 0};
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

// ==================== 初始化列表展平 ====================

// 将嵌套的 ConstInitVal 展平为一维整数向量 (编译时)
static void FlattenConstInit(const vector<int> &dims, int dim_start,
                              const ConstInitValAST &init,
                              vector<int> &result) {
  int total_in_sub = 1;
  for (int i = dim_start; i < (int)dims.size(); i++) total_in_sub *= dims[i];

  if (init.is_list) {
    for (auto &item : init.items) {
      auto *sub = dynamic_cast<ConstInitValAST*>(item.get());
      if (!sub) continue;
      if (sub->is_list) {
        int pos = (int)result.size();
        int innermost = dims.back();
        if (pos % innermost != 0) {
          cerr << "warning: init list not aligned to innermost dimension boundary" << endl;
          while (pos % innermost != 0) { result.push_back(0); pos++; }
        }
        // 找到对齐的边界, 确定此嵌套列表对应的维度
        // 从最内层向外检查: 第一个 不 对齐的边界的外一层即目标
        // pos==0 时所有边界都对齐, 目标为 dim_start+1 (更内一层)
        int target;
        if (pos == 0 && dim_start + 1 < (int)dims.size()) {
          target = dim_start + 1;
        } else {
          target = dim_start;
          int stride = 1;
          for (int d = (int)dims.size() - 1; d >= dim_start; d--) {
            stride *= dims[d];
            if (pos % stride == 0) target = d;
          }
        }
        FlattenConstInit(dims, target, *sub, result);
      } else {
        result.push_back(EvaluateConstExp(*sub->exp));
      }
    }
    while ((int)result.size() % total_in_sub != 0)
      result.push_back(0);
  } else {
    result.push_back(EvaluateConstExp(*init.exp));
  }
}

// 构建嵌套的 Aggregate 常量 (用于多维全局数组初始化)
// dims = [2, 3], flat = [1,2,3,4,5,6] → {{1,2,3},{4,5,6}}
static unique_ptr<Aggregate> BuildNestedAggregate(const vector<int> &dims,
                                                    int dim_start,
                                                    const vector<int> &flat,
                                                    size_t &pos) {
  auto agg = make_unique<Aggregate>();
  int cur_dim = dim_start < (int)dims.size() ? dims[dim_start] : 0;

  if (dim_start == (int)dims.size() - 1) {
    // 最内层维度: 直接用 Integer 填充
    for (int i = 0; i < cur_dim; i++) {
      agg->elements.push_back(make_unique<Integer>(flat[pos++]));
    }
  } else {
    // 外层维度: 递归创建嵌套 Aggregate
    for (int i = 0; i < cur_dim; i++) {
      agg->elements.push_back(BuildNestedAggregate(dims, dim_start + 1, flat, pos));
    }
  }
  return agg;
}

// 数组访问辅助: 计算 flat_index → 多维索引
// dims = [2, 3] → stride = [3, 1]; flat_idx=4 → [1, 1]
static vector<int> FlatToMultiIndex(const vector<int> &dims, int flat_idx) {
  vector<int> indices;
  int n = (int)dims.size();
  for (int i = n - 1; i >= 0; i--) {
    indices.push_back(flat_idx % dims[i]);
    flat_idx /= dims[i];
  }
  reverse(indices.begin(), indices.end());
  return indices;
}

// 生成多维数组访问的 getelemptr/getptr 链
// base: 数组/指针的 alloc 名称 (如 "@arr") 或寄存器引用 (如 "%0")
// dims: 变量声明的维度
// is_array_param: true 表示是函数数组参数 (第一维用 getptr, 后续用 getelemptr)
// index_vals: 各维索引值 (Integer 或 RegRef), 所有权被转移
static string GenerateArrayAccess(const string &base_alloc,
                                   const vector<int> &dims,
                                   bool is_array_param,
                                   vector<unique_ptr<Value>> &index_vals,
                                   BasicBlock *&block, int &reg_counter) {
  string src = base_alloc;
  int n_idx = (int)index_vals.size();

  if (n_idx == 0) return base_alloc;  // 标量, 直接返回

  if (is_array_param) {
    // 数组参数: alloc 的类型是 **[...] (存的是指针),
    // 必须先 load 拿到实际的 *[...] 指针值, 再做指针运算
    int ld = reg_counter++;
    block->instructions.push_back(make_unique<LoadInst>(ld, src));
    src = "%" + to_string(ld);

    // 第一维用 getptr
    int dest = reg_counter++;
    block->instructions.push_back(
        make_unique<GetPtrInst>(dest, src, std::move(index_vals[0])));
    src = "%" + to_string(dest);

    // 后续维度用 getelemptr (如果有)
    for (int i = 1; i < n_idx; i++) {
      int d = reg_counter++;
      block->instructions.push_back(
          make_unique<GetElemPtrInst>(d, src, std::move(index_vals[i])));
      src = "%" + to_string(d);
    }
  } else {
    // 局部/全局数组: 所有维度都用 getelemptr
    for (int i = 0; i < n_idx; i++) {
      int dest = reg_counter++;
      block->instructions.push_back(
          make_unique<GetElemPtrInst>(dest, src, std::move(index_vals[i])));
      src = "%" + to_string(dest);
    }
  }
  return src;
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

    if (info->is_const && !info->is_array) return make_unique<Integer>(info->const_val);

    if (!primary->has_indices) {
      // 标量或数组名 (用于地址传递, 如函数参数)
      if (info->is_array) {
        if (info->is_array_param) {
          // 数组形参退化: 形参 alloc 存的是指针, load 得到指针值
          // 例如 int arr[] 传给另一个 int[] 参数, 需要 *i32
          int ld = reg_counter++;
          block->instructions.push_back(make_unique<LoadInst>(ld, info->alloc_name));
          return make_unique<AllocRef>("%" + to_string(ld));
        }
        // 普通数组退化: getelemptr @arr, 0 获取第一个元素的地址
        // 只做一次 getelemptr: 2D 数组 → *[i32,N2] (指向第一行), 而非 *i32
        vector<unique_ptr<Value>> idx_vals;
        idx_vals.push_back(make_unique<Integer>(0));
        string ptr = GenerateArrayAccess(info->alloc_name, info->dims,
                                          false, idx_vals,
                                          block, reg_counter);
        // 返回指针值 (作为 *elem), ptr 已经是 "%N" 格式
        return make_unique<AllocRef>(ptr);
      }
      if (info->is_const) return make_unique<Integer>(info->const_val);
      int dest = reg_counter++;
      block->instructions.push_back(make_unique<LoadInst>(dest, info->alloc_name));
      return make_unique<RegRef>(dest);
    }

    // 数组索引访问: LVal["[" Exp "]"]
    // 1. 求值索引表达式
    vector<unique_ptr<Value>> idx_vals;
    for (auto &idx : primary->indices) {
      auto val = EvaluateExp(*idx, block, reg_counter);
      if (val) idx_vals.push_back(std::move(val));
      else idx_vals.push_back(make_unique<Integer>(0));
    }

    // 2. 生成 getelemptr/getptr 链
    string ptr = GenerateArrayAccess(info->alloc_name, info->dims,
                                      info->is_array_param, idx_vals,
                                      block, reg_counter);

    // 实际总维数: 数组形参第一维省略 (dims 不含第一维)
    int total_dims = (int)info->dims.size() + (info->is_array_param ? 1 : 0);

    if ((int)primary->indices.size() < total_dims) {
      // 部分解引用: 结果是子数组, 作为实参传递
      // 子数组作为值使用时需要 decay (C 中数组到指针的隐式转换)
      // 添加一个 getelemptr 0 将子数组指针 decay 到其第一个元素的指针
      vector<unique_ptr<Value>> decay_idx;
      decay_idx.push_back(make_unique<Integer>(0));
      string p2 = GenerateArrayAccess(ptr, {}, false, decay_idx, block, reg_counter);
      return make_unique<AllocRef>(p2);
    }

    // 3. 完整访问: load
    int dest = reg_counter++;
    block->instructions.push_back(make_unique<LoadInst>(dest, ptr));
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

static void ProcessConstDecl(const BaseAST &ast, BasicBlock *block, int &reg_counter) {
  const auto *decl = dynamic_cast<const DeclAST*>(&ast);
  if (!decl || !decl->is_const) return;
  const auto *const_decl = dynamic_cast<const ConstDeclAST*>(decl->decl_body.get());
  if (!const_decl) return;
  for (auto &def : const_decl->const_defs) {
    const auto *const_def = dynamic_cast<const ConstDefAST*>(def.get());
    if (!const_def) continue;

    // 求值数组维度
    vector<int> dims;
    for (auto &d : const_def->dims) {
      dims.push_back(EvaluateConstExp(*d));
    }

    const auto *init = dynamic_cast<const ConstInitValAST*>(const_def->init_val.get());
    if (!init) continue;

    if (dims.empty()) {
      // 标量常量: 直接求值并折叠
      AddConst(const_def->ident, EvaluateConstExp(*init->exp));
    } else {
      // 常量数组: 需要在 IR 中分配 (标量值不用存符号表, 但数组必须分配)
      // 只考虑整数类型的常量定义, 为数组生成 alloc
      // 先记录到符号表, 但标记为常量数组
      bool is_global = (scope_stack.size() == 1 && current_func == nullptr);

      if (is_global) {
        string global_name = "@" + const_def->ident;
        string koopa_type = MakeArrayType(dims);
        SymbolInfo info;
        info.is_const = true;
        info.const_val = 0;
        info.alloc_name = global_name;
        info.is_func = false;
        info.is_global = true;
        info.is_array = true;
        info.dims = dims;
        info.is_array_param = false;  // 全局常量数组不是函数形参
        auto &cur = scope_stack.back();
        cur[const_def->ident] = info;

        // 展平初始化列表
        vector<int> flat;
        FlattenConstInit(dims, 0, *init, flat);
        int total = 1;
        for (int d : dims) total *= d;
        while ((int)flat.size() < total) flat.push_back(0);

        // 生成 Aggregate 常量
        if (dims.size() <= 1) {
          vector<unique_ptr<Value>> elems;
          for (int v : flat) elems.push_back(make_unique<Integer>(v));
          current_program->global_allocs.push_back(
              make_unique<GlobalAllocInst>(global_name, koopa_type,
                  make_unique<Aggregate>(std::move(elems))));
        } else {
          size_t pos = 0;
          auto nested = BuildNestedAggregate(dims, 0, flat, pos);
          current_program->global_allocs.push_back(
              make_unique<GlobalAllocInst>(global_name, koopa_type, std::move(nested)));
        }
      } else {
        // 局部常量数组
        string alloc_name = "@" + const_def->ident + "_" + to_string(alloc_counter++);
        string koopa_type = MakeArrayType(dims);
        SymbolInfo info;
        info.is_const = true;
        info.const_val = 0;
        info.alloc_name = alloc_name;
        info.is_func = false;
        info.is_global = false;
        info.is_array = true;
        info.dims = dims;
        info.is_array_param = false;  // 局部常量数组不是函数形参
        auto &cur = scope_stack.back();
        cur[const_def->ident] = info;

        auto &insts = entry_block->instructions;
        if (!insts.empty() && IsTerminated(*entry_block))
          insts.insert(insts.end() - 1, make_unique<AllocInst>(alloc_name, koopa_type));
        else
          insts.push_back(make_unique<AllocInst>(alloc_name, koopa_type));

        // 展平并生成存储
        vector<int> flat;
        FlattenConstInit(dims, 0, *init, flat);
        int total = 1;
        for (int d : dims) total *= d;
        while ((int)flat.size() < total) flat.push_back(0);

        // 为常量数组生成 getelemptr + store 初始化
        BasicBlock *init_block = block ? block : entry_block;
        // 遍历所有常量值, 生成 getelemptr + store
        for (size_t i = 0; i < flat.size(); i++) {
          auto multi = FlatToMultiIndex(dims, (int)i);
          vector<unique_ptr<Value>> idx_vals;
          for (int m : multi) idx_vals.push_back(make_unique<Integer>(m));
          string ptr = GenerateArrayAccess(alloc_name, dims, false, idx_vals, init_block, reg_counter);
          init_block->instructions.push_back(
              make_unique<StoreInst>(make_unique<Integer>(flat[i]), ptr));
        }
      }
    }
  }
}

// 运行时求值 InitVal, 得到所有元素的值
// 返回 flat vector of Values (reg refs)
static void EvalInitToFlat(const vector<int> &dims, int dim_start,
                            const InitValAST &init,
                            BasicBlock *&block, int &reg_counter,
                            vector<unique_ptr<Value>> &result) {
  int total_in_sub = 1;
  for (int i = dim_start; i < (int)dims.size(); i++) total_in_sub *= dims[i];

  if (init.is_list) {
    for (auto &item : init.items) {
      auto *sub = dynamic_cast<InitValAST*>(item.get());
      if (!sub) continue;
      if (sub->is_list) {
        int pos = (int)result.size();
        int innermost = dims.back();
        if (pos % innermost != 0) {
          while (pos % innermost != 0) { result.push_back(make_unique<Integer>(0)); pos++; }
        }
        // 找到对齐的边界, 确定此嵌套列表对应的维度
        int target;
        if (pos == 0 && dim_start + 1 < (int)dims.size()) {
          target = dim_start + 1;
        } else {
          target = dim_start;
          int stride = 1;
          for (int d = (int)dims.size() - 1; d >= dim_start; d--) {
            stride *= dims[d];
            if (pos % stride == 0) target = d;
          }
        }
        EvalInitToFlat(dims, target, *sub, block, reg_counter, result);
      } else {
        auto val = EvaluateExp(*sub->exp, block, reg_counter);
        if (val) result.push_back(std::move(val));
        else result.push_back(make_unique<Integer>(0));
      }
    }
    while ((int)result.size() % total_in_sub != 0)
      result.push_back(make_unique<Integer>(0));
  } else {
    auto val = EvaluateExp(*init.exp, block, reg_counter);
    if (val) result.push_back(std::move(val));
    else result.push_back(make_unique<Integer>(0));
  }
}

// 为局部数组生成 getelemptr + store 初始化代码
static void GenerateArrayInitCode(const vector<int> &dims,
                                   const string &array_alloc,
                                   vector<unique_ptr<Value>> &flat_vals,
                                   BasicBlock *&block, int &reg_counter) {
  for (size_t i = 0; i < flat_vals.size(); i++) {
    auto multi = FlatToMultiIndex(dims, (int)i);
    vector<unique_ptr<Value>> idx_vals;
    for (int m : multi) idx_vals.push_back(make_unique<Integer>(m));
    string ptr = GenerateArrayAccess(array_alloc, dims, false, idx_vals, block, reg_counter);
    block->instructions.push_back(
        make_unique<StoreInst>(std::move(flat_vals[i]), ptr));
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

    // 求值数组维度
    vector<int> dims;
    for (auto &d : var_def->dims) {
      dims.push_back(EvaluateConstExp(*d));
    }

    // 添加变量到符号表 (带维度)
    if (is_global) {
      // 全局变量: 手动构造 SymbolInfo
      string global_name = "@" + var_def->ident;
      string koopa_type = dims.empty() ? "i32" : MakeArrayType(dims);
      SymbolInfo info;
      info.alloc_name = global_name;
      info.is_global = true;
      info.is_array = !dims.empty();
      info.dims = dims;
      info.is_array_param = false;  // 全局变量不是函数形参
      auto &cur = scope_stack.back();
      if (cur.count(var_def->ident)) { cerr << "error: duplicate symbol " << var_def->ident << endl; continue; }
      cur[var_def->ident] = info;
      // 关键: 必须生成 global alloc (默认 zeroinit, 初始化后替换)
      current_program->global_allocs.push_back(
          make_unique<GlobalAllocInst>(global_name, koopa_type, make_unique<ZeroInit>()));
    } else {
      AddVar(var_def->ident, dims, block, reg_counter);
    }

    // 处理初始化
    if (var_def->has_init) {
      auto *info = Lookup(var_def->ident);
      if (!info) continue;

      if (dims.empty()) {
        // 标量变量初始化
        const auto *init_val = dynamic_cast<const InitValAST*>(var_def->init_val.get());
        if (!init_val || init_val->is_list) continue;
        unique_ptr<Value> val;
        if (is_global) {
          int const_init = EvaluateConstExp(*init_val->exp);
          val = make_unique<Integer>(const_init);
        } else {
          val = EvaluateExp(*init_val->exp, block, reg_counter);
        }
        if (val && info) {
          if (is_global) {
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
      } else {
        // 数组初始化
        const auto *init_val = dynamic_cast<const InitValAST*>(var_def->init_val.get());
        if (!init_val) continue;
        if (is_global) {
          // 全局数组: 展平为常量, 生成 Aggregate
          vector<int> flat;
          const auto *const_init = dynamic_cast<const ConstInitValAST*>(init_val);
          // 对于全局数组, InitVal 中的表达式必须是常量
          if (const_init) {
            FlattenConstInit(dims, 0, *const_init, flat);
          } else {
            // 仍然是 InitVal, 但约束为常量表达式
            // 手动展平: 所有元素调用 EvaluateConstExp
            // 对于 InitVal, 递归展平
            function<void(int, const InitValAST&)> flatten;
            flatten = [&](int dim_start, const InitValAST &iv) {
              int total = 1;
              for (int i = dim_start; i < (int)dims.size(); i++) total *= dims[i];
              if (iv.is_list) {
                for (auto &item : iv.items) {
                  auto *sub = dynamic_cast<InitValAST*>(item.get());
                  if (!sub) continue;
                  if (sub->is_list) {
                    int pos = (int)flat.size();
                    int inn = dims.back();
                    if (pos % inn != 0) {
                      while (pos % inn != 0) { flat.push_back(0); pos++; }
                    }
                    int tgt;
                    if (pos == 0 && dim_start + 1 < (int)dims.size()) {
                      tgt = dim_start + 1;
                    } else {
                      tgt = dim_start;
                      int s = 1;
                      for (int dd = (int)dims.size() - 1; dd >= dim_start; dd--) {
                        s *= dims[dd];
                        if (pos % s == 0) tgt = dd;
                      }
                    }
                    flatten(tgt, *sub);
                  } else {
                    flat.push_back(EvaluateConstExp(*sub->exp));
                  }
                }
                while ((int)flat.size() % total != 0) flat.push_back(0);
              } else {
                flat.push_back(EvaluateConstExp(*iv.exp));
              }
            };
            flatten(0, *init_val);
          }
          int total = 1;
          for (int d : dims) total *= d;
          while ((int)flat.size() < total) flat.push_back(0);

          if (dims.size() <= 1) {
            vector<unique_ptr<Value>> elems;
            for (int v : flat) elems.push_back(make_unique<Integer>(v));
            for (auto &ga : current_program->global_allocs) {
              auto *g = dynamic_cast<GlobalAllocInst*>(ga.get());
              if (g && g->name == info->alloc_name) {
                g->init_val = make_unique<Aggregate>(std::move(elems));
                break;
              }
            }
          } else {
            size_t pos = 0;
            auto nested = BuildNestedAggregate(dims, 0, flat, pos);
            for (auto &ga : current_program->global_allocs) {
              auto *g = dynamic_cast<GlobalAllocInst*>(ga.get());
              if (g && g->name == info->alloc_name) {
                g->init_val = std::move(nested);
                break;
              }
            }
          }
        } else {
          // 局部数组: 展平为运行时值, 生成 getelemptr + store
          vector<unique_ptr<Value>> flat_vals;
          EvalInitToFlat(dims, 0, *init_val, block, reg_counter, flat_vals);
          int total = 1;
          for (int d : dims) total *= d;
          while ((int)flat_vals.size() < (size_t)total)
            flat_vals.push_back(make_unique<Integer>(0));
          GenerateArrayInitCode(dims, info->alloc_name, flat_vals, block, reg_counter);
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
  if (!val) return;

  if (lval->indices.empty()) {
    // 标量赋值
    block->instructions.push_back(make_unique<StoreInst>(std::move(val), info->alloc_name));
  } else {
    // 数组元素赋值
    vector<unique_ptr<Value>> idx_vals;
    for (auto &idx : lval->indices) {
      auto idx_val = EvaluateExp(*idx, block, reg_counter);
      if (idx_val) idx_vals.push_back(std::move(idx_val));
      else idx_vals.push_back(make_unique<Integer>(0));
    }
    string ptr = GenerateArrayAccess(info->alloc_name, info->dims,
                                      info->is_array_param, idx_vals,
                                      block, reg_counter);
    block->instructions.push_back(make_unique<StoreInst>(std::move(val), ptr));
  }
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

  // 为库函数设置 SymbolInfo, 新增字段默认值
  global["getint"]    = {false, 0, "", true, "i32", 0, false, false, {}, false, 0};
  global["getch"]     = {false, 0, "", true, "i32", 0, false, false, {}, false, 0};
  global["getarray"]  = {false, 0, "", true, "i32", 1, false, false, {}, false, 0};
  global["putint"]    = {false, 0, "", true, "void", 1, false, false, {}, false, 0};
  global["putch"]     = {false, 0, "", true, "void", 1, false, false, {}, false, 0};
  global["putarray"]  = {false, 0, "", true, "void", 2, false, false, {}, false, 0};
  global["starttime"] = {false, 0, "", true, "void", 0, false, false, {}, false, 0};
  global["stoptime"]  = {false, 0, "", true, "void", 0, false, false, {}, false, 0};

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
  vector<string> param_refs;    // Koopa IR 中的参数引用名 (可能与 param_names 不同, 用于避免与全局变量冲突)
  vector<string> param_koopa_types;
  vector<bool> param_is_array;
  vector<vector<int>> param_dims;
  if (func_def->has_params) {
    for (auto &p : func_def->params) {
      const auto *fparam = dynamic_cast<const FuncFParamAST*>(p.get());
      if (fparam) {
        // 检查参数名是否与全局变量冲突
        string param_ref;
        if (!scope_stack.empty()) {
          auto &global = scope_stack[0];
          if (global.count(fparam->ident)) {
            // 冲突: 使用带前缀的参数名, 避免 Koopa IR 在函数体内将 @name 解析为全局变量
            param_ref = "@_param_" + fparam->ident;
          } else {
            param_ref = "@" + fparam->ident;
          }
        } else {
          param_ref = "@" + fparam->ident;
        }

        if (fparam->is_array) {
          // 数组参数: 求值已知维度
          vector<int> known_dims;
          for (auto &d : fparam->dims) {
            known_dims.push_back(EvaluateConstExp(*d));
          }
          string koopa_type = MakeFuncArrayParamType(known_dims);
          func->params.push_back({param_ref, koopa_type});
          param_names.push_back(fparam->ident);
          param_refs.push_back(param_ref);
          param_koopa_types.push_back(koopa_type);
          param_is_array.push_back(true);
          param_dims.push_back(known_dims);
        } else {
          // 标量参数
          func->params.push_back({param_ref, "i32"});
          param_names.push_back(fparam->ident);
          param_refs.push_back(param_ref);
          param_koopa_types.push_back("i32");
          param_is_array.push_back(false);
          param_dims.push_back({});
        }
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
    string alloc_name = "@" + pname + "_" + to_string(alloc_counter++);
    string koopa_type = param_koopa_types[i];
    auto &cur = scope_stack.back();

    SymbolInfo info;
    info.is_const = false;
    info.const_val = 0;
    info.alloc_name = alloc_name;
    info.is_func = false;
    info.is_global = false;
    info.is_array = param_is_array[i];
    info.dims = param_dims[i];
    info.is_array_param = param_is_array[i];
    info.param_index = (int)i;
    cur[pname] = info;

    // alloc 形参空间
    entry_bb->instructions.push_back(make_unique<AllocInst>(alloc_name, koopa_type));
    // store 形参值到局部变量
    // param_ref 已通过重命名避免与全局变量同名冲突
    string param_ref = param_refs[i];
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
