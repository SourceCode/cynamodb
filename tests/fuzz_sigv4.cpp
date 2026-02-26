#include <cynamodb/auth/sigv4.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    std::string_view auth_header(reinterpret_cast<const char*>(Data), Size);
    
    try {
        cynamodb::auth::SigV4Parser::parse_authorization_header(auth_header);
    } catch (...) {
        // Ignore
    }
    
    return 0;
}
