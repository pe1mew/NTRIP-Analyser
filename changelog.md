# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **Windows GUI Application** - Native Win32 GUI with real-time RTCM stream analysis
  - Interactive connection management and configuration
  - Real-time message monitoring and decoding
  - Live message detail windows with double-click support
  - Message statistics analysis (count, min/avg/max intervals)
  - Satellite counting and constellation breakdown
  - Configuration save/load (JSON format)
  - Clipboard support for copying data
  - Multi-format stream detection
  - Automated build scripts (`build-gui.bat`, `build-gui.ps1`)
- **MSM7 Full Decoder** - Complete rewrite with comprehensive structure
  - Reference Station ID and Epoch Time display
  - Multiple Message Flag, IODS, Clock Steering indicators
  - Divergence-free Smoothing and Smoothing Interval
  - Satellite data (rough range, extended info, phase-range rate)
  - Signal data (fine pseudorange, carrier phase, lock time, CNR, Doppler)
  - Support for GPS, GLONASS, Galileo, QZSS, BeiDou, SBAS (types 1077, 1087, 1097, 1117, 1127, 1137)
- **MSM4 Decoder** - Generic decoder for all GNSS constellations (types 1074, 1084, 1094, 1124)
- **Doxygen Documentation** - Comprehensive API documentation in header files
- **Distance and Heading Calculation** - From rover to base station for RTCM 1005/1006 messages
- **CRC Validation** - RTCM message CRC-24Q verification
- **CMake Support** - CMakeLists.txt for cross-platform builds
- **CI/CD Pipeline** - GitHub Actions workflow for automated builds
- **Debug Mode** - Server response display in ASCII and HEX format with `-v` flag

### Changed
- **RTCM 1013 Decoder** - Fixed System Parameters decoding, corrected field extraction
- **Documentation Structure** - Reorganized with simplified README and detailed GUI guide
- **Build System** - Added VS Code tasks for both CLI and GUI builds
- **Timing Measurement** - Fixed for Linux compilation

### Fixed
- RTCM 1013 message structure and field decoding
- Linux compilation timing measurement issues
- RTCM parser warnings (unused variables now displayed)

### Removed
- Leap seconds field from RTCM 1013 decoder (not part of specification)

## [0.1] - 2025-06-18

### Added
- Initial release with CLI application
- NTRIP client functionality (connect, authenticate, receive streams)
- RTCM 3.x message parsing foundation
- Basic message decoders (1005, 1006, 1007, 1008, 1012, 1013, 1019, 1033, 1045, 1230)
- Message statistics collection (count, intervals)
- Mountpoint list retrieval
- NMEA GGA sentence generation for rover positioning
- JSON configuration support
- Linux and Windows compatibility
- Basic documentation
