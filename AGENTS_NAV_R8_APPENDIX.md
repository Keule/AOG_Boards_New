# AGENTS.md Appendix – NAV-R8 protected lessons

For NAV recovery/refactor tasks, agents MUST read and follow:

- `docs/adr/ADR-020-nav-boot-safe-gnss-ntrip-lessons.md`
- `docs/testing/NAV-R8-RECOVERY-TEST-MATRIX.md`

Protected subsystems unless task explicitly allows changes:
- Board/Profile logging and pin sanity
- HAL Ethernet init/start
- RemoteDiag and OTA route registration
- UART service ownership model
- RTCM router fanout to both GNSS receivers
- task_fast no-network/no-blocking invariant

Known critical lessons:
- GNSS2 RX uses GPIO34, not GPIO35.
- RX2 uses UM980 COM2 successfully.
- RX1 must not be assumed COM2; require COM1/COM2 matrix or explicit receiver-specific port config.
- UM980 output rates are configured by `LOG COMx <sentence> ONTIME <period>`.
- ACK alone is insufficient; effect verification of NMEA rates is mandatory.
- NTRIP config source must be visible and secrets masked.
- GNSS invalid/fresh failures require explicit Drop-Reasons.
