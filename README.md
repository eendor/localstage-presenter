# 🎬 LocalStage

**Local Network Presentation App** — Share your screen with anyone on the same Wi-Fi, no internet required.

A local-first presentation platform with real-time slide sharing, presenter notes, and synchronized audience viewing over a local network, without internet access.

A presenter runs a small server on their laptop, shares a screen/window/browser tab, and shows a local IP address or QR code. Viewers open that address in a browser and watch the live stream. Only the presenter installs the app. Viewers need no installation or accounts.

## Quick Start

### Prerequisites

- **Node.js** >= 22.13.0 (Node 24 tested by automated checks)
- **npm** >= 9.0.0
- A local network (Wi-Fi router, laptop hotspot, or phone hotspot)

### Install

```bash
# Open a terminal in the LocalStage project directory
npm install
```

### Build

```bash
npm run build
```

### One-Click Run (Windows)

Simply double-click **`run-localstage.bat`** (or run `.\run-localstage.ps1` in PowerShell).  
It will automatically verify dependencies, build if needed, start the local server, and open your browser directly to the presenter page.

### Manual Run

```bash
npm start
# or start with automatic browser opening:
npm run launch
```

Open the **one-use presenter link** printed in the terminal. It uses
`http://127.0.0.1:3000/host#...`, expires after five minutes, and exchanges the
secret for a host token. Restart the server if the link has expired.
The presenter page shows the selected LAN address and locally generated QR code.
The host link must not be shared with viewers.

`npm run launch` starts the built server and opens the presenter link automatically.
`PORT` overrides port 3000. The server listens on loopback and the selected LAN
IPv4 address. Changing adapters ends the active session; restart sharing afterward.

### Development

```bash
# Run both server and web frontend in dev mode
npm run dev

# Or run them separately
npm run dev:server
npm run dev:web
```

## How It Works

1. The presenter opens the authenticated launcher link and selects screen sharing or a deck
2. Viewers connect to the displayed LAN address from their devices
3. Video streams directly between presenter and viewers via WebRTC
4. All communication stays on the local network — no internet needed after installation

## Project Structure

```
LocalStage/
├── apps/
│   ├── server/          # Node.js + Express backend
│   │   └── src/
│   │       ├── index.ts         # Startup & shutdown
│   │       ├── network.ts       # Network interface discovery
│   │       ├── http.ts          # HTTP routes & static serving
│   │       ├── signaling.ts     # WebSocket signaling server
│   │       ├── session-store.ts # Session & viewer state
│   │       └── auth.ts          # Host auth & PIN rate limiting
│   └── web/             # React + TypeScript frontend
│       └── src/
│           ├── main.tsx         # App entry & routing
│           ├── pages/           # Host, Join, Viewer pages
│           ├── net/             # Signaling client
│           └── styles/          # Global CSS
├── packages/
│   └── protocol/        # Shared message types & validation
│       └── src/
│           └── index.ts
├── docs/
│   ├── network-setup.md    # Network troubleshooting
│   └── test-results.md     # Device & browser test results
└── package.json         # Monorepo root
```

## Tech Stack

| Layer | Technology |
|-------|------------|
| Frontend | React + TypeScript + Vite |
| Backend | Node.js + Express |
| Signaling | WebSocket (`ws`) |
| Media | WebRTC (`RTCPeerConnection`) |
| Screen Capture | `getDisplayMedia()` |
| QR Code | `qrcode` (bundled locally) |

## Browser Support

Target browsers (physical-device results still pending in `docs/test-results.md`):
- Chrome/Edge (desktop) — primary target, capture requires loopback
- Android Chrome — to be tested
- iPhone/iPad Safari — to be tested

## Network Requirements

- Presenter and viewers must be on the same reachable local network
- The presenter's firewall must allow connections on port 3000 (TCP)
- WebRTC media may use additional dynamic ports
- See `docs/network-setup.md` for detailed troubleshooting

## License

Private project — all rights reserved.

## Uploaded presentations

PDF rendering is installed with npm and runs locally in a bounded child process.
Upload and preview a deck before starting a deck-only presentation, or add a deck
to a screen-sharing session. Viewers can independently review slides up to the
presenter's **current** slide; moving backward tightens that bound immediately.
Original files and rendered slides are stored outside the static web directory.
Slide requests require membership and are not cached.

For PPT/PPTX, install LibreOffice locally and set `LIBREOFFICE_PATH` to the absolute
path to `soffice.exe` (or `soffice` on other systems). Example in PowerShell:

```powershell
$env:LIBREOFFICE_PATH = 'C:\Program Files\LibreOffice\program\soffice.exe'
npm start
```

Office conversion is implemented but unverified on this machine. Export to PDF
for the tested conversion path. Macro execution and link updates are disabled in
the isolated LibreOffice profile; an OS-level network/resource sandbox remains
unfinished. Hidden-slide, notes, font and encrypted Office-file fixtures still
need validation. Configurable limits: `MAX_DECK_BYTES` (default `50mb`) and
`MAX_DECK_PAGES` (default `200`); conversion timeout is two minutes.

## Lifecycle and current limits

- Maximum five viewers per session by default; capacity is not yet benchmarked.
- Host disconnect ends the session immediately (zero-second grace), closing peers.
  A reload requires a new user-approved capture. Broken sockets are checked every
  15 seconds and terminated after a missed heartbeat.
- Viewers retry with bounded exponential backoff and validated membership.
- Manual pause replaces video with a slate and suppresses captured audio.
- Automatic still mode compares full-resolution pixels after five unchanged
  seconds. A viewer must decode/display and acknowledge its PNG before its video
  sender is suspended. Audio stays active. Hidden dashboards fall back to live.
  Browser race/performance validation is still pending; use **Always live** to
  disable automatic transitions.
- Deck-only mode does not require capture or a video stream. To switch from a
  deck-only session to screen sharing, end the session and start a screen session.
- Offline real-device, mobile, fullscreen/audio, and 20-minute capacity tests remain
  open. The conditional classroom SFU upgrade is not implemented.

## Verification

```bash
npm run check
```

This builds all workspaces and runs protocol/authentication/signaling and PDF
conversion/slide-access tests. See `TASKS.md` and `docs/test-results.md` for the
distinction between implemented work and measured acceptance results.
