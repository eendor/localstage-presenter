# LocalStage

Share your screen or a slide deck with anyone on the same Wi-Fi. No internet, no accounts, nothing to install for viewers.

This fork is a rewrite in plain C. The server is a single binary with no Node.js or npm. Browsers can't run C, so the presenter and viewer pages are one small hand-written HTML/JS file, built into the binary. Screen sharing still uses WebRTC in the browser.

## Requirements

- Linux or macOS, with a C compiler (`cc`/`gcc`/`clang`) and `make`
- For deck uploads: `poppler-utils` (`pdfinfo`, `pdftoppm`). PPT and PPTX files also need LibreOffice (`soffice`).
- Presenter browser: desktop Chrome or Edge. Viewers can use any current browser.

## Build and run

```bash
make            # builds ./localstage
make test       # end-to-end tests (starts a server on a random port)
./localstage
```

The server prints a one-use presenter link (`http://127.0.0.1:3000/host#...`, valid for 5 minutes) and the join address for each network adapter. Open the presenter link on this computer. Viewers open the `/join` address or scan the QR code.

## Settings (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `3000` | TCP port |
| `OPEN_BROWSER` | unset | `1` opens the presenter link automatically |
| `MAX_DECK_BYTES` | `150mb` | Upload size limit (`kb`/`mb`/`gb` suffixes) |
| `MAX_DECK_PAGES` | `200` | Page limit for a converted deck |
| `LIBREOFFICE_PATH` | `soffice` | LibreOffice binary for PPT/PPTX |

## How it works

- The presenter page is served only on 127.0.0.1. Viewers reach the server on the selected LAN adapter.
- Signaling runs over a WebSocket at `/ws`. Video goes peer-to-peer over WebRTC with no STUN or TURN servers, so it never leaves the local network.
- Decks are converted to PNG slides in a separate process, with a 120-second limit. Viewers can fetch the live slide and earlier slides only. They can't see future slides, and when the presenter turns on the review lock they see only the live slide.

## Changes from the original

- The server is rewritten in C (`src/`), and the React client is replaced by `web/index.html`.
- Bug fixes:
  - The join page keeps polling, so viewers who arrive before the presenter can join without reloading.
  - Old slide image blob URLs are now freed.
  - Stale deck conversions are discarded.
  - The PIN limiter fails closed when its table is full.
  - Removed viewers lose their slide access immediately.
  - WebRTC messages are rebuilt from validated fields before they are relayed.
  - A clear message is printed when the port is already in use.
  - HTTP requests that use an unknown Host or chunked transfer encoding are rejected.
- "Still image" mode is gone. Use a deck upload instead.
- The Windows `.bat`/`.ps1` launchers are gone.

See `docs/network-setup.md` for network setup and troubleshooting.
