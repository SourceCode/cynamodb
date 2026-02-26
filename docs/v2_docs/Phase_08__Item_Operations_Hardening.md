# Phase 08: Full API Compliance: Item Operations Hardening

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
While the basic item operations exist, DynamoDB has hundreds of edge cases regarding attribute types, size limits, and return values (ReturnValues). This phase "hardens" the core APIs to ensure they are 1:1 compatible with the AWS specification, passing the most rigorous compliance tests.

## Technical Definition
*   **Attribute Validation:** Strict checking of data types, sizes (400KB limit), and naming rules.
*   **ReturnValues Compliance:** Correct implementation of NONE, ALL_OLD, UPDATED_OLD, ALL_NEW, UPDATED_NEW.
*   **Atomic Counters:** High-performance implementation of `ADD` in UpdateItem.

## Reference Files
*   `src/api/dispatcher.cpp`
*   `include/cynamodb/engine/table_manager.hpp`
*   `tests/test_items_http.cpp`

## Expanded Tasks
1.  **Item Size Validation:** Implement `calculate_item_size(item)`. Every attribute name counts (length in bytes) plus the value's size (e.g., Number = length of numeric string). Sum must be `<= 400,000` bytes.
2.  **Attribute Name Validation:** Enforce that attribute names cannot exceed 255 characters and must be valid UTF-8. Reject names that match reserved keywords unless they are handled via `ExpressionAttributeNames`.
3.  **PutItem ReturnValues:** In `handle_put_item`, perform a `get_item` *before* the write if `ReturnValues=ALL_OLD`. Store the old item in the response's `Attributes` field.
4.  **DeleteItem ReturnValues:** Similarly for `DeleteItem`, if `ALL_OLD` is requested, return the attributes of the item as they existed prior to the deletion.
5.  **GetItem ConsistentRead:** Implement the logic: if `ConsistentRead=true`, bypass the `BlockCache` for the MemTable check and ensure we read the latest WAL entries. If `false`, use the standard potentially-stale cache path.
6.  **GetItem Projections:** Implement `filter_attributes(item, projection)`. If an item has 100 attributes but only 2 are requested, the response must only contain those 2. Correctly handle nested attribute projections.
7.  **UpdateItem 'ADD' Action:** Implement `AttributeValue::add(other)`. For Numbers, it's arithmetic addition. For Sets (SS, NS, BS), it's a set union. If the attribute doesn't exist, treat it as `0` or an empty set.
8.  **UpdateItem 'DELETE' Action:** For Sets (SS, NS, BS), `DELETE` removes the specified elements from the existing set. This is NOT the same as removing the entire attribute (which is `REMOVE`).
9.  **UpdateItem 'REMOVE' Action:** For `REMOVE`, support both top-level attributes and nested paths like `MyMap.ChildList[2]`. If a list element is removed, shift subsequent elements to close the gap.
10. **UpdateItem 'SET' Action:** Implement `list_append(list1, list2)` and `if_not_exists(path, value)`. These must be atomic within the `UpdateItem` operation.
11. **UpdateItem ReturnValues Logic:** Implement the complex matrix. `UPDATED_NEW` only returns the attributes that were actually modified by the current request, in their new state.
12. **Null Attribute Handling:** Ensure `{"MyAttr": {"NULL": true}}` is distinct from a missing attribute. In expressions, `attribute_exists(MyAttr)` should return `true` for a Null value.
13. **Empty String/Binary Policy:** In 2020, DynamoDB started allowing empty strings and empty binary values. Update the validator to allow `{"S": ""}` and `{"B": ""}`.
14. **Number Precision:** Store numbers as `std::string` internally. Use `cpp_dec_float` (from Boost or a custom library) for calculations in `ADD` to avoid the precision loss of `double`.
15. **Binary Set (BS) Sorting:** When returning a `BS` (Binary Set), sort the entries lexicographically by their byte values to ensure deterministic JSON output.
16. **Map/List Depth Limit:** During parsing of any item or expression, maintain a `depth` counter. If it exceeds 32, return `ValidationException: Member must have length less than or equal to 32`.
17. **Provisioned Throughput Simulation:** In `CapacityManager`, if a table has 100 WCU and a 1KB write occurs, subtract 1 unit. If the unit count is 0, return `ProvisionedThroughputExceededException`.
18. **Item Collection Metrics:** If an LSI exists, track the sum of sizes of all items with the same Partition Key. If the sum > 10GB, reject any further writes to that partition.
19. **Conflict Resolution:** Ensure that if two `PutItem` requests for the same key hit the engine at the same time, the one with the higher `SequenceNumber` from the `TIDGenerator` is the final state.
20. **Error Mapping:** Map a `std::system_error` with `ENOSPC` to `InternalServerError` with the message "Insufficient disk space for operation".
21. **Batch Validation:** Create `validate_item_standard(item)` and reuse it across `PutItem`, `BatchWriteItem`, and `TransactWriteItems` to ensure consistent behavior.
22. **Key Type Validation:** If a table schema defines `PK` as `S`, and a request provides `{"PK": {"N": "123"}}`, reject it with `ValidationException: Type mismatch for key`.
23. **Test Coverage - Large Items:** Add a test in `test_items_http.cpp` that generates a 399,999 byte string and verifies it is accepted and correctly retrieved.
24. **Test Coverage - Nested Updates:** Add a test: `UpdateExpression: "SET MyMap.#c.#l[1] = :v"`. Verify that if `MyMap` exists but `#c` doesn't, the operation fails or creates intermediate maps based on the path.
25. **Validation:** Use the `aws-cli` to perform the same operations on a real DynamoDB instance and CynamoDB, comparing the JSON output for exact parity.

## Validation Criteria
*   **Compliance:** Passes all tests in `tests/test_items_http.cpp`.
*   **Performance:** A standard `PutItem` (without condition) takes < 2ms p99 on SSD.
*   **Reliability:** No memory leaks when handling 100k invalid requests (e.g., type mismatches).
