# SysY 编译器实验项目

基于[北大编译实践课程](https://pku-minic.github.io)的 SysY 语言编译器，将 SysY 程序编译为 RISC-V 汇编。

## 项目结构

```
├── Makefile              # 编译脚本
├── README.md
└── src/
    ├── main.cpp          # 入口, 驱动编译流水线
    ├── sysy.l            # Flex 词法规则 → 词法分析器
    ├── sysy.y            # Bison 语法规则 → 语法分析器, 构建 AST
    ├── AST.h             # AST 节点定义
    ├── KoopaIR.h         # Koopa IR 的 C++ 表示 (用于生成文本 IR)
    ├── IRGenerator.h     # AST → Koopa IR 生成器头文件
    ├── IRGenerator.cpp   # AST → Koopa IR 生成器实现
    ├── ASMGenerator.h    # Koopa Raw IR → RISC-V 汇编生成器头文件
    ├── ASMGenerator.cpp  # Koopa Raw IR → RISC-V 汇编生成器实现
    └── koopa.h           # libkoopa C API 头文件
```

## 编译流水线

```
SysY 源码 (.sy)
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. 词法分析 (Flex: sysy.l → lexer)                          │
│    将源代码切分为 Token 序列 (INT, RETURN, IDENT, 符号…)     │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. 语法分析 (Bison: sysy.y → parser)                        │
│    根据 SysY 文法将 Token 序列解析为抽象语法树 (AST)          │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. IR 生成 (IRGenerator: AST → Koopa IR C++ 对象 → 文本)     │
│    遍历 AST, 将表达式映射为 Koopa IR 指令:                      │
│    一元降级: +X → X (恒等)                                     │
│             -X → sub 0, X (取负数)                             │
│             !X → eq X, 0 (逻辑取反)                            │
│    二元直映: + → add, - → sub, * → mul, / → div, % → mod       │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. IR 解析 (libkoopa: 文本 IR → Koopa Raw Program)           │
│    调用 koopa_parse_from_string() 得到可遍历的 C 结构体       │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. 汇编生成 (ASMGenerator: Koopa Raw IR → RISC-V 汇编)       │
│    遍历 Raw Program, 将每条指令翻译为对应的 RISC-V 指令      │
│    - add/sub → add/sub                                      │
│    - mul/div/mod → mul/div/rem                              │
│    - eq → sub + sltiu (set if zero)                        │
│    - ret → li / mv a0 + j exit                              │
│    输出可被 RISC-V 汇编器/模拟器执行的 .s 文件                │
└─────────────────────────────────────────────────────────────┘
```

## 各文件详解

### 构建系统

| 文件 | 作用 |
|------|------|
| [Makefile](Makefile) | 使用 `clang++`、`flex`、`bison` 编译整个项目。生成 `build/compiler`。需要 libkoopa 库 (`$CDE_LIBRARY_PATH`, `$CDE_INCLUDE_PATH`)。 |

### 前端：词法 & 语法分析

| 文件 | 作用 |
|------|------|
| [sysy.l](src/sysy.l) | **Flex 词法规则文件**。定义 SysY 语言的词素模式：空白符、注释、关键字 (`int`, `return`)、标识符、整数字面量（十进制/八进制/十六进制）。将匹配到的词素转换为 Token 返回给 Bison 解析器。 |
| [sysy.y](src/sysy.y) | **Bison 语法规则文件**。定义 SysY 文法的产生式，在归约动作中构造 AST 节点。支持文法：`CompUnit → FuncDef`, `FuncDef → FuncType IDENT ( ) Block`, `Block → { Stmt }`, `Stmt → return Exp ;`, `Exp → AddExp`, `AddExp → MulExp | AddExp (+|-) MulExp`, `MulExp → UnaryExp | MulExp (*|/|%) UnaryExp`, `UnaryExp → PrimaryExp | UnaryOp UnaryExp`, `PrimaryExp → ( Exp ) | Number`, `UnaryOp → + | - | !`。 |

### 抽象语法树

| 文件 | 作用 |
|------|------|
| [AST.h](src/AST.h) | **AST 节点类定义**。所有节点继承 `BaseAST`，用 `unique_ptr` 管理子节点。包含：`CompUnitAST`（编译单元）、`FuncDefAST`（函数定义）、`FuncTypeAST`（函数返回类型）、`BlockAST`（语句块）、`StmtAST`（return 语句）、`ExpAST`（表达式，包装 AddExp）、`AddExpAST`（加减表达式，用 `is_mul` 区分纯 MulExp 和 AddExp 二元运算）、`MulExpAST`（乘除模表达式，用 `is_unary` 区分纯 UnaryExp 和 MulExp 二元运算）、`UnaryExpAST`（一元表达式，用 `is_primary` 区分 PrimaryExp 和 UnaryOp+UnaryExp）、`PrimaryExpAST`（基本表达式，用 `is_number` 区分 Number 和括号表达式）、`NumAST`（数字字面量）。 |

### 中间表示

| 文件 | 作用 |
|------|------|
| [KoopaIR.h](src/KoopaIR.h) | **Koopa IR 的 C++ 类表示**。用于在内存中构建 IR 结构并 `Dump()` 为文本格式。包含：`Integer`（整数常量）、`RegRef`（寄存器引用 `%n`）、`BinaryInst`（二元运算指令 `%n = op lhs, rhs`）、`Return`（返回指令）、`BasicBlock`（基本块）、`Function`（函数）、`Program`（整个程序）。 |
| [koopa.h](src/koopa.h) | **libkoopa C API 头文件**。提供文本 IR ↔ Raw Program 的解析/生成能力，以及 Raw Program 遍历所需的数据结构（`koopa_raw_program_t`, `koopa_raw_function_t`, `koopa_raw_value_t` 等），内置 `KOOPA_RVT_BINARY`、`KOOPA_RBO_SUB` 等二元运算支持。 |

### 代码生成

| 文件 | 作用 |
|------|------|
| [IRGenerator.h](src/IRGenerator.h) / [IRGenerator.cpp](src/IRGenerator.cpp) | **AST → Koopa IR 生成器**。`GenerateIR()` 遍历 AST，将每个语法结构映射为 Koopa IR。递归求值函数链：`EvaluateExp → EvaluateAddExp → EvaluateMulExp → EvaluateUnaryExp → EvaluatePrimaryExp`。一元运算降级为二元指令（`-X`→`sub 0,X`, `!X`→`eq X,0`, `+X`→恒等），二元运算直接映射（`+`→`add`, `-`→`sub`, `*`→`mul`, `/`→`div`, `%`→`mod`）。
| [ASMGenerator.h](src/ASMGenerator.h) / [ASMGenerator.cpp](src/ASMGenerator.cpp) | **Koopa Raw IR → RISC-V 汇编生成器**。遍历 libkoopa 解析后的 Raw Program，将每条 IR 指令翻译为 RISC-V 汇编。二元运算映射：`add`→`add`, `sub`→`sub`, `mul`→`mul`, `div`→`div`, `mod`→`rem`, `eq`→`sub; sltiu`。使用 `t0-t6` 作为临时寄存器，`val_to_reg` 映射追踪 IR 值到寄存器的分配。 |

### 入口

| 文件 | 作用 |
|------|------|
| [main.cpp](src/main.cpp) | **程序入口，驱动整个编译流水线**。参数格式：`compiler <mode> <input> -o <output>`。依次执行：Flex+Bison 解析 → IRGenerator 生成 Koopa IR → libkoopa 解析为 Raw Program → ASMGenerator 生成 RISC-V 汇编。 |

## 构建与使用

```bash
# 构建
make 

# 运行编译器
build/compiler -koopa test.sy -o test.S
```

## 当前支持语法 (Lv4)

```
CompUnit    ::= FuncDef
FuncDef     ::= FuncType IDENT "(" ")" Block
FuncType    ::= "int"
Block       ::= "{" Stmt "}"
Stmt        ::= "return" Exp ";"
Exp         ::= LOrExp
LOrExp      ::= LAndExp | LOrExp "||" LAndExp
LAndExp     ::= EqExp | LAndExp "&&" EqExp
EqExp       ::= RelExp | EqExp ("==" | "!=") RelExp
RelExp      ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp
AddExp      ::= MulExp | AddExp ("+" | "-") MulExp
MulExp      ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp
UnaryExp    ::= PrimaryExp | UnaryOp UnaryExp
UnaryOp     ::= "+" | "-" | "!"
PrimaryExp  ::= "(" Exp ")" | Number
Number      ::= INT_CONST
```

**表达式优先级**（从高到低）：

| 优先级 | 运算符 | 结合性 |
|--------|--------|--------|
| 最高 | `+` `-` `!` (一元) | 右结合 |
| | `*` `/` `%` | 左结合 |
| | `+` `-` | 左结合 |
| | `<` `>` `<=` `>=` | 左结合 |
| | `==` `!=` | 左结合 |
| | `&&` | 左结合 |
| 最低 | `\|\|` | 左结合 |

**一元运算降级策略**（Koopa IR 无原生一元运算，用二元运算模拟）：

| 一元运算 | Koopa IR 等价 | 说明 |
|----------|--------------|------|
| `+X` | `X` | 恒等，无操作 |
| `-X` | `sub 0, X` | 0 减操作数 |
| `!X` | `eq X, 0` | 操作数与 0 比较相等 |

**逻辑运算降级策略**（`&&` `||` 需展开为多条指令）：

| 运算符 | Koopa IR 展开 | 说明 |
|--------|--------------|------|
| `X && Y` | `ne X, 0`; `ne Y, 0`; `and t1, t2` | 先归一化为 0/1, 再按位与 |
| `X \|\| Y` | `ne X, 0`; `ne Y, 0`; `or t1, t2` | 先归一化为 0/1, 再按位或 |

**二元运算映射**:

| 运算符 | Koopa IR | RISC-V |
|--------|----------|--------|
| `+` | `add` | `add` |
| `-` | `sub` | `sub` |
| `*` | `mul` | `mul` |
| `/` | `div` | `div` |
| `%` | `mod` | `rem` |
| `<` | `lt` | `slt` |
| `>` | `gt` | `slt` (交换操作数) |
| `<=` | `le` | `slt` + `xori` |
| `>=` | `ge` | `slt` + `xori` |
| `==` | `eq` | `sub` + `sltiu` |
| `!=` | `ne` | `sub` + `sltu` |
| `&&` | `ne`×2 + `and` | (见逻辑运算降级) |
| `\|\|` | `ne`×2 + `or` | (见逻辑运算降级) |

## 关键设计

- **一种 rule 一种 AST**：对于包含 `|` 的规则，只为左侧符号设计一个 AST 类，用 tag 字段（如 `is_add`、`is_rel`）区分右侧的不同分支。
- **多字符运算符词法**：`<=` `>=` `==` `!=` `&&` `||` 由 Flex 规则统一匹配为单 Token，避免字符级歧义。
- **左递归文法**：所有二元运算符规则均为左递归，Bison 原生支持且保证左结合性（一元运算除外，`UnaryOp UnaryExp` 为右结合）。
- **运算符优先级**：通过文法层级自然编码——`UnaryExp` > `MulExp` > `AddExp` > `RelExp` > `EqExp` > `LAndExp` > `LOrExp`。
