# Lv9 数组编译器实现 

### SysY 数组语法 (Lv9)

```
ConstDef   ::= IDENT {"[" ConstExp "]"} "=" ConstInitVal
VarDef     ::= IDENT {"[" ConstExp "]"} ["=" InitVal]
LVal       ::= IDENT {"[" Exp "]"}
FuncFParam ::= BType IDENT ["[" "]" {"[" ConstExp "]"}]
```

### Koopa IR 数组类型表示

**维度顺序与 C 相反，最内层在最左边：**

| SysY | Koopa IR 类型 |
|------|--------------|
| `int` | `i32` |
| `int[10]` | `[i32, 10]` |
| `int[4][3]` | `[[i32, 3], 4]` |
| `int[2][3][4]` | `[[[i32, 4], 3], 2]` |

**函数数组参数：**

| SysY | Koopa IR 类型 | 含义 |
|------|--------------|------|
| `int arr[]` | `*i32` | 指针到 int |
| `int arr[][10]` | `*[i32, 10]` | 指针到 `int[10]` |
| `int arr[][10][20]` | `*[[i32, 20], 10]` | 指针到 `int[10][20]` |

### Koopa IR 数组指令

| 指令 | 格式 | 说明 |
|------|------|------|
| `alloc` | `@arr = alloc [[i32, 3], 2]` | 分配数组，类型是 `*[[i32, 3], 2]` |
| `global alloc` | `global @arr = alloc [i32, 3], {1, 2, 3}` | 全局数组+初始化 |
| `getelemptr` | `%0 = getelemptr @arr, i` | 数组指针运算，源必须是 `*[T, N]`，结果 `*T` |
| `getptr` | `%0 = getptr %p, i` | 通用指针运算，`ptr + i * sizeof(T)` |
| aggregate | `{1, 2, 3}` 或 `{{1,2},{3,4}}` | 常量聚合，需与类型结构匹配 |

### C 语言数组到指针隐式转换 (Array Decay)

N 维数组作为**值**使用时，自动 decay 一层（变为指向 N-1 维的指针）：

```c
int a[10];         // a     → int*             (decay 1: [10]→*i32)
int b[4][3];       // b     → int(*)[3]        (decay 1: [4][3]→*[i32,3])
                   // b[0]  → int*             (decay 2: [0]→*[i32,3]→*i32)
int c[2][3][4];    // c     → int(*)[3][4]     (decay 1: →*[[i32,4],3])
                   // c[0]  → int(*)[4]        (decay 2: →*[i32,4])
                   // c[0][0] → int*           (decay 3: →*i32)
```

### RISC-V 立即数限制

RISC-V I-type 指令（`lw`, `sw`, `addi` 等）立即数为 **12 位有符号**：**[-2048, 2047]**。

当栈帧 > 4096 字节时，`lw rd, offset(sp)` 的 offset 超限，需 `li` + `add` 间接计算地址。

### RISC-V 函数调用约定

- 前 8 个参数通过 **a0–a7** 寄存器传递
- 超过 8 个的参数通过**调用者栈**传递
- 调用者 `addi sp, sp, -N` 预留空间，`call` 后 `addi sp, sp, N` 恢复

---





## 所有测试通过情况

| 测试 | 最开始的错误 | 修复后的状态 |
|------|-------------|-------------|
| 00–04 | PASSED | PASSED |
| 05_global_arr_init | Aggregate 类型错误 | PASSED |
| 06–07 | PASSED | PASSED |
| 08_arr_access | Aggregate + 级联符号丢失 | PASSED |
| 09–11 | PASSED | PASSED |
| 12_more_arr_params | Partial decay + CALL sp 顺序 | PASSED |
| 13_complex_arr_params | Partial decay | PASSED |
| 14–17 | PASSED | PASSED |
| 18_sort4 | 参数名冲突 | PASSED |
| 19 | PASSED | PASSED |
| 20_sort6 | 参数名冲突 | PASSED |
| 21 | PASSED | PASSED |

**最终：22/22 全部通过**
