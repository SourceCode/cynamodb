#pragma once

#include <cynamodb/expressions/ast.hpp>
#include <cynamodb/core/types.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace cynamodb::expressions {

class Evaluator {
public:
    static bool evaluate_condition(
        const ASTNode& node,
        const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
        const std::map<std::string, std::string, core::StringViewLess>& names,
        const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values);

    static std::shared_ptr<core::AttributeValue> evaluate_expression(
        const ASTNode& node,
        const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
        const std::map<std::string, std::string, core::StringViewLess>& names,
        const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values);

    static std::optional<std::string> get_attribute_name(
        const ASTNode& node,
        const std::map<std::string, std::string, core::StringViewLess>& names);

    // Resolves a document path against an item, returning the value at the path or
    // nullptr if any step is missing / type-mismatched.
    static std::shared_ptr<core::AttributeValue> resolve_path(
        const PathNode& path,
        const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
        const std::map<std::string, std::string, core::StringViewLess>& names);

private:
    static bool evaluate_condition_impl(
        const ASTNode& node,
        const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
        const std::map<std::string, std::string, core::StringViewLess>& names,
        const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values,
        size_t depth);
};

} // namespace cynamodb::expressions
