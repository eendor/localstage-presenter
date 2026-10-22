# LocalStage: technical notes

Building from source, settings, and how it works inside.

## Requirements

- Linux or macOS, with a C compiler (`cc`/`gcc`/`clang`) and `make`
- PDFs are rendered in the presenter's browser with the bundled PDF.js (`web/vendor`). If that fails, the server falls back to `poppler-utils` (`pdfinfo`, `pdftoppm`) when installed. PPT and PPTX files need LibreOffice (`soffice`); standard install folders on Windows and macOS are found automatically.
- Presenter browser: desktop Chrome or Edge. Viewers can use any current browser.

## Build and run

```bash
make            # builds ./localstage
make test       # end-to-end tests (starts a server on a random port)
./localstage
# or simply: ./scripts/run.sh   (builds, starts, opens the browser)
```

The server prints a one-use presenter link (`http://127.0.0.1:3000/host#...`, valid for 5 minutes) and the join address for each network adapter. Open the presenter link on this computer. Viewers open the `/join` address or scan the QR code.

## macOS

Install the Xcode command line tools, then `make run`.

## Windows

Download `LocalStage-windows.exe` from the [Releases](https://github.com/eendor/localstage-presenter/releases) page and double-click it. The presenter page opens in your browser; allow the firewall prompt so phones on your Wi-Fi can join.

Release builds ship a single `LocalStage-windows.exe`: double-click opens the browser (set `OPEN_BROWSER=0` to skip). `scripts/run-localstage.bat` is an optional launcher that also offers to install LibreOffice with winget.

To build the exe yourself: `make windows` (MinGW cross-compiler, e.g. `pacman -S mingw-w64-gcc`) or natively in MSYS2.

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
- PDF, PPTX and PPT decks are supported. Portrait and landscape pages are detected per page: each is rendered with its long side at 1920 px and shown fitted to the screen without cropping.
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

See [NETWORK.md](NETWORK.md) for network setup and troubleshooting.

## Android

`android/app.c` is the app: a NativeActivity in C, plus a tiny `Main.java` that only opens Android's file picker for the page's file button. It runs the server on a thread and shows the presenter page in a WebView built through JNI. PPTX files are drawn in the browser (pptx-renderer + html2canvas, `web/vendor`) and uploaded as slide images; computers use LibreOffice when installed and fall back to the browser otherwise. The server follows a hotspot or tether adapter automatically while no presentation is running. Shared PDFs are copied to the app's storage and picked up by the page via `GET /api/shared-deck`.

Build the APK (needs the Android SDK and NDK):

```sh
ANDROID_HOME=~/Android/Sdk android/build.sh   # writes LocalStage.apk
```

Release builds are signed in CI with the `ANDROID_KEYSTORE_B64` / `ANDROID_KEYSTORE_PASS` secrets.
