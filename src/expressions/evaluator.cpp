#include <cynamodb/expressions/evaluator.hpp>
#include <algorithm>
#include <cctype>

namespace cynamodb::expressions {

namespace {

bool is_valid_identifier(std::string_view value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.';
    });
}

std::string to_upper_copy(std::string_view input) {
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

bool compare_values(
    const std::shared_ptr<core::AttributeValue>& left,
    const std::shared_ptr<core::AttributeValue>& right,
    std::string_view op) {
    if (!left || !right) return false;

    if (left->type == core::AttributeType::S && right->type == core::AttributeType::S) {
        const auto& l = std::get<core::String>(left->value);
        const auto& r = std::get<core::String>(right->value);
        if (op == "=") return l == r;
        if (op == "<>") return l != r;
        if (op == "<") return l < r;
        if (op == "<=") return l <= r;
        if (op == ">") return l > r;
        if (op == ">=") return l >= r;
        return false;
    }

    if (left->type == core::AttributeType::N && right->type == core::AttributeType::N) {
        const auto& l = std::get<core::String>(left->value);
        const auto& r = std::get<core::String>(right->value);
        if (op == "=") return l == r;
        if (op == "<>") return l != r;
        return false;
    }

    if (left->type == core::AttributeType::BOOL && right->type == core::AttributeType::BOOL) {
        const bool l = std::get<bool>(left->value);
        const bool r = std::get<bool>(right->value);
        if (op == "=") return l == r;
        if (op == "<>") return l != r;
        return false;
    }

    if (left->type == core::AttributeType::NUL && right->type == core::AttributeType::NUL) {
        if (op == "=") return true;
        if (op == "<>") return false;
    }

    return false;
}

} // namespace

bool Evaluator::evaluate_condition(
    const ASTNode& node,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
    const std::map<std::string, std::string, core::StringViewLess>& names,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values) {
    return evaluate_condition_impl(node, item, names, values, 0);
}

bool Evaluator::evaluate_condition_impl(
    const ASTNode& node,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
    const std::map<std::string, std::string, core::StringViewLess>& names,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values,
    size_t depth) {
    if (depth > 128) return false;

    if (std::holds_alternative<BinaryOpNode>(node.data)) {
        const auto& bin = std::get<BinaryOpNode>(node.data);
        // Gate 4: Short-Circuit Evaluation
        if (bin.op == "AND") {
            if (!evaluate_condition_impl(*bin.left, item, names, values, depth + 1)) return false;
            return evaluate_condition_impl(*bin.right, item, names, values, depth + 1);
        }
        if (bin.op == "OR") {
            if (evaluate_condition_impl(*bin.left, item, names, values, depth + 1)) return true;
            return evaluate_condition_impl(*bin.right, item, names, values, depth + 1);
        }
        
        auto left_val = evaluate_expression(*bin.left, item, names, values);
        auto right_val = evaluate_expression(*bin.right, item, names, values);
        return compare_values(left_val, right_val, bin.op);
    }

    if (std::holds_alternative<FunctionCallNode>(node.data)) {
        const auto& func = std::get<FunctionCallNode>(node.data);
        const std::string fn = to_upper_copy(func.function_name);

        if (fn == "NOT" && func.arguments.size() == 1) {
            return !evaluate_condition_impl(*func.arguments[0], item, names, values, depth + 1);
        }

        if ((fn == "ATTRIBUTE_EXISTS" || fn == "ATTRIBUTE_NOT_EXISTS") && func.arguments.size() == 1) {
            auto attr_name = get_attribute_name(*func.arguments[0], names);
            bool exists = attr_name && item.find(*attr_name) != item.end();
            return (fn == "ATTRIBUTE_EXISTS") ? exists : !exists;
        }
    }

    return false;
}

std::shared_ptr<core::AttributeValue> Evaluator::evaluate_expression(
    const ASTNode& node,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
    const std::map<std::string, std::string, core::StringViewLess>& names,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values) {
    
    if (std::holds_alternative<LiteralNode>(node.data)) {
        return std::get<LiteralNode>(node.data).value;
    }

    if (std::holds_alternative<IdentifierNode>(node.data)) {
        const auto& id = std::get<IdentifierNode>(node.data);
        auto it = item.find(id.name);
        if (it != item.end()) return it->second;
        return nullptr;
    }

    if (std::holds_alternative<PlaceholderNode>(node.data)) {
        const auto& ph = std::get<PlaceholderNode>(node.data);
        if (ph.name.starts_with("#")) {
            const auto it = names.find(ph.name);
            if (it == names.end() || !is_valid_identifier(it->second)) {
                return nullptr;
            }
            const auto item_it = item.find(it->second);
            if (item_it != item.end()) return item_it->second;
            return nullptr;
        }
        if (ph.name.starts_with(":")) {
            const auto it = values.find(ph.name);
            if (it != values.end()) return it->second;
            return nullptr;
        }
    }

    return nullptr;
}

std::optional<std::string> Evaluator::get_attribute_name(
    const ASTNode& node,
    const std::map<std::string, std::string, core::StringViewLess>& names) {
    if (std::holds_alternative<IdentifierNode>(node.data)) {
        return std::get<IdentifierNode>(node.data).name;
    }
    if (std::holds_alternative<PlaceholderNode>(node.data)) {
        const auto& ph = std::get<PlaceholderNode>(node.data);
        if (ph.name.starts_with("#")) {
            auto it = names.find(ph.name);
            if (it != names.end()) return it->second;
        }
    }
    return std::nullopt;
}

} // namespace cynamodb::expressions
