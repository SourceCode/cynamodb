#include <cynamodb/expressions/partiql/parser.hpp>
#include <algorithm>

namespace cynamodb::expressions::partiql {

std::expected<PartiQLStatement, PartiQLError> PartiQLParser::parse(std::string_view statement) {
    std::string stmt(statement);
    std::transform(stmt.begin(), stmt.end(), stmt.begin(), [](unsigned char c) { return std::toupper(c); });

    if (stmt.starts_with("SELECT")) {
        SelectStatement sel;
        // Basic naive parsing for demo
        size_t from_pos = stmt.find("FROM");
        if (from_pos != std::string::npos) {
            sel.table_name = "MockTable"; // Placeholder
            return PartiQLStatement{std::move(sel)};
        }
    }

    return std::unexpected(PartiQLError::UnsupportedStatement);
}

} // namespace cynamodb::expressions::partiql
