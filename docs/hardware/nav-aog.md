# AOG Output Layer — NAV-AOG-001-FINAL

> **Module:** NAV-AOG-001 · **Layer:** AOG PGN Output · **Version:** 2.0 (FINAL)
> **Datum:** 2026-05-01
> **Abhängig von:** PGN-Verzeichnis, ADR-0001..0005

---

## 1. Overview

The AOG output layer translates raw GNSS position fixes and heading measurements into AgOpenGPS-compatible PGN frames. This is the **sole owner** of PGN encoding logic. Application code never constructs PGN bytes directly.

### FINAL-Härtung (NAV-AOG-001-FINAL)

This document covers the hardened, production-ready state after all 9 Nacharbeit points:

| # | Punkt | Status |
|---|-------|--------|
| P1 | Capability-/Freshness-Gating | ✅ Strict 2-stage gating |
| P2 | Discovery-Kompatibilität | ✅ Tolerant + strict split |
| P3 | Source IDs exakt | ✅ GPS = 0x78 per PGN-Verzeichnis |
| P4 | Heading-Fallback | ✅ LOST/INVALID/STALE unterschieden |
| P5 | PGN 214 Bytepräzision | ✅ Regression-safe |
| P6 | Status-/Fix-Mapping | ✅ 8 output states |
| P7 | Hosttests massiv | ✅ 39 Tests |
| P8 | Dokumentation | ✅ This document |
| P9 | Build-/Testnachweis | ⚠️ Sandbox: ehrlich dokumentiert |

---

## 2. Architecture

```
┌──────────────────┐    ┌──────────────────┐
│  GNSS Snapshot   │    │ Heading Snapshot │
│  (lat, lon, alt, │    │ (dual, true,     │
│   speed, hdop,   │    │  quality, ts)    │
│   sats, fix, ts) │    │                  │
└────────┬─────────┘    └────────┬─────────┘
         │                       │
         └───────────┬───────────┘
                     │
                     ▼
         ┌───────────────────────┐
         │   AOG Nav App         │
         │   (output gating,     │
         │    status mapping,    │
         │    heading fallback)  │
         └───────────┬───────────┘
                     │
         ┌───────────┼───────────┐
         │           │           │
         ▼           ▼           ▼
  ┌────────────┐ ┌────────┐ ┌──────────┐
  │ protocol_  │ │Discovery│ │Transport │
  │ aog (PGN   │ │(202/203│ │  Layer   │
  │  encoder)  │ │ 253/254)│ │ (UDP)    │
  └────────────┘ └────────┘ └──────────┘
```

**Layer separation (ADR-0003):**
- `aog_navigation_app` never calls transport directly
- `protocol_aog` never does I/O
- Transport never contains PGN logic

---

## 3. Supported PGNs

| PGN | Hex | Name | Direction | Trigger |
|-----|-----|------|-----------|---------|
| 214 | 0xD6 | Primary GPS + Heading | Module → AOG | Periodic, 20 Hz |
| 254 | 0xFE | Hello Response | Module → AOG | On PGN 253 request |
| 203 | 0xCB | Scan Reply | Module → AOG | On PGN 202 request |

### PGN 214 — Frame Structure

```
Total: 58 bytes on wire
[0x80][0x81][SRC][PGN_lo][PGN_hi][LEN][PAYLOAD 51 bytes][CRC]

Byte Layout (within payload):
  Offset  Size  Type   Field                Scaling
  ──────────────────────────────────────────────────
  0       8     f64    longitude            decimal degrees (E+)
  8       8     f64    latitude             decimal degrees (N+)
  16      4     f32    heading_dual         degrees (0-360, CW)
  20      4     f32    heading_true         degrees (COG from RMC)
  24      4     f32    speed                km/h
  28      4     f32    roll                 degrees
  32      4     f32    altitude             meters
  36      2     u16    satellites           count
  38      1     u8     fix_quality          0/1/2/4/5
  39      2     u16    hdop                 ×100 (1.5 → 150)
  41      2     u16    age                  ×100 (correction age, seconds)
  43      2     u16    imu_heading          ×10 degrees (sentinel if no IMU)
  45      2     i16    imu_roll             ×10 degrees (sentinel)
  47      2     i16    imu_pitch            degrees (sentinel)
  49      2     i16    imu_yaw_rate         degrees/s (sentinel)
```

### CRC Rules

**TX (outgoing):** ALWAYS strict. CRC = sum(bytes[2..2+4+len-1]) mod 256.

**RX (incoming):**
- Core PGNs (214, 200, 201): **STRICT** — exact CRC required
- Discovery PGNs (202, 253): **TOLERANT** — accepts:
  - Exact CRC match (normal)
  - CRC == 0x00 (known AgIO quirk: uninitialized Discovery frames)
  - CRC off by ±1 (wire noise on UDP)

### Sentinel Values

| Type | Value | Used for |
|------|-------|----------|
| f64 | double.MaxValue (1.79e+308) | longitude, latitude |
| f32 | FLT_MAX | heading, speed, roll, altitude |
| u16 | 0xFFFF | satellites, hdop, age, imu_heading |
| i16 | 0x7FFF | imu_roll, imu_pitch, imu_yaw_rate |

---

## 4. Source Addresses & Module Types

Per PGN-Verzeichnis:

### Source Addresses (in frame header byte [2])

| SRC | Hex | Module |
|-----|-----|--------|
| GPS | 0x05 | GNSS navigation module |
| IMU | 0x06 | Inertial measurement unit |
| Steering | 0x07 | Steering controller |
| AOG | 0x00 | AgOpenGPS itself |

**NAV-AOG-001 uses SRC = 0x05 for all outgoing frames.**

### Module Types (in Scan Reply payload)

| Type | Dec | Hex | Module |
|------|-----|-----|--------|
| GPS | 120 | 0x78 | GNSS navigation module |
| IMU | 121 | 0x79 | Inertial measurement unit |
| Steering | 122 | 0x7A | Steering controller |

**NAV-AOG-001 reports module_type = 0x78 (120) in PGN 203 Scan Reply.**

---

## 5. Output Gating Rules (P1)

### Two-Stage Gating Cascade

#### Stage 1 — GNSS Gate

| GNSS State | Frame Content |
|------------|---------------|
| **Invalid** (valid=false) | Full sentinel PGN 214, fix_quality=0 |
| **Stale** (fresh=false) | Full sentinel PGN 214, fix_quality=0 |
| **Valid + Fresh** | Real GNSS data, proceed to Stage 2 |

#### Stage 2 — Heading Gate (only if GNSS is valid)

| Heading State | Frame Content | Output State |
|---------------|---------------|--------------|
| **Lost** (source NULL) | GNSS real, heading = sentinel | `HEADING_LOST` |
| **Invalid** (valid=false) | GNSS real, heading = sentinel | `HEADING_INVALID` |
| **Stale** (> freshness_ms) | GNSS real, heading = sentinel | `HEADING_STALE` |
| **Valid + Fresh** | Full live frame | `OK` |

### Critical Rules

1. **No stale reuse:** Once data becomes stale, the module MUST NOT retransmit the last known value.
2. **No silent fallback:** Invalid/stale data is replaced by sentinel, not by zero or previous value.
3. **Deterministic:** Same input → same output, no random delays or jitter.

---

## 6. Output State Model (P6)

8-state machine covering all GNSS + Heading combinations:

| # | State | fix_quality | Heading | Description |
|---|-------|-------------|---------|-------------|
| 0 | `INIT` | N/A | N/A | Before first service step |
| 1 | `OK` | from GGA | real | Full live frame |
| 2 | `GNSS_INVALID` | 0 (NONE) | sentinel | No GNSS fix at all |
| 3 | `GNSS_STALE` | 0 (NONE) | sentinel | GNSS was valid, now expired |
| 4 | `HEADING_INVALID` | from GGA | sentinel | GNSS OK, heading reports failure |
| 5 | `HEADING_STALE` | from GGA | sentinel | GNSS OK, heading expired |
| 6 | `HEADING_LOST` | from GGA | sentinel | GNSS OK, heading source gone |
| 7 | `SUPPRESSED` | N/A | N/A | Output entirely suppressed |

### Fix Quality Mapping (NMEA GGA → AOG)

| NMEA GGA | AOG Value | Label |
|----------|-----------|-------|
| 0 | 0 | NONE |
| 1 | 1 | GPS |
| 2 | 2 | DGPS |
| 3 | 0 | NONE (PPS, unknown to AOG) |
| 4 | 4 | RTK_FIX |
| 5 | 5 | RTK_FLOAT |
| other | 0 | NONE |

---

## 7. Heading Fallback Rules (P4)

| Condition | Action | State |
|-----------|--------|-------|
| Source absent (NULL) | heading_dual = FLT_MAX sentinel | `HEADING_LOST` |
| Source present, valid=false | heading_dual = FLT_MAX sentinel | `HEADING_INVALID` |
| Source valid, but stale (> freshness) | heading_dual = FLT_MAX sentinel | `HEADING_STALE` |
| Source valid + fresh | heading_dual = real degrees (0-360) | `OK` |

**Key:** The fallback is always **sentinel** (FLT_MAX). Never zero, never last-known-value, never suppress (except when GNSS is also invalid, in which case the entire frame is sentinel).

---

## 8. Discovery Behavior (P2)

### CRC Mode Split

| Frame Type | CRC Mode | Reason |
|------------|----------|--------|
| PGN 214 (output) | STRICT | Core data integrity |
| PGN 200/201 (legacy) | STRICT | Core data integrity |
| PGN 253 (Hello Request, RX) | **TOLERANT** | Known AgIO inconsistencies |
| PGN 202 (Scan Request, RX) | **TOLERANT** | Known AgIO inconsistencies |
| PGN 254 (Hello Response, TX) | STRICT | Our output is always correct |
| PGN 203 (Scan Reply, TX) | STRICT | Our output is always correct |

### Known AgIO Inconsistencies

1. **CRC == 0x00:** Some AgIO versions send Discovery frames with uninitialized CRC byte.
2. **CRC off by ±1:** UDP wire noise can flip bits in CRC byte.
3. **Inconsistent length:** Some AgIO builds send extra trailing bytes.

### Discovery Response Rules

- Discovery responses **bypass output gating** — always emitted regardless of GNSS/heading state.
- Module reports `SRC = 0x05` and `module_type = 0x78`.
- Lists PGN 214 and PGN 254 as supported PGNs.

---

## 9. Freshness Thresholds

| Data Source | Default Threshold | Configurable |
|-------------|-------------------|-------------|
| GNSS position | 2000 ms | Via `gnss_snapshot.freshness_ms` |
| Heading | 3000 ms | Via `aog_nav_app_set_heading_freshness()` (500–10000 ms) |

---

## 10. Test Coverage (P7)

39 host tests covering all mandatory cases:

### Frame Format & Encoding (8 tests)
1. `test_pgn214_frame_format` — v5 header verification
2. `test_pgn214_frame_length_exact` — 58 bytes total
3. `test_pgn214_crc_sum` — CRC = sum mod 256
4. `test_pgn214_little_endian` — LE encoding
5. `test_pgn214_byte_offsets` — all 15 field offsets
6. `test_pgn214_sentinel_values` — all sentinel types
7. `test_fix_quality_mapping` — 7 NMEA→AOG cases
8. `test_pgn214_exact_bytes_every_offset` — regression: every field

### Output Gating (10 tests)
9. `test_valid_gnss_valid_heading` — full live frame
10. `test_valid_gnss_invalid_heading` — sentinel heading
11. `test_valid_gnss_stale_heading` — stale heading
12. `test_valid_gnss_heading_lost` — heading source gone
13. `test_invalid_gnss` — full sentinel frame
14. `test_stale_gnss` — stale → sentinel
15. `test_no_stale_gnss_reuse` — no position reuse
16. `test_no_stale_heading_reuse` — no heading reuse
17. `test_heading_fallback_sentinel` — sentinel defined
18. `test_heading_suppress_when_gnss_invalid` — full suppress

### Status / Fix Mapping (3 tests)
19. `test_output_state_8_states` — all 8 states
20. `test_fix_quality_all_nmea_values` — 6 fix types
21. `test_output_state_transitions` — state machine transitions

### Discovery (10 tests)
22. `test_hello_request_reply` — PGN 253→254
23. `test_scan_request_reply` — PGN 202→203
24. `test_discovery_tolerant_crc_zero` — CRC=0x00 accepted
25. `test_discovery_tolerant_crc_plus_minus_one` — ±1 accepted
26. `test_discovery_strict_crc_reject` — off by 10 rejected
27. `test_discovery_bypasses_gating` — works without GNSS
28. `test_multiple_hello_replies` — queuing
29. `test_scan_reply_module_type` — 0x78 verified
30. `test_pgn_is_discovery` — classification
31. `test_verify_crc_tolerant_function` — verify function

### Source IDs (2 tests)
32. `test_source_byte_gps` — SRC=0x05
33. `test_module_type_initialized` — 0x78

### Timing, Conversion, Architecture (6 tests)
34. `test_cyclic_20hz` — deterministic timing
35. `test_heading_normalization` — rad→deg 0-360
36. `test_speed_conversion` — m/s→km/h
37. `test_hdop_scaling` — ×100
38. `test_transport_isolation` — no UDP in app
39. `test_crc_exact_on_tx_frames` — all TX CRCs strict

---

## 11. IMU Fields

Currently **no IMU integration**. All IMU fields are sentinel:

| Offset | Field | Sentinel |
|--------|-------|----------|
| 43 | imu_heading | 0xFFFF |
| 45 | imu_roll | 0x7FFF |
| 47 | imu_pitch | 0x7FFF |
| 49 | imu_yaw_rate | 0x7FFF |

---

## 12. Known Limitations

| # | Limitation | Impact | Mitigation |
|---|-----------|--------|------------|
| 1 | No IMU integration | IMU fields always sentinel | Future hardware revision |
| 2 | No auto-configuration | AgIO IP/port manual | Build-time or NVS |
| 3 | No NTRIP client | External RTK needed | External caster/radio |
| 4 | Single heading source | No blending | Sentinel when lost |
| 5 | Fixed freshness thresholds | No runtime adjust | Recompile or NVS |
| 6 | Discovery CRC tolerant | Accepts some bad frames | Only for Discovery PGNs |

---

## 13. Dependencies

| Component | Role |
|-----------|------|
| `gnss_snapshot` | Validated GNSS position, speed, fix quality |
| `gnss_dual_heading` | Dual-antenna heading measurement |
| `protocol_aog` | PGN encoding, CRC, frame formatting |
| `byte_ring_buffer` | TX/RX transport abstraction |

---

## 14. References

- [PGN 214 Specification](../protocol/pgn-214.md)
- PGN-Verzeichnis.md (module types, source IDs)
- ADR-0001..0005
- AgOpenGPS: [github.com/AgOpenGPS/AgOpenGPS](https://github.com/AgOpenGPS/AgOpenGPS)
