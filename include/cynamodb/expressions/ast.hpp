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

// A document path: a root attribute followed by zero or more map-key or
// list-index steps, e.g. `a`, `a.b.c`, `a[0].b`, `#a.#b[2]`. Each name segment
// may be a literal identifier or a `#placeholder` resolved via
// ExpressionAttributeNames.
struct PathSegment {
    enum class Kind { Name, Index };
    Kind kind = Kind::Name;
    std::string name;     // for Kind::Name (literal identifier or "#placeholder")
    size_t index = 0;     // for Kind::Index
};

struct PathNode {
    std::vector<PathSegment> segments;
};

struct FunctionCallNode {
    std::string function_name;
    std::vector<std::shared_ptr<ASTNode>> arguments;
};

struct ASTNode {
    std::variant<LiteralNode, BinaryOpNode, IdentifierNode, PlaceholderNode, FunctionCallNode, PathNode> data;
};

} // namespace cynamodb::expressions
