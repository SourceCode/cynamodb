#pragma once

#include <string>
#include <vector>
#include <expected>
#include <cynamodb/expressions/partiql/ast.hpp>

namespace cynamodb::expressions::partiql {

enum class PartiQLError {
    InvalidSyntax,
    UnsupportedStatement,
    InternalError
};

class PartiQLParser {
public:
    static std::expected<PartiQLStatement, PartiQLError> parse(std::string_view statement);
};

} // namespace cynamodb::expressions::partiql
