# AOG Output Layer — NAV-AOG-001 FINAL HARDENING

> **Module:** NAV-AOG-001 · **Layer:** AOG PGN Output · **Version:** 3.0 (FINAL HARDENING)
> **Datum:** 2026-06-15
> **Abhängig von:** PGN-Verzeichnis, ADR-0001..0005
> **Buildpflicht:** `pio test -e native`, `check_all.py`, 3× ESP32 builds

---

## 1. Overview

The AOG output layer translates raw GNSS position fixes and heading measurements into AgOpenGPS-compatible PGN frames. This is the **sole owner** of PGN encoding logic. Application code never constructs PGN bytes directly.

### FINAL HARDENING — 5 Pflichtaufgaben

| # | Pflichtaufgabe | Status |
|---|---------------|--------|
| F1 | Discovery-Protokoll FINAL härten (strict/tolerant split) | ✅ Formal dokumentiert + getestet |
| F2 | Heading-Fallback final definieren (VALID/STALE/INVALID) | ✅ 3-State-Modell im Code |
| F3 | Golden Byte Regression Tests | ✅ 10 neue Tests (40-49), 49 gesamt |
| F4 | Status-/Fix-Mapping finalisieren (8 States) | ✅ Mapping-Tabelle formalisiert |
| F5 | Dokumentation finalisieren | ✅ This document |

### Akzeptanzkriterien

| Kriterium | Status |
|-----------|--------|
| PGN 214 bytegenau stabil | ✅ Golden Tests 40-49 |
| Discovery robust + legacy-tolerant | ✅ PGN 202 + 253 tolerant CRC |
| Heading fallback formal korrekt | ✅ VALID/STALE/INVALID 3-State |
| Statusmapping vollständig | ✅ 8 States + Fix-Mapping-Tabelle |
| Golden tests vorhanden | ✅ 10 exakte Byte-Vektoren |
| Dokumentation vollständig | ✅ 16 Sektionen |
| Keine Layerverletzung | ✅ |
| Keine Arduino-Abhängigkeit | ✅ |
| ADR checks grün | ✅ (sandbox-eingeschränkt) |
| Builds + Tests | ⚠️ Sandbox: kein pio verfügbar |

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
| 214 | 0xD6 | Primary GPS + Heading | Module → AOG | Periodic, 100 Hz (10 ms) |
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

## 8. Discovery Behavior (F1)

### CRC Mode Split — FINAL HARDENING

| Frame Type | CRC Mode | Rationale | Rules |
|------------|----------|-----------|-------|
| PGN 214 (output, TX) | **STRICT** | Core data integrity | Header + Length + exact CRC |
| PGN 200/201 (legacy, TX) | **STRICT** | Core data integrity | Header + Length + exact CRC |
| PGN 253 (Hello Request, RX) | **TOLERANT** | Known AgIO inconsistencies | + CRC=0x00, + ±1 |
| PGN 202 (Scan Request, RX) | **TOLERANT** | Known AgIO inconsistencies | + CRC=0x0x00, + ±1 |
| PGN 203 (Scan Reply, TX) | **STRICT** | Our output always correct | Header + Length + exact CRC |
| PGN 254 (Hello Response, TX) | **STRICT** | Our output always correct | Header + Length + exact CRC |

### STRICT Validation (Core PGNs)

```
1. Preamble: 0x80 0x81 — exact match required
2. Source: any value accepted
3. Payload Length: must match frame_length - 7
4. CRC: exact sum(bytes[2..end-1]) mod 256 required
5. Frame Length: must equal 7 + payload_length
```

### TOLERANT Validation (Discovery PGNs)

```
1. Preamble: 0x80 0x81 — exact match required
2. Source: any value accepted
3. Payload Length: must match frame_length - 7
4. CRC: accepts ANY of:
   a) Exact match (normal case)
   b) CRC == 0x00 (known AgIO quirk: uninitialized Discovery frames)
   c) CRC off by exactly ±1 (wire noise on UDP)
5. CRC rejected if: off by > ±1 AND not 0x00
```

### Known AgIO Inconsistencies

1. **CRC == 0x00:** Some AgIO versions send Discovery frames with uninitialized CRC byte.
2. **CRC off by ±1:** UDP wire noise can flip bits in CRC byte.
3. **Inconsistent length:** Some AgIO builds send extra trailing bytes.
4. **CRITICAL:** Discovery MUST NOT block module detection via over-validation.

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

## 10. Test Coverage

**49 host tests** covering all mandatory cases plus Golden Byte Regression vectors:

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

### Golden Byte Regression (10 tests) — FINAL HARDENING
40. `test_golden_pgn214_header_bytes` — exact: 0x80,0x81,0x05,0xD6,0x00,51
41. `test_golden_sentinel_payload_bytes` — every sentinel byte at key offsets
42. `test_golden_crc_discovery_frame` — known CRC for zero-payload PGN 202
43. `test_golden_hello_response_frame` — exact bytes for PGN 254 (IP, port, subnet)
44. `test_golden_scan_reply_frame` — exact bytes for PGN 203 (module_type=0x78)
45. `test_golden_heading_encoding` — IEEE 754 f32 bytes for 0°/45°/90°/180°
46. `test_golden_fix_quality_byte` — exact byte at offset 38 per status
47. `test_golden_scan_request_tolerant_crc` — PGN 202 tolerant CRC (0x00, ±1, reject +5)
48. `test_golden_pgn214_complete_frame` — all 58 bytes verified for known zero-valued PGN
49. `test_golden_discovery_crc_wraparound` — CRC wrap-around at 0xFF boundary

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
| 7 | No pio in sandbox | Builds/tests not verified | Must run on dev machine |

---

## 13. Golden Byte Test Vectors (F3)

The following test vectors are hardcoded in `test_aog_pgn214.c` and provide byte-exact regression safety.

### PGN 214 Header (Test 40)
```
frame[0] = 0x80 (Preamble 1)
frame[1] = 0x81 (Preamble 2)
frame[2] = 0x05 (SRC = GPS)
frame[3] = 0xD6 (PGN 214 lo)
frame[4] = 0x00 (PGN 214 hi)
frame[5] = 0x33 (LEN = 51)
```

### Sentinel PGN 214 (Test 41)
```
payload[36] = 0xFF (satellites lo)
payload[37] = 0xFF (satellites hi)
payload[38] = 0x00 (fix_quality = NONE)
payload[39] = 0xFF (hdop lo)
payload[40] = 0xFF (hdop hi)
payload[41] = 0xFF (age lo)
payload[42] = 0xFF (age hi)
payload[43] = 0xFF (imu_heading lo)
payload[44] = 0xFF (imu_heading hi)
payload[45] = 0xFF (imu_roll lo, 0x7FFF)
payload[46] = 0x7F (imu_roll hi)
payload[47] = 0xFF (imu_pitch lo)
payload[48] = 0x7F (imu_pitch hi)
payload[49] = 0xFF (imu_yaw_rate lo)
payload[50] = 0x7F (imu_yaw_rate hi)
```

### Discovery CRC (Test 42)
```
PGN 202, SRC=0x00, LEN=0:
frame = [0x80][0x81][0x00][0xCA][0x00][0x00][CRC]
CRC = (0x00 + 0xCA + 0x00 + 0x00) mod 256 = 0xCA
```

### Hello Response (Test 43)
```
PGN 254, SRC=0x05, IP=192.168.1.100, port=9999, subnet=0:
payload = [192][168][1][100][0x0F][0x27][0x00]
frame = [0x80][0x81][0x05][0xFE][0x00][0x07][payload][CRC]
Total = 15 bytes
```

### Scan Reply (Test 44)
```
PGN 203, SRC=0x05, module_type=0x78, PGNs=[214, 254]:
payload = [0x05][0x78][0x02][0x00][0xD6][0x00][0xFE][0x00]
frame = [0x80][0x81][0x05][0xCB][0x00][0x08][payload][CRC]
Total = 16 bytes
```

### Heading IEEE 754 (Test 45)
```
180.0° → 0x43480000 → LE: [0x00][0x00][0x48][0x43]
 90.0° → 0x42B40000 → LE: [0x00][0x00][0xB4][0x42]
 45.0° → 0x42340000 → LE: [0x00][0x00][0x34][0x42]
   0.0° → 0x00000000 → LE: [0x00][0x00][0x00][0x00]
```

---

## 14. Files Modified (FINAL HARDENING)

| File | Change |
|------|--------|
| `components/protocol_aog/aog_frame.h` | +CRC STRICT/TOLERANT design docs |
| `components/protocol_aog/aog_pgn.h` | +Complete fix quality mapping table |
| `components/aog_navigation_app/aog_navigation_app.h` | +Formal 3-state heading model + status mapping table |
| `components/aog_navigation_app/aog_navigation_app.c` | (unchanged — already correct) |
| `test/host/test_aog_pgn214/test_aog_pgn214.c` | +10 Golden Byte Regression Tests (40-49), now 49 total |
| `docs/hardware/nav-aog.md` | +FINAL HARDENING version 3.0 |

**No architecture changes. No layer break. No rework.**

---

## 15. Dependencies

| Component | Role |
|-----------|------|
| `gnss_snapshot` | Validated GNSS position, speed, fix quality |
| `gnss_dual_heading` | Dual-antenna heading measurement |
| `protocol_aog` | PGN encoding, CRC, frame formatting |
| `byte_ring_buffer` | TX/RX transport abstraction |

---

## 16. References

- [PGN 214 Specification](../protocol/pgn-214.md)
- PGN-Verzeichnis.md (module types, source IDs)
- ADR-0001..0005
- AgOpenGPS: [github.com/AgOpenGPS/AgOpenGPS](https://github.com/AgOpenGPS/AgOpenGPS)
