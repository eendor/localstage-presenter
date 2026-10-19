import { useEffect, useRef, useState } from 'react';
import { SignalingClient, signalingUrl } from '../net/signaling-client';
import { ViewerPeer } from '../media/viewer-peer';
import type { DeckState } from '@localstage/protocol';
import { SplitReview } from '../components/SplitReview';
import { acceptDeckState } from '../deck/review-state';

export function ViewerPage({ admission, title, onLeave }: { admission: { sessionId: string; displayName: string; pin: string }; title: string; onLeave: () => void }) {
  const video = useRef<HTMLVideoElement>(null);
  const incoming = useRef<MediaStream | null>(null);
  const root = useRef<HTMLDivElement>(null);
  const client = useRef<SignalingClient | null>(null);
  const [state, setState] = useState('connecting');
  const [status, setStatus] = useState('waiting');
  const [error, setError] = useState('');
  const [muted, setMuted] = useState(true);
  const [tap, setTap] = useState(false);
  const [expanded, setExpanded] = useState(false);
  const [snapshot, setSnapshot] = useState<string | null>(null);
  const [deck, setDeck] = useState<DeckState | null>(null);
  const [membership, setMembership] = useState('');
  const [deckOnly, setDeckOnly] = useState(false);
  const snapshotReady = useRef<(() => void) | undefined>(undefined);
  const resumeCleanup = useRef<(() => void) | undefined>(undefined);
  useEffect(() => {
    if (!window.RTCPeerConnection) { setError('This browser does not support WebRTC. Use a current Chrome, Edge or Safari browser.'); return; }
    let peer: ViewerPeer | undefined;
    let token: string | undefined;
    let queue = Promise.resolve();
    const ws = new SignalingClient({ url: signalingUrl(), onStateChange: next => {
      setState(next);
      if (next === 'connected') ws.send({ type: 'viewer:join', ...admission, ...(token ? { token, pin: undefined } : {}) });
      else { peer?.close(); resumeCleanup.current?.(); incoming.current = null; if (video.current) video.current.srcObject = null; }
    }, onMessage: m => {
      if (m.type === 'viewer:accepted') {
        token = m.token; setMembership(m.token); peer?.close(); peer = new ViewerPeer(m.viewerId, message => ws.send(message), stream => {
          incoming.current = stream;
          if (video.current) { video.current.srcObject = stream; void video.current.play().catch(() => setTap(true)); }
        }, setState, (url, ready) => { setSnapshot(url); snapshotReady.current = ready; }, done => {
          resumeCleanup.current?.();
          const element = video.current;
          if (!element) return;
          const baseline = element.getVideoPlaybackQuality?.().totalVideoFrames || 0;
          const start = performance.now();
          const timer = setInterval(() => {
            const frames = element.getVideoPlaybackQuality?.().totalVideoFrames || 0;
            if (frames > baseline) { clearInterval(timer); done(); }
            else if (performance.now() - start > 10000) { clearInterval(timer); setError('Video is recovering. Leave and rejoin if motion does not resume.'); }
          }, 100);
          resumeCleanup.current = () => clearInterval(timer);
        });
      } else if (m.type === 'session:state') {
        setDeckOnly(m.session.source === 'deck');
        setStatus(m.status);
        if (m.status === 'ended') { setDeck(null); peer?.close(); ws.disconnect(); if (video.current) video.current.srcObject = null; }
      } else if (m.type === 'deck:state') setDeck(current => acceptDeckState(current, m.state) ? m.state : current);
      else if (m.type === 'error') { setDeck(null); setError(m.message); peer?.close(); ws.disconnect(); if (video.current) video.current.srcObject = null; }
      else queue = queue.then(() => peer?.handle(m)).catch(e => setError(String(e)));
    } });
    client.current = ws; ws.connect();
    return () => { ws.send({ type: 'viewer:leave' }); ws.disconnect(); peer?.close(); resumeCleanup.current?.(); if (video.current) video.current.srcObject = null; };
  }, [admission.sessionId]);
  const live = <div className="video-container"><video ref={element => { video.current = element; if (element && element.srcObject !== incoming.current) { element.srcObject = incoming.current; void element.play().catch(() => setTap(true)); } }} autoPlay playsInline muted={muted} />
      {snapshot && <img className="snapshot" src={snapshot} alt="Current presentation still" onLoad={() => { const ready = snapshotReady.current; requestAnimationFrame(() => requestAnimationFrame(() => ready?.())); }} />}
      {(status !== 'live' || state !== 'connected') && <div className="overlay"><h2>{status === 'ended' ? 'Presentation ended' : status === 'paused' ? 'Presentation paused' : state === 'failed' ? 'Video failed — leave and rejoin' : 'Waiting for presenter / reconnecting…'}</h2></div>}
      {tap && <button className="btn btn-primary play" onClick={() => video.current?.play().then(() => setTap(false)).catch(() => setError('Playback blocked. Try muting audio.'))}>Tap to play</button>}
    </div>;
  return <div ref={root} className={`viewer ${expanded ? 'expanded' : ''}`}>
    <header className="toolbar"><h1>{title}</h1><span>{status} · {state}</span><button className="btn btn-secondary" onClick={onLeave}>Leave</button></header>
    {error && <p role="alert" className="error">{error}</p>}
    {deck ? <SplitReview key={`${deck.deckId}-${deck.deckRevision}`} state={deck} token={membership} live={deckOnly ? undefined : live} /> : live}
    <footer className="toolbar"><button className="btn btn-secondary" onClick={() => { setMuted(!muted); void video.current?.play().catch(() => setTap(true)); }}>{muted ? 'Enable audio' : 'Mute audio'}</button>
      <span>{snapshot ? 'Still image (not paused)' : 'Live video'}</span><button className="btn btn-secondary" onClick={async () => {
        try { if (document.fullscreenElement) await document.exitFullscreen(); else if (root.current?.requestFullscreen) await root.current.requestFullscreen(); else setExpanded(!expanded); }
        catch { setExpanded(!expanded); }
      }}>{expanded ? 'Fit in page' : 'Fullscreen'}</button></footer>
  </div>;
}
