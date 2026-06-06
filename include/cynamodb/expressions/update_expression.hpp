#pragma once

#include <map>
#include <memory>
#include <string>

#include <cynamodb/core/types.hpp>

namespace cynamodb::expressions {

using ItemMap = std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>;
using NameMap = std::map<std::string, std::string, core::StringViewLess>;
using ValueMap = std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>;

struct UpdateResult {
    bool ok = true;
    std::string error;  // populated when ok == false
};

// Applies a DynamoDB UpdateExpression to `item` in place, supporting the
// SET / REMOVE / ADD / DELETE clauses over top-level attribute paths.
//
// SET    target = operand            (operand: :v | path | if_not_exists(path, op)
//                                       | list_append(op, op) | op +/- op)
// REMOVE target [, ...]
// ADD    path :value                 (numeric increment, or set union)
// DELETE path :value                 (set element removal)
//
// ExpressionAttributeNames (#n) and ExpressionAttributeValues (:v) are resolved
// from `names` / `values`. Returns ok=false with a message on any malformed
// expression or unresolved placeholder.
UpdateResult apply_update_expression(const std::string& expression, ItemMap& item,
                                     const NameMap& names, const ValueMap& values);

}  // namespace cynamodb::expressions
