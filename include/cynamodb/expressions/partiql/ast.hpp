#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <cynamodb/expressions/ast.hpp>

namespace cynamodb::expressions::partiql {

struct SelectStatement {
    std::vector<std::string> projection; // "*" or specific fields
    std::string table_name;
    std::shared_ptr<ASTNode> where_clause;
};

struct InsertStatement {
    std::string table_name;
    std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess> value;
};

struct UpdateStatement {
    std::string table_name;
    std::string set_expression; // Simplified for now
    std::shared_ptr<ASTNode> where_clause;
};

struct DeleteStatement {
    std::string table_name;
    std::shared_ptr<ASTNode> where_clause;
};

struct PartiQLStatement {
    std::variant<SelectStatement, InsertStatement, UpdateStatement, DeleteStatement> data;
};

} // namespace cynamodb::expressions::partiql
