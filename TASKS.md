# LocalStage — Project Tasks

> **Last updated:** 2026-10-19  
> **Status legend:** ✅ Done · 🔲 Not started · 🚧 In progress

> **Audit:** Checked implementation items mean code exists and builds, not that all
> device acceptance gates have passed. Physical-device tests remain unchecked.
> See `docs/test-results.md` for actual evidence. Remaining engineering includes
> Office-conversion sandbox/fixtures, recovery edge cases, and the conditional SFU.

---

## Phase 0 — Project Setup

- [x] Write product blueprint / specification document
- [x] Initialize monorepo structure (`apps/server`, `apps/web`, `packages/protocol`)
- [x] Set up TypeScript configuration (root + per-package)
- [x] Set up Vite for frontend (`apps/web`)
- [x] Set up Node.js + Express backend (`apps/server`)
- [x] Install and configure shared dev dependencies (concurrently, rimraf, tsx)
- [x] Create `packages/protocol` with shared message schemas and types
- [x] Create `README.md` with install/build/start instructions
- [x] Create `docs/network-setup.md` (router/hotspot/firewall troubleshooting)
- [x] Create `docs/test-results.md` template

---

## Phase 1 — Feasibility (Minimal LAN Streaming)

> **Gate:** A second physical device watches screen video over LAN with internet disconnected.

### 1.1 Local Server Bootstrap
- [x] `apps/server/src/index.ts` — Startup, listener config, graceful shutdown
- [x] `apps/server/src/network.ts` — Adapter enumeration via `os.networkInterfaces()`, let presenter pick interface
- [x] `apps/server/src/http.ts` — Serve built frontend assets + basic HTTP endpoints
- [x] Bind loopback for presenter (`127.0.0.1:3000/host`) and selected LAN IP for viewers

### 1.2 Signaling Server
- [x] `apps/server/src/signaling.ts` — WebSocket server using `ws`
- [x] Handle `rtc:offer`, `rtc:answer`, `rtc:ice` relay messages
- [x] Tag messages with session ID, viewer ID, connection-generation ID
- [x] Reject stale or unauthorized messages

### 1.3 Presenter Capture
- [x] `apps/web/src/media/capture.ts` — `getDisplayMedia()` capture start/stop
- [x] Handle capture denial/cancel gracefully
- [x] Handle browser stop button (`track.ended` event)
- [x] Keep local preview muted

### 1.4 Host Peer Management
- [x] `apps/web/src/media/host-peers.ts` — Create one `RTCPeerConnection` per viewer
- [x] Presenter as offerer; attach captured tracks to each peer
- [x] Exchange SDP offers/answers through signaling server
- [x] Exchange ICE candidates; queue until remote description is set
- [x] Use `RTCPeerConnection({ iceServers: [] })` for local-only

### 1.5 Viewer Peer & Playback
- [x] `apps/web/src/media/viewer-peer.ts` — Receive-only peer lifecycle
- [x] Attach incoming stream to `<video>` element
- [x] Handle `video.play()` rejection (tap-to-play fallback)
- [x] No `getDisplayMedia` or `getUserMedia` calls on viewer side

### 1.6 Feasibility Validation
- [ ] Test on second physical device over Wi-Fi with internet disconnected
- [ ] Verify page loads from local assets (no CDN/external fetch)
- [ ] Verify video stream plays on viewer device
- [ ] Document browser/OS/device used and result in `docs/test-results.md`

---

## Phase 2 — Session Foundation

> **Gate:** Invalid roles/messages cannot control the host; stop releases capture and peers.

### 2.1 Session Store
- [x] `apps/server/src/session-store.ts` — In-memory session and viewer lifecycle
- [x] Core types: `Session` (id, title, status, joinLocked, maxViewers, createdAt)
- [x] Core types: `Viewer` (id, sessionId, displayName, state, joinedAt)
- [x] One active session at a time

### 2.2 Protocol & Validation
- [x] `packages/protocol/src/index.ts` — Shared message schemas and TypeScript types
- [x] Define all protocol messages (`host:create`, `viewer:join`, `viewer:accepted`, `viewer:ready`, `rtc:offer`, `rtc:answer`, `rtc:ice`, `session:state`, `host:lock`, `host:remove-viewer`, `host:end`, `viewer:left`)
- [x] Validate message schemas, roles, membership, target IDs
- [x] Enforce message-size limits

### 2.3 Authentication & Authorization
- [x] `apps/server/src/auth.ts` — Host bootstrap secret (short-lived, loopback only)
- [x] Join token issuance for admitted viewers
- [x] PIN rate limiting
- [x] Role enforcement on all server-side message handlers
- [x] Keep credentials separate from public session state

### 2.4 Signaling Client
- [x] `apps/web/src/net/signaling-client.ts` — WebSocket connection with retry handling
- [x] Reconnect with bounded backoff

### 2.5 Lifecycle & Cleanup
- [x] Close all peer connections on session end
- [x] Release capture tracks on stop
- [ ] Remove all listeners and timers on cleanup
- [x] Handle host disconnect with documented zero-second grace (immediate end)

---

## Phase 3 — Core Interface

> **Gate:** Actual controls operate actual streaming and show accurate states.

### 3.1 Setup Page (Screen A)
- [x] `apps/web/src/pages/HostPage.tsx` — Setup form
- [x] Session title input
- [x] Network adapter dropdown (populated from server)
- [x] Optional 6-digit join PIN
- [x] Source choice: "Share screen" or "Upload presentation"
- [x] Viewer URL display + copy button
- [x] QR code generation (locally bundled library)
- [x] "Start sharing" primary action button
- [x] Inline help / same-network instruction
- [x] Local server status indicator

### 3.2 Presenter Dashboard (Screen B)
- [x] Top bar: session title, sharing state, viewer count, End button
- [x] Large muted preview of selected source (2/3 width on desktop)
- [x] Controls: Pause/Resume, Change source, Audio control, Quality preset
- [ ] Right panel (1/3 width): Viewer address, QR, PIN, Lock/Unlock joining
- [x] Viewer list panel: admitted viewers, signaling membership state, Remove action
- [x] Status details area with connection warnings
- [x] Responsive: stack side panel below preview on narrow screens
- [x] Preview and outgoing media reflect same sharing state

### 3.3 Viewer Join Page (Screen C)
- [x] `apps/web/src/pages/JoinPage.tsx`
- [x] Presentation title display
- [x] Optional name field (generated label if omitted)
- [x] PIN field (conditional)
- [x] "Watch presentation" button
- [x] Clear error messages: wrong PIN, session full, joining locked, host unavailable

### 3.4 Viewer Playback Page (Screen D)
- [x] `apps/web/src/pages/ViewerPage.tsx`
- [x] Compact header with title and connection state
- [x] Large `<video>` with `object-fit: contain`
- [x] Controls: fit/fullscreen, audio, leave
- [x] Mobile landscape layout + 44px touch targets (device validation pending)
- [x] State overlays: waiting, paused, reconnecting, ended
- [x] Expand-in-page fallback when native fullscreen unavailable

### 3.5 Connection Help (Screen E)
- [x] `docs/network-setup.md` — Distinguish "page won't open" vs "page opens but video won't connect"
- [x] Show actual address, adapter, and port in help area

### 3.6 UI Polish
- [x] Dark video area, clean presentation-focused design
- [x] System fonts and text controls; no CDN assets
- [ ] Responsive layout tested on desktop + mobile viewports

---

## Phase 4 — Presentation Controls

> **Gate:** Join controls work; paused media does not leak changing screen content.

### 4.1 QR Code & Join Flow
- [x] QR encodes actual LAN viewer URL (not hardcoded)
- [x] QR regenerates on IP/adapter change

### 4.2 PIN & Admission
- [x] Optional PIN gate on join
- [x] Rate-limited PIN attempts
- [x] Clear error for wrong PIN

### 4.3 Room Controls
- [x] Lock joining (prevents new joins, existing viewers stay)
- [x] Unlock joining
- [x] Remove viewer (revoke token + close peer connection)

### 4.4 Pause & Resume
- [x] Pause: replace outgoing video with blank/slate track
- [x] Pause: suppress outgoing audio
- [x] Pause: clear snapshot and replace captured content with slate
- [x] Resume: restore active capture tracks
- [x] Resume: verify tracks still available

### 4.5 Source Switching
- [x] "Change source" opens browser capture picker on user click
- [x] Replace tracks with `replaceTrack()` where possible
- [x] Cancel picker keeps current share
- [x] Rebuild peer when audio transceiver activation changes

### 4.6 Audio
- [x] Check actual returned audio tracks (don't assume availability)
- [x] Display audio availability indicator
- [x] Viewer audio enable/disable control
- [x] Keep local preview muted always

### 4.7 Quality Presets
- [x] Slides preset: 1920×1080, 10–15 fps, ~2 Mbps
- [x] Balanced preset (default): 1280×720, up to 30 fps, ~2–3 Mbps
- [x] Low bandwidth preset: 960×540, 10–15 fps, ~0.8–1.2 Mbps
- [x] Show actual negotiated output when available

---

## Phase 4a — Automatic Still Mode

> **Gate:** Still displays before video stops; changes resume video; audio continues.

### 4a.1 Change Detection
- [x] `apps/web/src/media/change-detector.ts` — Frame comparison logic
- [x] Compare captured pixels (not input inactivity)
- [x] Tile-based comparison + full-resolution verification
- [ ] Detect small text edits, gradual changes, blinking carets, cursor movement
- [x] Monotonic 5-second inactivity timer; reset on any detected change
- [ ] Test background/minimized behavior (dashboard may be hidden)
- [x] Fall back to live video if detector cannot run reliably

### 4a.2 Snapshot Encoding & Transfer
- [x] `apps/web/src/media/snapshot-channel.ts` — Data channel snapshot delivery
- [x] Encode current frame as full-resolution PNG
- [x] Send via reliable, ordered `RTCDataChannel`
- [x] Bounded chunks (start at 16 KiB, respect negotiated max message size)
- [x] Handle backpressure
- [x] Include session, source-generation, mode-generation, snapshot IDs
- [x] Validate MIME type, dimensions, total byte length

### 4a.3 Still Mode Transitions
- [x] `apps/web/src/media/still-mode.ts` — Live/static transitions
- [x] Wait for viewer acknowledgement (decode + display confirmed) before `replaceTrack(null)`
- [x] Keep peer connection, data channel, capture source, audio sender active
- [x] Keep failed/slow viewers on live video (don't leave blank)
- [x] Track each viewer's committed mode separately

### 4a.4 Resume from Still
- [x] On detected change: invalidate pending snapshot generations
- [x] Reattach capture track with `replaceTrack(captureTrack)`
- [x] Display snapshot until fresh resumed video frame arrives
- [x] Check frame progress against pre-resume baseline
- [x] Show recovery state if fresh video cannot resume
- [x] Require another full 5-second interval before re-entering still mode

### 4a.5 Race Conditions & Lifecycle
- [x] Generation IDs prevent late ack from suspending new video
- [x] Invalidate stale encoding/transfers on screen change, source change, session end
- [x] New viewers joining during still mode receive snapshot + negotiate video transceiver
- [x] Manual pause/stop overrides automatic still mode
- [x] Revoke object URLs and release buffers on cleanup
- [x] One detector + encoder per source, not per viewer

### 4a.6 UI for Still Mode
- [x] "Always live" override toggle on presenter dashboard
- [x] Enabled by default with 5-second delay
- [x] "Live video" / "Still image" indicator on presenter dashboard
- [x] Subtle "Live video" / "Still image" indicator on viewer playback
- [x] Viewer indicator clarifies still ≠ manual pause

---

## Phase 4b — Uploaded Presentations & Split Review

> **Gate:** Review stays independent; every access path enforces the current-slide limit.

### 4b.1 File Upload & Processing
- [x] `apps/server/src/decks.ts` — Upload endpoint (presenter-only auth)
- [ ] Accept PDF, PPTX, PPT formats
- [x] Configurable file-size and page-count limits
- [ ] Reject unsupported, encrypted, or password-protected files
- [x] Store original in private session directory (outside static web root)

### 4b.2 Conversion Pipeline
- [x] `apps/server/src/deck-converter.ts` — Local conversion pipeline
- [ ] PPT/PPTX → PDF via LibreOffice subprocess
- [x] PDF → individual slide images via PDF.js (tested local renderer)
- [ ] Bounded subprocess runtime/resources
- [ ] Safe argument passing, disabled macros, no remote resource fetching
- [ ] Exclude speaker notes and hidden slides by default
- [ ] Conversion progress reporting to presenter
- [x] Conversion errors reported clearly; pending status shown while processing
- [ ] Entire pipeline works offline after installation

### 4b.3 Slide Access Control
- [x] `apps/server/src/slide-access.ts` — Authorize slide/thumbnail requests
- [x] Enforce `1 <= requestedSlide <= presenterSlide` on every viewer request
- [x] Reject future slide URLs even if guessed
- [x] Don't expose original PPT/PDF, whole-deck viewer, notes, or unrestricted manifest
- [x] Showing locked slide numbers is OK; showing content is not

### 4b.4 Presenter Slide Controls
- [x] `apps/web/src/components/DeckPresenter.tsx` — Upload preview, thumbnails, nav
- [x] Presenter preview before starting (check converted slides)
- [x] Slide thumbnails on dashboard
- [x] Current slide number display
- [x] Previous / Next buttons
- [x] Keyboard navigation (arrows, etc.)
- [x] Ignore nav hotkeys while typing in a text field
- [x] All navigation routes through one host command handler
- [x] Only actual slide changes update server state

### 4b.5 Server-Authoritative Slide State
- [x] `DeckState`: deckId, deckRevision, totalSlides, presenterSlide, stateSequence
- [x] Server validates all `presenter:navigate` commands
- [x] Broadcast `deck:state` to all members on each committed change
- [x] `deck:activate` on starting a new/converted deck (new revision)
- [x] Switching decks: reset to slide 1, return viewers to follow mode, cancel old loads

### 4b.6 Viewer Review & Split Layout
- [x] `apps/web/src/components/SplitReview.tsx` — Labeled live/review panes
- [x] `apps/web/src/deck/review-state.ts` — Follow/review transitions
- [x] Follow presenter by default (single pane)
- [x] Selecting an earlier slide opens split: "Presenter — Live" | "Your Review"
- [x] Each pane shows its own slide number
- [x] Only review pane has independent slide controls
- [x] "Back to Live" returns to single synchronized pane
- [x] Selecting current slide also returns to follow mode
- [x] On mobile: stack live above review, both visible
- [x] Enforce `1 <= reviewSlide <= presenterSlide` in UI + server

### 4b.7 Backward Navigation & Clamping
- [x] Presenter moves backward below viewer's review → clamp review, return to follow mode
- [x] Late asset responses discarded when deck revision or slide bound changed
- [x] Race handling for old state messages
- [x] Stale navigation ignored (sequence/revision check)

### 4b.8 Deck + Screen Share Combo
- [x] If screen share active: live pane uses live video stream; review pane uses deck assets
- [x] Apply automatic still mode only to the live pane
- [x] Keep one media stream for live/review; browser continuity validation pending
- [x] In deck-only mode: render current slide on change (no video/5-sec detector needed)

---

## Phase 5 — Reliability

> **Gate:** Network interruption and capture denial produce recoverable states.

### 5.1 Viewer Reconnect
- [x] Bounded backoff reconnection
- [x] Fresh or validated membership on reconnect
- [x] Fresh peer negotiation on viewer signaling reconnect
- [x] Full state snapshot after reconnect

### 5.2 Host Disconnect Recovery
- [x] Documented zero-second host grace period
- [x] End immediately on host socket closure
- [x] Release media resources
- [x] After host page reload: require new user-approved capture

### 5.3 Adapter / IP Changes
- [x] Detect disappearance of selected IP/interface
- [x] Stop or reconfigure session
- [x] Regenerate address and QR code

### 5.4 Browser Fallbacks
- [x] Handle capture denial without crashing
- [x] Handle fullscreen API unavailability
- [x] Handle autoplay restrictions
- [x] Handle missing audio tracks gracefully
- [x] Feature-detect capabilities and report unsupported cases

### 5.5 Error States
- [x] Clear distinction: "page won't open" vs "page opens but video won't connect"
- [x] Connection help area with actual diagnostic info
- [x] Origin checks on WebSocket connections

---

## Phase 6 — Capacity & Packaging

> **Gate:** Documented device/browser limits and successful offline cold startup.

### 6.1 Performance Measurement
- [x] `apps/web/src/media/stats.ts` — `getStats()` for bitrate, loss, frames, RTT
- [ ] 20-minute real-device test at 1, 3, and 5 viewers
- [ ] Record: resolution, fps, outgoing bitrate, CPU use, loss, observed delay
- [ ] Test with slides content and moving content
- [ ] Document all results in `docs/test-results.md`

### 6.2 Offline Packaging
- [x] Vite produces compiled frontend (no Vite dev server for viewers)
- [x] Express serves compiled assets
- [x] QR library bundled locally; system fonts/text controls use no CDN
- [ ] Test: cold startup with empty browser cache, internet disconnected
- [x] Lockfile committed

### 6.3 Launcher
- [x] Local launcher starts server + opens presenter page
- [x] Issues host bootstrap secret on loopback
- [x] Firewall instructions for Windows

### 6.4 Documentation
- [x] `README.md` — Exact install/build/start commands
- [ ] Browser compatibility matrix (tested)
- [x] Firewall/hotspot/network setup guide
- [x] Known limitations documented

---

## Phase 7 — Classroom Upgrade (If Needed)

> **Gate:** Target audience works on actual venue network.

- [ ] Evaluate local SFU (mediasoup) for larger audiences
- [ ] Implement SFU signaling and deployment
- [ ] Presenter publishes once; SFU forwards to consumers
- [ ] Snapshot distribution via SFU
- [ ] Test with classroom-sized audience on venue network
- [ ] Document capacity findings and Wi-Fi limitations

---

## Acceptance Checklist (Phase-Independent)

> Cross-cutting validation items from blueprint §9. Check these as each becomes testable.

- [ ] **AC-1:** Offline cold startup — build once, disconnect internet, start app, join from empty-cache device
- [ ] **AC-2:** Join address uses selected adapter and is reachable on another physical device
- [ ] **AC-3:** Presenter can select tab/window/screen from loopback; denying capture leaves usable page
- [ ] **AC-4:** Viewers receive video without camera/mic/screen-capture permission requests
- [ ] **AC-5:** Chrome/Edge desktop tested; Android Chrome and iPhone/iPad Safari results recorded
- [ ] **AC-6:** Audio/autoplay and fullscreen fallback behavior tested
- [ ] **AC-7:** PIN errors, rate limits, full sessions, locked sessions, viewer→host message rejection
- [ ] **AC-8:** Pause while source changes — outgoing content + audio suppressed, then resume works
- [ ] **AC-9:** Stop via app button and browser indicator — viewers see end, capture released
- [ ] **AC-10:** Disconnect/reconnect viewer; close/reload host; change hotspot/IP — documented behavior
- [ ] **AC-11:** 20-min real-device test at 1/3/5 viewers with performance metrics recorded
- [ ] **AC-12:** Page-load failures vs media failures distinguished; firewall rules tested without disabling firewall
- [ ] **AC-13:** Static slide switches to still after 5s; readable; reduces video payload; audio continues
- [ ] **AC-14:** Motion at 4.9s, during transfer, after still; tiny edits, gradual changes, cursors, scrolling, late acks
- [ ] **AC-15:** New viewers in still mode, failed transfers, source changes, repeated transitions, memory cleanup
- [ ] **AC-16:** Present another app with dashboard backgrounded; reliable detection or live fallback; manual pause not auto-overridden
- [ ] **AC-17:** PDF/PPTX/PPT upload — conversion order, slide count, readability, offline. Missing fonts, hidden slides, notes, passwords, failures
- [ ] **AC-18:** Slide 8: follower sees 8; reviewer can select 1–8, can't fetch 9 by URL. Full original not exposed
- [ ] **AC-19:** Reviewer on 5, presenter 8→9: live pane=9, review stays 5. Back to Live works
- [ ] **AC-20:** Presenter backward 8→4 while reviewer on 6: clamp to 4, follow mode. Race handling for old assets
- [ ] **AC-21:** Two viewers review different slides; rapid nav; late joins; reconnects; deck replacement; switch to screen share; session end
- [ ] **AC-22:** Split layout on desktop + mobile — both panes visible, labeled, readable, independent review controls

---

## Summary

| Phase | Total Tasks | Done |
|-------|------------|------|
| 0 — Project Setup | 10 | 10 |
| 1 — Feasibility | 25 | 21 |
| 2 — Session Foundation | 19 | 18 |
| 3 — Core Interface | 36 | 34 |
| 4 — Presentation Controls | 25 | 25 |
| 4a — Automatic Still Mode | 36 | 34 |
| 4b — Uploaded Presentations | 51 | 43 |
| 5 — Reliability | 19 | 19 |
| 6 — Capacity & Packaging | 17 | 11 |
| 7 — Classroom Upgrade | 6 | 0 |
| Acceptance Checklist | 22 | 0 |
| **Total** | **266** | **215** |

The previous summary undercounted the actual checkbox list. This table now counts
every checkbox. **51 remain open**, including all 22 device acceptance items,
six conditional classroom/SFU tasks, and Office-conversion hardening/validation.
