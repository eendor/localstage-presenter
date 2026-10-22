# LocalStage

Share your screen or a slide deck with anyone on the same Wi-Fi. No internet, no accounts, nothing to install for viewers.

This fork is a rewrite in plain C. The server is a single binary with no Node.js or npm. Browsers can't run C, so the presenter and viewer pages are one small hand-written HTML/JS file, built into the binary. Screen sharing still uses WebRTC in the browser.

<p>
  <img src="docs/presenter.png" alt="Presenter page" width="68%">
  <img src="docs/join-phone.png" alt="Viewer join page on a phone" width="24%">
</p>

**Download:** [Windows, macOS, Linux](https://github.com/eendor/localstage-presenter/releases/latest)

## What it's for

LocalStage turns your laptop into a small presentation server for the room you're in. You start it and get a link and a QR code. People scan it with their phone or laptop and see your slides or your screen live in their browser. Nothing leaves your local network.

It's meant for situations where the usual options don't fit:

- **No projector, or a bad one.** Everyone follows along on their own screen instead of squinting at the wall.
- **No internet, or internet you can't trust.** Video calls and cloud slide links need the internet. LocalStage only needs a shared Wi-Fi or hotspot, even one with no internet connection.
- **No sign-ups.** Viewers don't install an app or create an account. They open a link.
- **Privacy.** Your slides and screen go straight from your computer to the viewers' browsers over the local network. No third-party server sees them.

## Use cases

- **Classrooms and workshops:** students follow slides on their phones and can flip back to an earlier slide when you allow it.
- **Meetups and small talks:** in a café, a meeting room or a hackathon without an HDMI cable.
- **Team meetings:** walk through a document or a demo on your screen without setting up a call.
- **Offline events:** field trainings, rural schools, or venues with no internet, using a phone hotspot.
- **Accessibility:** people at the back of the room or with low vision can zoom in on their own device.
- **Rehearsing:** read your speaker notes in the presenter script panel while viewers see only the slides.

## Two ways to present

| Mode | What viewers see | When to use it |
|---|---|---|
| **Upload a deck** (PDF, PPTX, PPT) | Sharp slide images, which you advance from your laptop | Slide presentations. No screen capture is needed and it uses very little bandwidth. Portrait PDFs are fitted automatically. |
| **Share screen** | A live video of a window or your whole screen | Live demos, documents, anything that isn't a slide deck. |

## Limits

- The presenter and viewers must be on the same network.
- Up to 50 viewers. Screen sharing sends a separate video stream to each viewer, so with many viewers a deck works better.
- PDF decks need Poppler installed. PowerPoint decks also need LibreOffice.

## Requirements

- Linux or macOS, with a C compiler (`cc`/`gcc`/`clang`) and `make`
- For deck uploads: `poppler-utils` (`pdfinfo`, `pdftoppm`). PPT and PPTX files also need LibreOffice (`soffice`).
- Presenter browser: desktop Chrome or Edge. Viewers can use any current browser.

## Build and run

```bash
make            # builds ./localstage
make test       # end-to-end tests (starts a server on a random port)
./localstage
# or simply: ./run.sh   (builds, starts, opens the browser)
```

The server prints a one-use presenter link (`http://127.0.0.1:3000/host#...`, valid for 5 minutes) and the join address for each network adapter. Open the presenter link on this computer. Viewers open the `/join` address or scan the QR code.

## macOS

Install Xcode command line tools and `brew install poppler` (plus LibreOffice for slides), then `make run`.

## Windows

Download `localstage-windows.zip` from the [Releases](https://github.com/eendor/localstage-presenter/releases) page, unzip it, and double-click **run-localstage.bat**. The presenter page opens in your browser; allow the firewall prompt so phones on your Wi-Fi can join.

PDF decks need Poppler: grab a build from [poppler-windows](https://github.com/oschwartz10612/poppler-windows/releases) and unzip it into a `poppler` folder next to `localstage.exe` (or put it on `PATH`). PowerPoint/ODP decks also need LibreOffice (`soffice` on `PATH`).

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

See `docs/network-setup.md` for network setup and troubleshooting.
