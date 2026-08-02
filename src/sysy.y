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

%parse-param { std::unique_ptr<BaseAST> &ast }

%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val;
}

// ==================== Token 声明 ====================
%token INT VOID RETURN
%token CONST
%token IF ELSE WHILE BREAK CONTINUE
%token LE GE EQ NE LAND LOR
%token <str_val> IDENT
%token <int_val> INT_CONST

// ==================== 非终结符类型定义 ====================
%type <ast_val> CompUnit CompUnitItemList
%type <ast_val> FuncDef Type Block Stmt Exp LOrExp LAndExp EqExp RelExp AddExp MulExp UnaryExp PrimaryExp
%type <ast_val> Decl ConstDecl ConstDef ConstInitVal ConstExp VarDecl VarDef InitVal BlockItem LVal
%type <ast_val> BlockItemList ConstDefList VarDefList
%type <ast_val> MatchedStmt OpenStmt
%type <ast_val> FuncFParam FuncFParams FuncRParams
%type <str_val> UnaryOp
%type <int_val> Number

%%

// ==================== CompUnit ====================
// CompUnit ::= [CompUnit] (Decl | FuncDef)
CompUnit
  : CompUnitItemList {
    auto comp_unit = dynamic_cast<CompUnitAST*>($1);
    if (!comp_unit) {
      // 单个 Decl 或 FuncDef
      comp_unit = new CompUnitAST();
      comp_unit->items.push_back(unique_ptr<BaseAST>($1));
    }
    ast = unique_ptr<BaseAST>(comp_unit);
  }
  ;

CompUnitItemList
  : Decl {
    auto comp = new CompUnitAST();
    comp->items.push_back(unique_ptr<BaseAST>($1));
    $$ = comp;
  }
  | FuncDef {
    auto comp = new CompUnitAST();
    comp->items.push_back(unique_ptr<BaseAST>($1));
    $$ = comp;
  }
  | CompUnitItemList Decl {
    auto comp = dynamic_cast<CompUnitAST*>($1);
    comp->items.push_back(unique_ptr<BaseAST>($2));
    $$ = comp;
  }
  | CompUnitItemList FuncDef {
    auto comp = dynamic_cast<CompUnitAST*>($1);
    comp->items.push_back(unique_ptr<BaseAST>($2));
    $$ = comp;
  }
  ;

// ==================== FuncDef ====================
// FuncDef ::= Type IDENT "(" [FuncFParams] ")" Block
FuncDef
  : Type IDENT '(' ')' Block {
    auto def = new FuncDefAST();
    def->func_type = unique_ptr<BaseAST>($1);
    def->ident = *unique_ptr<string>($2);
    def->has_params = false;
    def->block = unique_ptr<BaseAST>($5);
    $$ = def;
  }
  | Type IDENT '(' FuncFParams ')' Block {
    auto def = new FuncDefAST();
    def->func_type = unique_ptr<BaseAST>($1);
    def->ident = *unique_ptr<string>($2);
    def->has_params = true;
    // FuncFParams 返回的是 FuncRParamsAST (复用), 提取 exps 作为参数列表
    auto *params = dynamic_cast<FuncRParamsAST*>($4);
    if (params) {
      for (auto &p : params->exps) {
        def->params.push_back(std::move(p));
      }
      delete params;
    }
    def->block = unique_ptr<BaseAST>($6);
    $$ = def;
  }
  ;

// FuncFParams ::= FuncFParam {"," FuncFParam}
// 复用 FuncRParamsAST 存储参数列表
FuncFParams
  : FuncFParam {
    auto params = new FuncRParamsAST();
    params->exps.push_back(unique_ptr<BaseAST>($1));
    $$ = params;
  }
  | FuncFParams ',' FuncFParam {
    auto params = dynamic_cast<FuncRParamsAST*>($1);
    params->exps.push_back(unique_ptr<BaseAST>($3));
    $$ = params;
  }
  ;

// FuncFParam ::= Type IDENT
FuncFParam
  : Type IDENT {
    auto param = new FuncFParamAST();
    param->btype = unique_ptr<BaseAST>($1);
    param->ident = *unique_ptr<string>($2);
    $$ = param;
  }
  ;

// Type ::= "void" | "int"
// 将 FuncType 和 BType 合并为统一 Type, 消除 INT 的 reduce/reduce 冲突
Type
  : INT {
    auto ast = new FuncTypeAST();
    ast->type = "int";
    $$ = ast;
  }
  | VOID {
    auto ast = new FuncTypeAST();
    ast->type = "void";
    $$ = ast;
  }
  ;

// ==================== Block ====================
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

// ==================== Decl ====================
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

// ConstDecl ::= "const" Type ConstDef {"," ConstDef} ";"
ConstDecl
  : CONST Type ConstDef ConstDefList ';' {
    auto ast = new ConstDeclAST();
    ast->btype = unique_ptr<BaseAST>($2);
    ast->const_defs.push_back(unique_ptr<BaseAST>($3));
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

// ConstDefList: 对应 {"," ConstDef}
ConstDefList
  : /* empty */ {
    auto ast = new ConstDeclAST();
    ast->btype = nullptr;
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

// ConstExp ::= Exp
ConstExp
  : Exp { $$ = $1; }
  ;

// VarDecl ::= Type VarDef {"," VarDef} ";"
VarDecl
  : Type VarDef VarDefList ';' {
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
    ast->btype = nullptr;
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

// ==================== LVal ====================
LVal
  : IDENT {
    auto ast = new LValAST();
    ast->ident = *unique_ptr<string>($1);
    $$ = ast;
  }
  ;

// ==================== FuncRParams ====================
// FuncRParams ::= Exp {"," Exp}
FuncRParams
  : Exp {
    auto params = new FuncRParamsAST();
    params->exps.push_back(unique_ptr<BaseAST>($1));
    $$ = params;
  }
  | FuncRParams ',' Exp {
    auto params = dynamic_cast<FuncRParamsAST*>($1);
    params->exps.push_back(unique_ptr<BaseAST>($3));
    $$ = params;
  }
  ;

// ==================== Stmt ====================
Stmt
  : MatchedStmt { $$ = $1; }
  | OpenStmt   { $$ = $1; }
  ;

// MatchedStmt
MatchedStmt
  : RETURN Exp ';' {
    auto ast = new StmtAST();
    ast->kind = StmtAST::RETURN;
    ast->has_ret_val = true;
    ast->exp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  | RETURN ';' {
    auto ast = new StmtAST();
    ast->kind = StmtAST::RETURN;
    ast->has_ret_val = false;
    ast->exp = nullptr;
    $$ = ast;
  }
  | LVal '=' Exp ';' {
    auto ast = new StmtAST();
    ast->kind = StmtAST::ASSIGN;
    ast->lval = unique_ptr<BaseAST>($1);
    ast->assign_exp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | Exp ';' {
    auto ast = new StmtAST();
    ast->kind = StmtAST::EXP_STMT;
    ast->exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | ';' {
    auto ast = new StmtAST();
    ast->kind = StmtAST::EXP_STMT;
    ast->exp = nullptr;
    $$ = ast;
  }
  | Block {
    auto ast = new StmtAST();
    ast->kind = StmtAST::BLOCK;
    ast->block = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | IF '(' Exp ')' MatchedStmt ELSE MatchedStmt {
    auto ast = new StmtAST();
    ast->kind = StmtAST::IF_ELSE;
    ast->exp = unique_ptr<BaseAST>($3);
    ast->then_stmt = unique_ptr<BaseAST>($5);
    ast->else_stmt = unique_ptr<BaseAST>($7);
    $$ = ast;
  }
  | WHILE '(' Exp ')' MatchedStmt {
    auto ast = new StmtAST();
    ast->kind = StmtAST::WHILE;
    ast->exp = unique_ptr<BaseAST>($3);
    ast->body = unique_ptr<BaseAST>($5);
    $$ = ast;
  }
  | BREAK ';' {
    auto ast = new StmtAST();
    ast->kind = StmtAST::BREAK;
    $$ = ast;
  }
  | CONTINUE ';' {
    auto ast = new StmtAST();
    ast->kind = StmtAST::CONTINUE;
    $$ = ast;
  }
  ;

// OpenStmt
OpenStmt
  : IF '(' Exp ')' Stmt {
    auto ast = new StmtAST();
    ast->kind = StmtAST::IF_ELSE;
    ast->exp = unique_ptr<BaseAST>($3);
    ast->then_stmt = unique_ptr<BaseAST>($5);
    ast->else_stmt = nullptr;
    $$ = ast;
  }
  | IF '(' Exp ')' MatchedStmt ELSE OpenStmt {
    auto ast = new StmtAST();
    ast->kind = StmtAST::IF_ELSE;
    ast->exp = unique_ptr<BaseAST>($3);
    ast->then_stmt = unique_ptr<BaseAST>($5);
    ast->else_stmt = unique_ptr<BaseAST>($7);
    $$ = ast;
  }
  | WHILE '(' Exp ')' OpenStmt {
    auto ast = new StmtAST();
    ast->kind = StmtAST::WHILE;
    ast->exp = unique_ptr<BaseAST>($3);
    ast->body = unique_ptr<BaseAST>($5);
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

// UnaryExp ::= PrimaryExp
//            | IDENT "(" [FuncRParams] ")"   // 函数调用
//            | UnaryOp UnaryExp
UnaryExp
  : PrimaryExp {
    auto ast = new UnaryExpAST();
    ast->kind = UnaryExpAST::PRIMARY;
    ast->primary_exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | IDENT '(' ')' {
    auto ast = new UnaryExpAST();
    ast->kind = UnaryExpAST::CALL;
    ast->func_name = *unique_ptr<string>($1);
    ast->has_args = false;
    $$ = ast;
  }
  | IDENT '(' FuncRParams ')' {
    auto ast = new UnaryExpAST();
    ast->kind = UnaryExpAST::CALL;
    ast->func_name = *unique_ptr<string>($1);
    ast->has_args = true;
    auto *rparams = dynamic_cast<FuncRParamsAST*>($3);
    if (rparams) {
      for (auto &e : rparams->exps) ast->args.push_back(std::move(e));
      delete rparams;
    }
    $$ = ast;
  }
  | UnaryOp UnaryExp {
    auto ast = new UnaryExpAST();
    ast->kind = UnaryExpAST::UNARY_OP;
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

void yyerror(unique_ptr<BaseAST> &ast, const char *s) {
  cerr << "error: " << s << endl;
}
