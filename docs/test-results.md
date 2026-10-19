# Test Results

> Record actual test results here. Do not fill in results that haven't been tested.

## Automated implementation check — 2026-10-19

- Environment: Windows, Node v24.19.0.
- `npm run check`: TypeScript and Vite production build; signaling and deck tests.
- Signaling: one-use host bootstrap, wrong host token, wrong PIN, viewer-to-host
  role rejection, full session, locked session, offer/answer relay, stale offer
  rejection, removal/token revocation, session end.
- PDF: generated two-page PDF rendered to PNG with the local PDF.js/native canvas
  subprocess. Current-slide access, guessed future slide rejection, host preview,
  stale revision/sequence rejection, backward bounds and invalid/page-limited PDF
  rejection are covered.
- Browser smoke attempt: setup and join pages loaded and synthetic-source peers
  reached `connected`. No decoded viewer video frame was confirmed in the embedded
  browser; this is **not** a streaming acceptance pass. Real capture is untested.
- LibreOffice/Poppler executables were not found on PATH. PPT/PPTX conversion,
  missing fonts, hidden slides, notes and encrypted Office files are unverified.
- No second physical device, disconnected-internet test, mobile browser test or
  20-minute capacity run was performed. Empty tables below remain intentional.

## Environment

| Item | Value |
|------|-------|
| Date | |
| Presenter OS | |
| Presenter Browser | |
| Node.js Version | |
| Network Type | |

## Device Tests

| # | Viewer Device | Viewer Browser | Page Load | Video Stream | Audio | Fullscreen | Notes |
|---|--------------|----------------|-----------|-------------|-------|------------|-------|
| 1 | | | | | | | |
| 2 | | | | | | | |
| 3 | | | | | | | |
| 4 | | | | | | | |
| 5 | | | | | | | |

## Capacity Test (20 minutes)

| Viewers | Resolution | FPS | Bitrate (per viewer) | CPU Usage | Packet Loss | Observed Delay | Notes |
|---------|-----------|-----|---------------------|-----------|-------------|----------------|-------|
| 1 | | | | | | | |
| 3 | | | | | | | |
| 5 | | | | | | | |

## Feature Tests

| Test | Result | Notes |
|------|--------|-------|
| Offline cold startup | | |
| Capture denial | | |
| Source switching | | |
| Pause privacy | | |
| Browser stop button | | |
| Host reload | | |
| Viewer reconnect | | |
| Wrong PIN | | |
| Room full | | |
| Room locked | | |
| Still mode (5s) | | |
| Still mode resume | | |
| PDF upload | | |
| PPTX upload | | |
| PPT upload | | |
| Split review | | |
| Backward navigation clamp | | |

## Known Issues

| Issue | Severity | Status |
|-------|----------|--------|
| | | |
