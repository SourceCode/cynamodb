# Changelog

All notable changes to the cynamoDB project will be documented in this file.

## [2.1.2] - 2026-02-26

### Added
- **AI Agent Specification**: Added `AGENTS.md` optimized for LLM-based agents.
- **Enterprise Documentation Suite**: Comprehensive documentation covering architecture, setup, and compliance.
- **Repository Hardening**: Added `.gitignore` and `LICENSE`.
- **Doxygen Documentation**: Initialized Doxygen tags for core components.

### Removed
- Cleaned up one-off refactoring scripts and temporary development artifacts.

## [2.0.0] - 2026-02-26

### Added
- **C++23 Migration**: Core engine now leverages C++23 features for better performance and safety.
- **Modern Documentation Suite**: Full set of engineering-grade documentation.
- **AGENTS.md**: Optimized technical reference for AI agents.
- **Enhanced SigV4 Support**: Standardized behavior for recognize-but-not-implemented operations.
- **SIMD JSON**: Switched to `simdjson` for faster request parsing.

### Changed
- Refactored LSM Tree merging logic to be lock-free in common paths.
- Improved error code parity with Amazon DynamoDB.

### Fixed
- Resolved edge case in `UpdateItem` where conditional checks failed incorrectly.
- Fixed memory leak in the Expression Engine during complex AST evaluations.

## [1.5.0] - 2025-11-12

### Added
- Support for DynamoDB Streams.
- Basic PartiQL `ExecuteStatement` support.
- Background TTL expiration process.

## [1.0.0] - 2025-06-01

### Added
- Initial release of cynamoDB.
- Core CRUD, Query, and Scan operations.
- GSI and LSI support.
- LSM Tree persistence layer.
- Basic SigV4 authentication.
