#pragma once

#include <memory>
#include "AST.h"
#include "KoopaIR.h"

std::unique_ptr<Program> GenerateIR(const BaseAST &ast);
