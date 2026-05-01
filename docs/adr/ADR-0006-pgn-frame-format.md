# ADR-0006 AOG/PGN Frameformat-Entscheidung

## Status

- ACCEPTED

## Kontext

- Das PGN-Verzeichnis (AgOpenGPS-Dokumentation) beschreibt teils 1-Byte-PGN-Formate.
- Das Repo implementiert bereits konsistent das AOG-v5-Format mit 16-bit PGN (2 Bytes Little-Endian).
- NAV-FIX-001 AP-A erfordert eine explizite, dokumentierte Entscheidung.
- Zwei Varianten standen zur Diskussion:
  - Variante A: PGN-Verzeichnis mit 1-Byte-PGN ist bindend
  - Variante B: AOG-v5/Repo-Zielformat mit 16-bit PGN ist bindend

## Entscheidung

- **Variante B: Das Repo-Zielformat mit 16-bit PGN ist bindend.**
- Frameformat: `[0x80][0x81][SRC][PGN_lo][PGN_hi][LEN][DATA...][CRC]`
- Total frame size = 7 + data_length (nicht 6 + data_length wie bei 1-Byte-PGN)
- CRC = sum(bytes[2..2+4+length-1]) mod 256 = sum(SRC + PGN_lo + PGN_hi + LEN + DATA[0..n-1])
- PGN wird als uint16_t behandelt, kodiert als 2 Bytes Little-Endian

## Konsequenzen

- Alle PGN-Zahlen werden als `uint16_t` behandelt.
- Der Parser (`aog_parser_t`) nutzt 2-Byte-PGN-Extraktion im State `AOG_PARSE_PGN`.
- TX-Encoder (`aog_frame_encode`) schreibt PGN als 2 Bytes LE.
- RX-Parser liest PGN als 2 Bytes LE (`pgn_lo | (pgn_hi << 8)`).
- Tests verwenden 16-bit-Format (`frame[3]=PGN_lo`, `frame[4]=PGN_hi`).
- Kompatibel mit AOG v5.x und hoehere Versionen.
- Nicht abwaertskompatibel mit hypothetischem 1-Byte-PGN-Format.
- AOG_MAX_FRAME_SIZE = 7 + AOG_MAX_DATA_SIZE = 87 Bytes.
