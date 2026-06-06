#pragma once

#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace cynamodb::auth {

// A simple in-memory access-key -> secret-key store used by SigV4 verification.
//
// Seeded from the environment so a local client can be configured to sign:
//   CYNAMODB_ACCESS_KEY_ID / CYNAMODB_SECRET_ACCESS_KEY  (a single credential)
// If neither is set, a documented default pair is registered so the common case
// "turn on auth and sign with these creds" works out of the box.
class CredentialStore {
public:
    static constexpr const char* kDefaultAccessKey = "cynamodb";
    static constexpr const char* kDefaultSecretKey = "cynamodb-secret";

    CredentialStore() {
        const char* ak = std::getenv("CYNAMODB_ACCESS_KEY_ID");
        const char* sk = std::getenv("CYNAMODB_SECRET_ACCESS_KEY");
        if (ak != nullptr && ak[0] != '\0' && sk != nullptr && sk[0] != '\0') {
            credentials_[ak] = sk;
        } else {
            credentials_[kDefaultAccessKey] = kDefaultSecretKey;
        }
    }

    void add_credential(const std::string& access_key, const std::string& secret_key) {
        std::lock_guard lock(mutex_);
        credentials_[access_key] = secret_key;
    }

    // Returns the secret for an access key, or nullopt if unknown.
    std::optional<std::string> secret_for(const std::string& access_key) const {
        std::lock_guard lock(mutex_);
        auto it = credentials_.find(access_key);
        if (it == credentials_.end()) return std::nullopt;
        return it->second;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::string> credentials_;
};

}  // namespace cynamodb::auth
