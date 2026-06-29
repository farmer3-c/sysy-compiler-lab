#include <cassert>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <sstream>
#include "AST.h"
#include "KoopaIR.h"
#include "IRGenerator.h"
#include "koopa.h"
#include "ASMGenerator.h"


using namespace std;

extern FILE *yyin;
extern int yyparse(unique_ptr<BaseAST> &ast);

int main(int argc, const char *argv[]) {
  assert(argc == 5);
  auto mode = argv[1];
  auto input = argv[2];
  auto output = argv[4];

  yyin = fopen(input, "r");
  assert(yyin);

  unique_ptr<BaseAST> ast;
  auto ret = yyparse(ast);
  assert(!ret);

  /*
  // // 生成 Koopa IR
  // auto program = GenerateIR(*ast);
  
  // // 将 IR 输出到文件
  // ofstream ofs(output);
  // assert(ofs.is_open());
  
  // // 重定向 cout 到文件
  // streambuf *old_cout = cout.rdbuf();
  // cout.rdbuf(ofs.rdbuf());
  
  // program->Dump();
  
  // // 恢复 cout
  // cout.rdbuf(old_cout);
lv 1 */

//建立内存形式的 Koopa IR, 并在程序中访问这些数据结构

// 1. 生成 Koopa IR 对象
auto program = GenerateIR(*ast);

// 2. 把 Program Dump 到 string 中（而不是直接到文件）
ostringstream oss;
streambuf *old_cout = cout.rdbuf();
cout.rdbuf(oss.rdbuf());
program->Dump();
cout.rdbuf(old_cout);

string koopa_str = oss.str();

// 3. 用 libkoopa 解析这个 string，得到内存形式的 Koopa IR
koopa_program_t koopa_program;
koopa_error_code_t ret2 = koopa_parse_from_string(koopa_str.c_str(), &koopa_program);
assert(ret2 == KOOPA_EC_SUCCESS);

// 4. 转换为可遍历的 raw program
koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
koopa_raw_program_t raw = koopa_build_raw_program(builder, koopa_program);
koopa_delete_program(koopa_program);

// 5. 生成 RISC-V 汇编


ofstream ofs(output);
ASMGenerator generator(ofs);
generator.Generate(raw);


// 6. 释放
koopa_delete_raw_program_builder(builder);
  return 0;
}
