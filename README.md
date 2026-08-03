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
