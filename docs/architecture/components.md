# Komponentenmodell

- Eine Komponente DARF KEIN eigener Task sein.
- Eine Komponente DARF mehrere Rollen haben.
- Eine Komponente DARF folgende Funktionen implementieren:
  - init
  - start
  - stop
  - deinit
  - fast_input
  - fast_process
  - fast_output
  - service_step
- KEINE dieser Funktionen ist verpflichtend.
- Eine Komponente DARF ausschließlich auf Core 0 laufen.
