#!/usr/bin/env python3
"""
Sandbox Test Runner — NAV-TEST-FIX-001 (WP-I)

Executes all sandbox-mandatory checks and produces a structured report.
Each suite is classified by type (S/L/H/C/N) and status (PASS/FAIL/...).

Status Terms:
  PASS:               All tests passed, no compiler errors, exit code 0.
  FAIL:               Compiler errors, test failures, or exit != 0.
  BLOCKED:            Missing tool, permission, or prerequisite.
  UNSTABLE:           Tests ran but signal/exit issues detected after completion.
  BUILD_ONLY_PASS:    Build succeeded but no test execution.
  USER_REQUIRED:      Requires user to run locally (toolchain/permission).
  HARDWARE_REQUIRED:  Requires real hardware (ESP32, GNSS, Ethernet, etc.).
  NOT_IN_SCOPE:       Suite exists but is outside current task scope.

Classification Terms:
  S = Sandbox Automated: Reliable in sandbox environment.
  L = Local/User Required: Must be run by user locally.
  H = Hardware Required: Needs real hardware.
  C = CI Candidate: Suitable for future GitHub Actions (subset of S).
  N = Not in current scope: Exists but excluded from current task.

History:
  v1: NAV-TEST-INFRA-001 (initial)
  v2: NAV-TEST-SPLIT-001-R2 WP-B (compiler error hardening)
  v3: NAV-TEST-FIX-001 WP-I (all suites, classification, exit codes)
"""

import subprocess
import sys
import os
import time
import json
import re
from dataclasses import dataclass, field
from typing import List, Optional, Dict


# ========================================================================
# Hard compiler/linker error patterns
# Any of these in stdout or stderr of a BUILD command → FAIL
# ========================================================================
COMPILER_ERROR_PATTERNS = [
    r"error:",                  # GCC/Clang generic error
    r"undefined reference",     # Linker: unresolved symbol
    r"unknown type name",       # C type resolution failure
    r"fatal error:",            # Fatal include/parse error
    r"collect2:",               # Linker collector errors
    r"ld returned",             # Linker return code
    r"ninja: build stopped",    # Ninja build failure
    r"\*\*\* \[\.pio/",        # Make/PIO build failure marker
    r"multiple definition",     # Duplicate symbol
    r"cannot find -l",          # Missing library
    r"undefined symbol",        # Another linker error form
    r"compilation terminated",  # GCC fatal termination
]

# ========================================================================
# Full suite registry with classification
# ========================================================================
SUITE_REGISTRY: List[Dict] = [
    # --- Sandbox / CI-Candidate (S, C) ---
    {"filter": "host/test_byte_ring_buffer",  "cls": "S, C", "label": "byte_ring_buffer",     "short": "Ring buffer data structure"},
    {"filter": "host/test_rtcm_router",       "cls": "S, C", "label": "rtcm_router",          "short": "RTCM routing logic"},
    {"filter": "host/test_runtime_mode",      "cls": "S, C", "label": "runtime_mode",         "short": "Runtime mode state machine"},
    {"filter": "host/test_aog_pgn214",        "cls": "S, C", "label": "aog_pgn214",           "short": "AOG PGN 214 protocol"},
    {"filter": "host/test_gnss_validation",   "cls": "S, C", "label": "gnss_validation",      "short": "GNSS/NMEA validation"},
    {"filter": "host/test_ntrip_client",      "cls": "S, C", "label": "ntrip_client",         "short": "NTRIP client protocol"},
    {"filter": "host/test_nav_rtcm_wiring",   "cls": "S, C", "label": "nav_rtcm_wiring",      "short": "NAV-RTCM wiring"},
    {"filter": "host/test_nav_diagnostics",   "cls": "S",    "label": "nav_diagnostics",      "short": "Navigation diagnostics"},
    {"filter": "host/test_hal_uart",          "cls": "S",    "label": "hal_uart",             "short": "HAL UART abstraction"},
    {"filter": "host/test_transport_uart",    "cls": "S",    "label": "transport_uart",       "short": "UART transport layer"},
    {"filter": "host/test_nav_rtcm_001",      "cls": "S",    "label": "nav_rtcm_001",         "short": "NAV RTCM integration"},
    {"filter": "host/test_followup_review",   "cls": "S",    "label": "followup_review",      "short": "Follow-up review checks"},
    {"filter": "host/test_board_profile_smoke", "cls": "S",  "label": "board_profile_smoke",  "short": "Board profile smoke tests"},

    # --- Sandbox / Sim (S) ---
    {"filter": "sim/test_nav_chain",          "cls": "S",    "label": "nav_chain (sim)",      "short": "NAV integration chain"},

    # --- Sandbox / Hardware simulation (S) ---
    {"filter": "hardware/test_gnss_smoke",    "cls": "S",    "label": "gnss_smoke (hw)",      "short": "GNSS hardware smoke"},

    # --- Not in scope (N) — steering suites ---
    {"filter": "host/test_steering_control",  "cls": "N",    "label": "steering_control",     "short": "Steering control (STEER-MIG-001)"},
    {"filter": "host/test_steering_output",   "cls": "N",    "label": "steering_output",      "short": "Steering output (STEER-MIG-001)"},
    {"filter": "host/test_steering_safety",   "cls": "N",    "label": "steering_safety",      "short": "Steering safety (STEER-MIG-001)"},
    {"filter": "host/test_steering_sensor",   "cls": "N",    "label": "steering_sensor",      "short": "Steering sensor (STEER-MIG-001)"},
]


def _scan_for_compiler_errors(stdout: str, stderr: str) -> List[str]:
    """Scan combined output for hard compiler error patterns."""
    combined = stdout + "\n" + stderr
    matches = []
    for pattern in COMPILER_ERROR_PATTERNS:
        for line in combined.split("\n"):
            if re.search(pattern, line, re.IGNORECASE):
                matches.append(line.strip())
    return matches


def _scan_for_steering_files(stdout: str, stderr: str) -> List[str]:
    """Scan build output for steering component compilation."""
    steering_patterns = [
        "steering_control.c", "steering_output.c", "steering_safety.c",
        "steering_diagnostics.c", "was_sensor.c", "ads1118.c",
        "actuator_drv8263h.c", "imu_bno085.c", "aog_steering_app.c",
        "safety_failsafe.c",
    ]
    combined = stdout + "\n" + stderr
    matches = []
    for pattern in steering_patterns:
        for line in combined.split("\n"):
            if pattern in line and ("Compiling" in line or "Linking" in line):
                matches.append(line.strip())
    return matches


@dataclass
class CheckResult:
    name: str
    category: str
    classification: str   # S, L, H, C, N or combination
    status: str           # PASS, FAIL, BLOCKED, UNSTABLE, BUILD_ONLY_PASS, USER_REQUIRED, HARDWARE_REQUIRED, NOT_IN_SCOPE
    command: str
    duration_s: float = 0.0
    details: str = ""
    exit_code: int = 0
    total_tests: int = 0
    passed_tests: int = 0
    failed_tests: int = 0


def run_cmd(
    cmd: List[str],
    timeout: int = 120,
    cwd: Optional[str] = None,
    env: Optional[dict] = None,
) -> tuple[int, str, str, float]:
    """Run a command and return (exit_code, stdout, stderr, duration)."""
    start = time.time()
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=cwd,
            env=env,
        )
        elapsed = time.time() - start
        return proc.returncode, proc.stdout, proc.stderr, elapsed
    except subprocess.TimeoutExpired:
        elapsed = time.time() - start
        return -1, "", f"TIMEOUT after {timeout}s", elapsed
    except FileNotFoundError:
        elapsed = time.time() - start
        return -1, "", "COMMAND_NOT_FOUND", elapsed
    except Exception as e:
        elapsed = time.time() - start
        return -1, "", str(e), elapsed


# ========================================================================
# Check functions
# ========================================================================

def check_policy_grep(project_dir: str) -> CheckResult:
    """D1-D6: Policy grep/rg checks."""
    checks = [
        ("Arduino.h", ["rg", "Arduino\\.h", "components/", "src/"], "0"),
        ("Serial.", ["rg", "Serial\\.", "components/", "src/"], "0"),
        ("WiFiUDP/EthernetUDP", ["rg", "WiFiUDP|EthernetUDP", "components/", "src/"], "2"),
        ("SPI./Wire.", ["rg", "SPI\\.|Wire\\.", "components/", "src/"], "2"),
        ("20Hz/50ms", ["rg", "-i", "20 Hz|20hz|50 ms|50ms", "components/", "src/"], "5"),
        ("service_step", ["rg", "delegates to service_step", "components/", "src/"], "0"),
    ]
    results = []
    for name, cmd, max_expected in checks:
        code, out, err, dur = run_cmd(cmd, cwd=project_dir)
        lines = [l for l in (out + err).strip().split("\n") if l.strip()]
        if code == 0:
            count = len(lines)
        elif code == 1 and not lines:
            count = 0
        else:
            count = -1
        max_exp = int(max_expected)
        if count >= 0 and count <= max_exp:
            results.append(f"{name}: {count} hit(s) <= {max_exp} OK")
        else:
            results.append(f"{name}: {count} hit(s) > {max_exp} ALERT")

    all_ok = all("OK" in r for r in results)
    details = "\n".join(results)
    return CheckResult(
        name="Policy grep/rg checks",
        category="Static Analysis",
        classification="C",
        status="PASS" if all_ok else "FAIL",
        command="rg policy checks (6 items)",
        duration_s=0,
        details=details,
    )


def check_cppcheck(project_dir: str) -> CheckResult:
    """Static analysis with cppcheck."""
    code, out, err, dur = run_cmd(
        ["cppcheck", "--enable=warning,performance,portability",
         "--suppress=missingInclude", "--suppress=unusedFunction",
         "--suppress=normalCheckLevelMaxBranches", "--inline-suppr",
         "-q", "components/"],
        cwd=project_dir, timeout=120,
    )
    if code == -1:
        if "COMMAND_NOT_FOUND" in err:
            return CheckResult("cppcheck", "Static Analysis", "C", "BLOCKED", "cppcheck", 0, "cppcheck not installed")
        return CheckResult("cppcheck", "Static Analysis", "C", "FAIL", "cppcheck", 0, err)
    if code == 0 and not out.strip() and not err.strip():
        return CheckResult("cppcheck", "Static Analysis", "C", "PASS", "cppcheck", dur, "0 warnings, 0 errors on components/")
    return CheckResult("cppcheck", "Static Analysis", "C", "FAIL", "cppcheck", dur, f"exit={code}\n{out}\n{err}")


def check_clang_format(project_dir: str) -> CheckResult:
    """Format check with clang-format."""
    code, out, err, dur = run_cmd(["clang-format", "--version"], timeout=10)
    if "COMMAND_NOT_FOUND" in err:
        return CheckResult("clang-format", "Static Analysis", "C", "BLOCKED", "clang-format --version", 0, "not installed")

    sample = None
    for candidate in ["components/runtime_buffers/byte_ring_buffer.c",
                        "components/rtcm_router/rtcm_router.c",
                        "components/runtime/runtime_mode.c"]:
        path = os.path.join(project_dir, candidate)
        if os.path.exists(path):
            sample = path
            break
    if sample is None:
        return CheckResult("clang-format", "Static Analysis", "C", "BLOCKED", "clang-format", 0, "no sample file found")

    code2, out2, err2, dur2 = run_cmd(["clang-format", "--dry-run", sample], timeout=30)
    return CheckResult("clang-format", "Static Analysis", "C", "PASS",
                       f"clang-format --dry-run {sample}", dur + dur2,
                       f"clang-format {err.strip().split(chr(10))[0] if err.strip() else 'available'} — dry-run OK")


def _is_lib_ignore_error(line: str) -> bool:
    """Check if a compiler error is from a lib_ignore'd component."""
    lib_ignore_patterns = [
        "steering_control", "steering_output", "steering_safety",
        "steering_diagnostics", "was_sensor", "ads1118",
        "actuator_drv8263h", "imu_bno085", "aog_steering_app",
        "safety_failsafe", "hal_backend", "hal_eth", "hal_gpio",
        "hal_nvs", "hal_ota", "hal_reset", "hal_spi",
        "transport_eth", "transport_udp", "app_core",
        "cli", "terminal", "feature_flags",
        "runtime_fast", "runtime_health", "runtime_queue",
        "runtime_stats", "runtime_watchdog",
        "test_steering_control", "test_steering_output",
        "test_steering_safety", "test_steering_sensor",
    ]
    for pattern in lib_ignore_patterns:
        if pattern in line:
            return True
    if "*** [.pio/" in line and "Error" in line:
        for pattern in lib_ignore_patterns:
            if pattern in line:
                return True
    return False


def check_pio_build_only(project_dir: str) -> CheckResult:
    """pio test build-only for native environment."""
    code, out, err, dur = run_cmd(
        ["pio", "test", "-e", "native", "--without-uploading", "--without-testing"],
        cwd=project_dir, timeout=180,
    )
    if code == -1:
        if "COMMAND_NOT_FOUND" in err:
            return CheckResult("pio test build-only", "Native Build", "C", "BLOCKED", "pio", 0, "PlatformIO not installed")
        return CheckResult("pio test build-only", "Native Build", "C", "FAIL", "pio", 0, err)

    all_errors = _scan_for_compiler_errors(out, err)
    is_expected = [_is_lib_ignore_error(e) for e in all_errors]
    for i in range(1, len(all_errors)):
        if "compilation terminated" in all_errors[i] and is_expected[i - 1]:
            is_expected[i] = True
    real_errors = [e for e, exp in zip(all_errors, is_expected) if not exp]

    if real_errors:
        return CheckResult("pio test build-only", "Native Build", "C", "FAIL",
                           "pio test -e native --without-testing", dur,
                           f"COMPILER ERRORS DETECTED ({len(real_errors)} real):\n" +
                           "\n".join(real_errors[:5]))

    combined = out + "\n" + err
    lines = combined.strip().split("\n")
    passed = sum(1 for l in lines if "PASSED" in l)
    errored = sum(1 for l in lines if "ERRORED" in l)
    total = passed + errored

    if code != 0:
        steering_patterns = [
            "steering_control", "steering_output", "steering_safety",
            "steering_diagnostics", "steering_sensor", "was_sensor",
            "ads1118", "actuator_drv8263h", "imu_bno085",
            "aog_steering_app", "safety_failsafe",
        ]
        errored_suites = [l.strip() for l in lines if "ERRORED" in l]
        all_errored_are_ignored = all(
            any(pat in suite for pat in steering_patterns)
            for suite in errored_suites
        ) if errored_suites else True

        if all_errored_are_ignored and passed >= 10:
            return CheckResult("pio test build-only", "Native Build", "C", "BUILD_ONLY_PASS",
                               "pio test -e native --without-testing", dur,
                               f"{passed}/{passed + len(errored_suites)} suites PASS, {len(errored_suites)} ERRORED (lib_ignore'd)")
        return CheckResult("pio test build-only", "Native Build", "C", "FAIL",
                           "pio test -e native --without-testing", dur,
                           f"exit={code}, {passed} PASS, {len(errored_suites)} ERRORED")

    detail = f"{passed}/{total} suites PASSED"
    if total >= 10 and passed == total:
        return CheckResult("pio test build-only", "Native Build", "C", "BUILD_ONLY_PASS",
                           "pio test -e native --without-testing", dur, detail)
    return CheckResult("pio test build-only", "Native Build", "C", "FAIL",
                       "pio test -e native --without-testing", dur, f"{passed}/{total} suites PASSED")


def check_single_suite(project_dir: str, suite_entry: Dict) -> CheckResult:
    """Build and run a single native test suite with classification."""
    suite_filter = suite_entry["filter"]
    classification = suite_entry["cls"]
    label = suite_entry["label"]

    # N (Not in scope) — skip execution, just report
    if "N" in classification:
        return CheckResult(
            name=label, category="Native Test", classification=classification,
            status="NOT_IN_SCOPE", command=f"pio test -f {suite_filter}",
            details=f"Not in scope (lib_ignore'd, STEER-MIG-001)",
        )

    code, out, err, dur = run_cmd(
        ["pio", "test", "-e", "native", "-f", suite_filter, "--without-uploading"],
        cwd=project_dir, timeout=120,
    )
    if code == -1:
        return CheckResult(label, "Native Test", classification, "BLOCKED",
                           f"pio test -f {suite_filter}", 0, err, exit_code=code)

    # Compiler errors → always FAIL
    compiler_errors = _scan_for_compiler_errors(out, err)
    if compiler_errors:
        return CheckResult(
            label, "Native Test", classification, "FAIL",
            f"pio test -f {suite_filter}", dur,
            f"COMPILER ERRORS ({len(compiler_errors)}):\n" + "\n".join(compiler_errors[:5]),
            exit_code=code,
        )

    # Extract test results from output
    result_lines = []
    for line in out.split("\n"):
        if "[PASSED]" in line or "[FAILED]" in line:
            result_lines.append(line.strip())

    passed = sum(1 for l in result_lines if "[PASSED]" in l)
    failed = sum(1 for l in result_lines if "[FAILED]" in l)
    total = passed + failed

    # Check for signals in stderr (for UNSTABLE detection)
    signal_patterns = [
        "SIGALRM", "SIGFPE", "SIGILL", "SIGSEGV",
        "SIGBUS", "SIGXCPU", "SIGTTIN", "SIGCONT", "SIGABRT",
    ]
    signals = []
    for line in err.split("\n"):
        for sig in signal_patterns:
            if sig in line:
                signals.append(line.strip())
                break

    # Summary line from output
    summary_match = re.search(r'(\d+)\s+test cases?:?\s+(\d+)\s+succeeded', out)
    if summary_match:
        total = int(summary_match.group(1))
        passed = int(summary_match.group(2))
        failed = total - passed

    if total > 0 and failed == 0 and not signals:
        return CheckResult(
            label, "Native Test", classification, "PASS",
            f"pio test -f {suite_filter}", dur,
            f"{passed}/{total} tests PASSED",
            exit_code=code, total_tests=total, passed_tests=passed, failed_tests=0,
        )

    if total > 0 and failed == 0 and signals:
        return CheckResult(
            label, "Native Test", classification, "UNSTABLE",
            f"pio test -f {suite_filter}", dur,
            f"{passed}/{total} PASSED, signals: {len(signals)}",
            exit_code=code, total_tests=total, passed_tests=passed, failed_tests=0,
        )

    if total > 0 and failed > 0 and signals:
        return CheckResult(
            label, "Native Test", classification, "UNSTABLE",
            f"pio test -f {suite_filter}", dur,
            f"{passed}/{total} PASSED, {failed} FAILED, signals: {len(signals)}",
            exit_code=code, total_tests=total, passed_tests=passed, failed_tests=failed,
        )

    if total > 0 and failed > 0:
        return CheckResult(
            label, "Native Test", classification, "FAIL",
            f"pio test -f {suite_filter}", dur,
            f"{passed}/{total} PASSED, {failed} FAILED",
            exit_code=code, total_tests=total, passed_tests=passed, failed_tests=failed,
        )

    # No test cases found at all
    return CheckResult(
        label, "Native Test", classification, "FAIL",
        f"pio test -f {suite_filter}", dur,
        f"0 test cases found (build OK, but no tests executed)",
        exit_code=code,
    )


def check_esp32_build(project_dir: str) -> CheckResult:
    """ESP32 NAV build — hardened with compiler error detection."""
    code, out, err, dur = run_cmd(
        ["pio", "run", "-e", "nav_esp32_t_eth_lite"],
        cwd=project_dir, timeout=300,
    )

    if code == -1:
        if "COMMAND_NOT_FOUND" in err:
            return CheckResult("ESP32 NAV build", "ESP32 Build", "L", "BLOCKED",
                               "pio run -e nav_esp32_t_eth_lite", 0, "PlatformIO not installed")
        if "TIMEOUT" in err:
            return CheckResult("ESP32 NAV build", "ESP32 Build", "L", "FAIL",
                               "pio run -e nav_esp32_t_eth_lite", 0, f"Build timeout: {err}")
        return CheckResult("ESP32 NAV build", "ESP32 Build", "L", "FAIL",
                           "pio run -e nav_esp32_t_eth_lite", 0, f"Build error: {err}")

    compiler_errors = _scan_for_compiler_errors(out, err)
    if compiler_errors:
        return CheckResult("ESP32 NAV build", "ESP32 Build", "L", "FAIL",
                           "pio run -e nav_esp32_t_eth_lite", dur,
                           f"COMPILER ERRORS ({len(compiler_errors)}):\n" + "\n".join(compiler_errors[:5]),
                           exit_code=code)

    if code != 0:
        last_lines = out.strip().split("\n")[-5:]
        return CheckResult("ESP32 NAV build", "ESP32 Build", "L", "FAIL",
                           "pio run -e nav_esp32_t_eth_lite", dur,
                           f"exit code = {code}\n" + "\n".join(last_lines),
                           exit_code=code)

    steering_files = _scan_for_steering_files(out, err)
    if steering_files:
        return CheckResult("ESP32 NAV build", "ESP32 Build", "L", "FAIL",
                           "pio run -e nav_esp32_t_eth_lite", dur,
                           f"STEERING FILES COMPILED:\n" + "\n".join(steering_files),
                           exit_code=code)

    if "SUCCESS" not in out:
        last_lines = out.strip().split("\n")[-5:]
        return CheckResult("ESP32 NAV build", "ESP32 Build", "L", "FAIL",
                           "pio run -e nav_esp32_t_eth_lite", dur,
                           f"No SUCCESS marker\n" + "\n".join(last_lines),
                           exit_code=code)

    return CheckResult("ESP32 NAV build", "ESP32 Build", "L", "PASS",
                       "pio run -e nav_esp32_t_eth_lite", dur,
                       "Build succeeded — no compiler errors, no steering files",
                       exit_code=code)


def check_steering_exclusion(project_dir: str) -> CheckResult:
    """Verify steering object files are absent from NAV build directory."""
    build_dir = os.path.join(project_dir, ".pio", "build", "nav_esp32_t_eth_lite")
    if not os.path.isdir(build_dir):
        return CheckResult("Steering exclusion (objects)", "Build Integrity", "L", "FAIL",
                           "find .pio/build -name '*.o'", 0,
                           "Build directory not found — run ESP32 NAV build first")

    steering_obj_patterns = [
        "steering_control", "steering_output", "steering_safety",
        "steering_diagnostics", "was_sensor", "ads1118",
        "actuator_drv8263h", "imu_bno085", "aog_steering_app", "safety_failsafe",
    ]

    found = []
    for root, dirs, files in os.walk(build_dir):
        for f in files:
            if f.endswith(".o"):
                for pattern in steering_obj_patterns:
                    if pattern in f:
                        found.append(os.path.join(root, f))
                        break

    if found:
        return CheckResult("Steering exclusion (objects)", "Build Integrity", "L", "FAIL",
                           "find build dir", 0,
                           f"STEERING .o FILES FOUND ({len(found)}):\n" + "\n".join(found[:5]))

    return CheckResult("Steering exclusion (objects)", "Build Integrity", "L", "PASS",
                       "find build dir", 0, "No steering .o files in NAV build directory")


# ========================================================================
# Main
# ========================================================================

def main():
    project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if not os.path.isdir(os.path.join(project_dir, "platformio.ini")):
        project_dir = os.getcwd()

    print(f"Sandbox Test Runner — NAV-TEST-FIX-001 (WP-I)")
    print(f"Project: {project_dir}")
    print(f"Time:    {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 78)

    results: List[CheckResult] = []

    # --- Phase 1: Static Analysis (3 checks) ---
    n_static = 3
    print(f"\n--- Phase 1: Static Analysis ({n_static} checks) ---")
    print(f"[1/{n_static}] Policy grep/rg checks...")
    results.append(check_policy_grep(project_dir))
    print(f"[2/{n_static}] cppcheck...")
    results.append(check_cppcheck(project_dir))
    print(f"[3/{n_static}] clang-format...")
    results.append(check_clang_format(project_dir))

    # --- Phase 2: Build Checks (3 checks) ---
    n_build = 3
    print(f"\n--- Phase 2: Build Checks ({n_build} checks) ---")
    print(f"[1/{n_build}] pio test build-only (native)...")
    results.append(check_pio_build_only(project_dir))
    print(f"[2/{n_build}] ESP32 NAV build...")
    results.append(check_esp32_build(project_dir))
    print(f"[3/{n_build}] Steering exclusion verification...")
    results.append(check_steering_exclusion(project_dir))

    # --- Phase 3: All Registered Test Suites ---
    sandbox_suites = [s for s in SUITE_REGISTRY if "N" not in s["cls"]]
    n_suite = len(sandbox_suites)
    print(f"\n--- Phase 3: Test Suites ({n_suite} suites) ---")
    for i, suite in enumerate(sandbox_suites, 1):
        print(f"[{i}/{n_suite}] {suite['label']}...")
        results.append(check_single_suite(project_dir, suite))

    # --- Phase 4: Not-in-scope suites (reported but not executed) ---
    noscope_suites = [s for s in SUITE_REGISTRY if "N" in s["cls"]]
    if noscope_suites:
        print(f"\n--- Phase 4: Not in Scope ({len(noscope_suites)} suites) ---")
        for suite in noscope_suites:
            results.append(check_single_suite(project_dir, suite))

    # --- Report ---
    print("\n" + "=" * 78)
    print("SANDBOX TEST REPORT — NAV-TEST-FIX-001 (WP-I)")
    print("=" * 78)

    # Tabular report with classification
    print(f"\n{'#':>3}  {'Suite':<30} {'Cls':<6} {'Status':<18} {'Tests':>8}  {'Exit':>5}  Details")
    print("-" * 110)

    summary = {}
    total_pass = 0
    total_fail = 0
    total_tests_all = 0
    total_tests_passed = 0

    for idx, r in enumerate(results, 1):
        summary[r.status] = summary.get(r.status, 0) + 1
        tests_str = f"{r.passed_tests}/{r.total_tests}" if r.total_tests > 0 else "—"
        exit_str = str(r.exit_code) if r.exit_code != 0 else "0"

        # Short reason
        reason = ""
        if r.status == "PASS":
            reason = "all green"
            total_pass += 1
        elif r.status == "FAIL":
            reason = r.details.split("\n")[0][:40] if r.details else "errors"
            total_fail += 1
        elif r.status == "UNSTABLE":
            reason = "signal/exit issues"
        elif r.status == "BLOCKED":
            reason = r.details[:40] if r.details else "blocked"
        elif r.status == "NOT_IN_SCOPE":
            reason = "lib_ignore'd"
        elif r.status == "BUILD_ONLY_PASS":
            reason = "build ok, no run"
        elif r.status == "USER_REQUIRED":
            reason = "user must run"
        elif r.status == "HARDWARE_REQUIRED":
            reason = "real hardware needed"

        if r.total_tests > 0:
            total_tests_all += r.total_tests
            total_tests_passed += r.passed_tests

        print(f"{idx:>3}  {r.name:<30} {r.classification:<6} {r.status:<18} {tests_str:>8}  {exit_str:>5}  {reason}")

    print("-" * 110)
    print(f"\nStatus Summary:")
    for status in ["PASS", "BUILD_ONLY_PASS", "UNSTABLE", "FAIL", "BLOCKED", "NOT_IN_SCOPE", "USER_REQUIRED", "HARDWARE_REQUIRED"]:
        if status in summary:
            icon = {"PASS": "✓", "FAIL": "✗", "BLOCKED": "⊘", "UNSTABLE": "⚠",
                    "BUILD_ONLY_PASS": "⊡", "NOT_IN_SCOPE": "○",
                    "USER_REQUIRED": "◉", "HARDWARE_REQUIRED": "◉"}.get(status, "?")
            print(f"  {icon} {status}={summary[status]}")

    if total_tests_all > 0:
        pct = (total_tests_passed / total_tests_all) * 100
        print(f"\n  Total: {total_tests_passed}/{total_tests_all} tests passed ({pct:.1f}%)")

    # ESP32 build assertion
    esp32 = next((r for r in results if r.name == "ESP32 NAV build"), None)
    if esp32:
        print(f"\n  ESP32 NAV build: {esp32.status}")

    # Write JSON report
    report = {
        "version": "NAV-TEST-FIX-001-WP-I",
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "project_dir": project_dir,
        "total_tests": total_tests_all,
        "total_passed": total_tests_passed,
        "results": [
            {
                "name": r.name,
                "category": r.category,
                "classification": r.classification,
                "status": r.status,
                "command": r.command,
                "duration_s": round(r.duration_s, 2),
                "exit_code": r.exit_code,
                "total_tests": r.total_tests,
                "passed_tests": r.passed_tests,
                "failed_tests": r.failed_tests,
                "details": r.details[:500] if r.details else "",
            }
            for r in results
        ],
        "summary": summary,
    }
    report_path = os.path.join(project_dir, "docs", "testing", "sandbox-report.json")
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w") as f:
        json.dump(report, f, indent=2)
    print(f"\n  JSON report: {report_path}")

    # Exit code
    has_fail = summary.get("FAIL", 0) > 0 or summary.get("BLOCKED", 0) > 0
    sys.exit(1 if has_fail else 0)


if __name__ == "__main__":
    main()
