#pragma once

#include <memory>
#include <string>
#include <vector>
#include <variant>
#include <cynamodb/core/types.hpp>

namespace cynamodb::expressions {

struct ASTNode;

struct LiteralNode {
    std::shared_ptr<core::AttributeValue> value;
};

struct BinaryOpNode {
    std::string op;
    std::shared_ptr<ASTNode> left;
    std::shared_ptr<ASTNode> right;
};

struct IdentifierNode {
    std::string name;
};

struct PlaceholderNode {
    std::string name; // #name or :value
};

struct FunctionCallNode {
    std::string function_name;
    std::vector<std::shared_ptr<ASTNode>> arguments;
};

struct ASTNode {
    std::variant<LiteralNode, BinaryOpNode, IdentifierNode, PlaceholderNode, FunctionCallNode> data;
};

} // namespace cynamodb::expressions
