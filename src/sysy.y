%code requires {
  #include <memory>
  #include <string>
  #include "AST.h"
}

%{

#include "AST.h"
#include <iostream>
#include <memory>
#include <string>

int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);

using namespace std;
%}

// 定义 parser 函数和错误处理函数的附加参数
// 我们需要返回一个字符串作为 AST, 所以我们把附加参数定义成字符串的智能指针
// 解析完成后, 我们要手动修改这个参数, 把它设置成解析得到的字符串
%parse-param { std::unique_ptr<BaseAST> &ast }

// yylval 的定义, 我们把它定义成了一个联合体 (union)
// 因为 token 的值有的是字符串指针, 有的是整数
// 之前我们在 lexer 中用到的 str_val 和 int_val 就是在这里被定义的
// 至于为什么要用字符串指针而不直接用 string 或者 unique_ptr<string>?
// 请自行 STFW 在 union 里写一个带析构函数的类会出现什么情况
%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val;
}

// lexer 返回的所有 token 种类的声明
// 注意 IDENT 和 INT_CONST 会返回 token 的值, 分别对应 str_val 和 int_val
%token INT RETURN
%token CONST
%token LE GE EQ NE LAND LOR
%token <str_val> IDENT
%token <int_val> INT_CONST

// 非终结符的类型定义
%type <ast_val> FuncDef FuncType Block Stmt Exp LOrExp LAndExp EqExp RelExp AddExp MulExp UnaryExp PrimaryExp
%type <ast_val> Decl ConstDecl ConstDef ConstInitVal ConstExp VarDecl VarDef InitVal BlockItem BType LVal
%type <ast_val> BlockItemList ConstDefList VarDefList
%type <str_val> UnaryOp
%type <int_val> Number

%%

// 开始符, CompUnit ::= FuncDef
CompUnit
  : FuncDef {
    auto comp_unit = make_unique<CompUnitAST>();
    comp_unit->func_def = unique_ptr<BaseAST>($1);
    ast = move(comp_unit);
  }
  ;

// FuncDef ::= FuncType IDENT '(' ')' Block;
FuncDef
  : FuncType IDENT '(' ')' Block {
    auto ast = new FuncDefAST();
    ast->func_type = unique_ptr<BaseAST>($1);
    ast->ident = *unique_ptr<string>($2);
    ast->block = unique_ptr<BaseAST>($5);
    $$ = ast;
  }
  ;

// FuncType ::= "int"
FuncType
  : INT {
    auto ast = new FuncTypeAST();
    ast->type = "int";
    $$ = ast;
  }
  ;

// ==================== Lv4: Block ====================
// Block ::= "{" {BlockItem} "}"
Block
  : '{' BlockItemList '}' {
    $$ = $2;
  }
  ;

BlockItemList
  : /* empty */ {
    auto ast = new BlockAST();
    $$ = ast;
  }
  | BlockItemList BlockItem {
    auto ast = dynamic_cast<BlockAST*>($1);
    ast->items.push_back(unique_ptr<BaseAST>($2));
    $$ = ast;
  }
  ;

// BlockItem ::= Decl | Stmt
BlockItem
  : Decl {
    auto ast = new BlockItemAST();
    ast->is_stmt = false;
    ast->item = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | Stmt {
    auto ast = new BlockItemAST();
    ast->is_stmt = true;
    ast->item = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

// ==================== Lv4: Decl ====================
// Decl ::= ConstDecl | VarDecl
Decl
  : ConstDecl {
    auto ast = new DeclAST();
    ast->is_const = true;
    ast->decl_body = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | VarDecl {
    auto ast = new DeclAST();
    ast->is_const = false;
    ast->decl_body = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

// BType ::= "int"
BType
  : INT {
    auto ast = new BTypeAST();
    ast->type = "int";
    $$ = ast;
  }
  ;

// ConstDecl ::= "const" BType ConstDef {"," ConstDef} ";"
ConstDecl
  : CONST BType ConstDef ConstDefList ';' {
    auto ast = new ConstDeclAST();
    ast->btype = unique_ptr<BaseAST>($2);
    ast->const_defs.push_back(unique_ptr<BaseAST>($3));
    // ConstDefList 返回的 vector 已被合并
    auto list_ast = dynamic_cast<ConstDeclAST*>(const_cast<BaseAST*>($4));
    if (list_ast) {
      for (auto &def : list_ast->const_defs) {
        ast->const_defs.push_back(std::move(def));
      }
      delete list_ast;
    }
    $$ = ast;
  }
  ;

// ConstDefList: 对应 {"," ConstDef}, 即零次或多次 ",ConstDef"
// 我们用 ConstDeclAST 临时承载 const_defs vector
ConstDefList
  : /* empty */ {
    auto ast = new ConstDeclAST();
    ast->btype = nullptr;  // dummy
    $$ = ast;
  }
  | ConstDefList ',' ConstDef {
    auto ast = dynamic_cast<ConstDeclAST*>($1);
    ast->const_defs.push_back(unique_ptr<BaseAST>($3));
    $$ = ast;
  }
  ;

// ConstDef ::= IDENT "=" ConstInitVal
ConstDef
  : IDENT '=' ConstInitVal {
    auto ast = new ConstDefAST();
    ast->ident = *unique_ptr<string>($1);
    ast->init_val = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// ConstInitVal ::= ConstExp
ConstInitVal
  : ConstExp {
    auto ast = new ConstInitValAST();
    ast->exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

// ConstExp ::= Exp  (语法同 Exp, 但语义上仅允许常量)
ConstExp
  : Exp { $$ = $1; }
  ;

// VarDecl ::= BType VarDef {"," VarDef} ";"
VarDecl
  : BType VarDef VarDefList ';' {
    auto ast = new VarDeclAST();
    ast->btype = unique_ptr<BaseAST>($1);
    ast->var_defs.push_back(unique_ptr<BaseAST>($2));
    auto list_ast = dynamic_cast<VarDeclAST*>(const_cast<BaseAST*>($3));
    if (list_ast) {
      for (auto &def : list_ast->var_defs) {
        ast->var_defs.push_back(std::move(def));
      }
      delete list_ast;
    }
    $$ = ast;
  }
  ;

// VarDefList: 对应 {"," VarDef}
VarDefList
  : /* empty */ {
    auto ast = new VarDeclAST();
    ast->btype = nullptr;  // dummy
    $$ = ast;
  }
  | VarDefList ',' VarDef {
    auto ast = dynamic_cast<VarDeclAST*>($1);
    ast->var_defs.push_back(unique_ptr<BaseAST>($3));
    $$ = ast;
  }
  ;

// VarDef ::= IDENT | IDENT "=" InitVal
VarDef
  : IDENT {
    auto ast = new VarDefAST();
    ast->ident = *unique_ptr<string>($1);
    ast->has_init = false;
    $$ = ast;
  }
  | IDENT '=' InitVal {
    auto ast = new VarDefAST();
    ast->ident = *unique_ptr<string>($1);
    ast->has_init = true;
    ast->init_val = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// InitVal ::= Exp
InitVal
  : Exp {
    auto ast = new InitValAST();
    ast->exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

// ==================== Lv4: LVal ====================
// LVal ::= IDENT
LVal
  : IDENT {
    auto ast = new LValAST();
    ast->ident = *unique_ptr<string>($1);
    $$ = ast;
  }
  ;

// ==================== Stmt ====================
// Stmt ::= "return" Exp ";" | LVal "=" Exp ";"
Stmt
  : RETURN Exp ';' {
    auto ast = new StmtAST();
    ast->is_return = true;
    ast->exp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  | LVal '=' Exp ';' {
    auto ast = new StmtAST();
    ast->is_return = false;
    ast->lval = unique_ptr<BaseAST>($1);
    ast->assign_exp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// ==================== 表达式层次 ====================

Exp
  : LOrExp {
    auto ast = new ExpAST();
    ast->lor_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

LOrExp
  : LAndExp {
    auto ast = new LOrExpAST();
    ast->is_land = true;
    ast->land_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | LOrExp LOR LAndExp {
    auto ast = new LOrExpAST();
    ast->is_land = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "||";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

LAndExp
  : EqExp {
    auto ast = new LAndExpAST();
    ast->is_eq = true;
    ast->eq_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | LAndExp LAND EqExp {
    auto ast = new LAndExpAST();
    ast->is_eq = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "&&";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

EqExp
  : RelExp {
    auto ast = new EqExpAST();
    ast->is_rel = true;
    ast->rel_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | EqExp EQ RelExp {
    auto ast = new EqExpAST();
    ast->is_rel = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "==";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | EqExp NE RelExp {
    auto ast = new EqExpAST();
    ast->is_rel = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "!=";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

RelExp
  : AddExp {
    auto ast = new RelExpAST();
    ast->is_add = true;
    ast->add_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | RelExp '<' AddExp {
    auto ast = new RelExpAST();
    ast->is_add = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "<";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | RelExp '>' AddExp {
    auto ast = new RelExpAST();
    ast->is_add = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = ">";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | RelExp LE AddExp {
    auto ast = new RelExpAST();
    ast->is_add = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "<=";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | RelExp GE AddExp {
    auto ast = new RelExpAST();
    ast->is_add = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = ">=";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

AddExp
  : MulExp {
    auto ast = new AddExpAST();
    ast->is_mul = true;
    ast->mul_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | AddExp '+' MulExp {
    auto ast = new AddExpAST();
    ast->is_mul = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "+";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | AddExp '-' MulExp {
    auto ast = new AddExpAST();
    ast->is_mul = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "-";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

MulExp
  : UnaryExp {
    auto ast = new MulExpAST();
    ast->is_unary = true;
    ast->unary_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | MulExp '*' UnaryExp {
    auto ast = new MulExpAST();
    ast->is_unary = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "*";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | MulExp '/' UnaryExp {
    auto ast = new MulExpAST();
    ast->is_unary = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "/";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | MulExp '%' UnaryExp {
    auto ast = new MulExpAST();
    ast->is_unary = false;
    ast->lhs = unique_ptr<BaseAST>($1);
    ast->op = "%";
    ast->rhs = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

UnaryExp
  : PrimaryExp {
    auto ast = new UnaryExpAST();
    ast->is_primary = true;
    ast->primary_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | UnaryOp UnaryExp {
    auto ast = new UnaryExpAST();
    ast->is_primary = false;
    ast->op = *unique_ptr<string>($1);
    ast->unary_exp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  ;

UnaryOp
  : '+' { $$ = new string("+"); }
  | '-' { $$ = new string("-"); }
  | '!' { $$ = new string("!"); }
  ;

// PrimaryExp ::= "(" Exp ")" | LVal | Number
PrimaryExp
  : '(' Exp ')' {
    auto ast = new PrimaryExpAST();
    ast->is_number = false;
    ast->is_lval = false;
    ast->exp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  | LVal {
    auto ast = new PrimaryExpAST();
    ast->is_number = false;
    ast->is_lval = true;
    ast->ident = dynamic_cast<LValAST*>($1)->ident;
    delete $1;
    $$ = ast;
  }
  | Number {
    auto ast = new PrimaryExpAST();
    ast->is_number = true;
    ast->is_lval = false;
    ast->number = $1;
    $$ = ast;
  }
  ;

Number
  : INT_CONST {
    $$ = $1;
  }
  ;

%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(unique_ptr<BaseAST> &ast, const char *s) {
  cerr << "error: " << s << endl;
}
