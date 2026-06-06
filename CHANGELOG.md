# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- Added security policy, SPDX headers, cleaned ignore rules, README badge correction, and license copyright normalisation.

## [2.1.0] - 2026-05-29

### Added

- Added `pool_footprint()`, `pool_footprint_t`, payload sizing helpers, octet conversion helpers, optional RAM budget checks, and footprint self-consistency tests.

### Changed

- Adopted the shared primitive-family source style across public headers and implementation files.

## [2.0.0] - 2026-05-28

### Added

- Added platform abstraction for 8-bit and 16-bit minimum addressable units, configurable atomicity, version-header installation, pkg-config metadata, subproject dependency override, expanded CI, ThreadSanitizer tests, 16-bit MAU simulation, and MISRA deviation documentation.

### Changed

- Changed slot status storage to `POOL_ATOMIC(uint_least8_t)`, changed backing storage to `pool_byte_t[]`, pinned the build to C11, and added `atomicity_mode`.

### Fixed

- Fixed TI C2000 builds where `uint8_t` is unavailable because `CHAR_BIT == 16`.

## [1.1.3] - 2026-03-14

### Added

- Added Meson options for maximum pool slots and item size, plus hash and linear lookup strategy support.

### Fixed

- Improved ISR-safe release ordering, free-slot validation, hash cursor wrap behaviour, and slot-size alignment checks.

## [1.0.3] - 2026-02-28

### Fixed

- Enhanced ISR safety and compliance readiness by reordering release logic and adding validation tests.
