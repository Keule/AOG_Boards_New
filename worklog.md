---
Task ID: A1-A5
Agent: main
Task: NAV-UART-STABILIZING-R2 — Build break fix, CMake audit, GNSS stabilization

Work Log:
- Diagnosed ESP-IDF 6.0 component dependency auto-scanner as root cause of imu_bno085 build failure
- Split app_core.c into three files: app_core.c (common), app_core_nav.c (NAV), app_core_steer.c (STEER)
- Moved role-specific files outside components/ directory to prevent auto-scanning
- Fixed gnss_nmea_config.h forward declarations (anonymous-struct typedefs cannot be forward-declared)
- Fixed gnss_nmea_config.c function signatures and type references
- Fixed missing comma in um980_cmd_status_t enum
- Fixed transport_tcp_config_t struct usage in app_core_nav.c
- Fixed gnss_dual_heading_set_sources type mismatch (was passing snapshot, not receiver ptr)
- Fixed native_test.py env undefined error
- Audited all 50 component CMakeLists.txt files
- Verified NAV build: SUCCESS (RAM 29.3%, Flash 9.5%)
- Verified STEER build: SUCCESS (RAM 22.2%, Flash 26.9%)
- Ran native tests: 267/297 PASSED (failures pre-existing)
- Created PATCH_NOTES.md with complete documentation

Stage Summary:
- NAV build is green — imu_bno085 dependency error eliminated
- STEER build is green — no regressions
- 13 steering components correctly gated for NAV builds
- No dummy components created
- git rev-parse handled by ESP-IDF (non-fatal warning only)
- All GNSS/NMEA improvements from R1 preserved and verified
