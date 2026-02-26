#pragma once

#include <string>
#include <memory>
#include <cynamodb/engine/lsm/lsm_engine.hpp>

namespace cynamodb::engine::recovery {

class RecoveryManager {
public:
    explicit RecoveryManager(std::shared_ptr<lsm::LsmEngine> engine);

    bool recover_from_wal(const std::string& wal_path);

private:
    std::shared_ptr<lsm::LsmEngine> engine_;
};

} // namespace cynamodb::engine::recovery
