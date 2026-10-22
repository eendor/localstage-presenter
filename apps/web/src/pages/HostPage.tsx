import { useEffect, useRef, useState } from 'react';
import QRCode from 'qrcode';
import { QUALITY_PRESETS, type QualityPreset, type Session, type Viewer, type DeckState } from '@localstage/protocol';
import { DeckPresenter, type UploadedDeck } from '../components/DeckPresenter';
import { acceptDeckState } from '../deck/review-state';
import { SignalingClient, signalingUrl } from '../net/signaling-client';
import { capture, createSlate, stopCapture } from '../media/capture';
import { HostPeers } from '../media/host-peers';
import { StatsSampler } from '../media/stats';
import { PresenterScript } from '../components/PresenterScript';

export function HostPage() {
  const [title, setTitle] = useState('My Presentation');
  const [pin, setPin] = useState('');
  const [session, setSession] = useState<Session | null>(null);
  const [viewers, setViewers] = useState<Viewer[]>([]);
  const [adapters, setAdapters] = useState<{ name: string; address: string }[]>([]);
  const [address, setAddress] = useState('');
  const [serverPort, setServerPort] = useState(location.port || '3000');
  const [qr, setQr] = useState('');
  const [error, setError] = useState('');
  const [connection, setConnection] = useState('disconnected');
  const [busy, setBusy] = useState(false);
  const [quality, setQuality] = useState<QualityPreset>('balanced');
  const [audio, setAudio] = useState(false);
  const [hasAudio, setHasAudio] = useState(false);
  const [stats, setStats] = useState('');
  const [alwaysLive, setAlwaysLive] = useState(false);
  const [stillCount, setStillCount] = useState(0);
  const [sourceChoice, setSourceChoice] = useState<'screen' | 'deck'>('screen');
  const [maxViewers, setMaxViewers] = useState('5');
  const [uploaded, setUploaded] = useState<UploadedDeck | null>(null);
  const [deckState, setDeckState] = useState<DeckState | null>(null);
  const alwaysLiveRef = useRef(false);
  const preview = useRef<HTMLVideoElement>(null);
  const source = useRef<MediaStream | null>(null);
  const outgoing = useRef<MediaStream | null>(null);
  const slate = useRef<MediaStream | null>(null);
  const peers = useRef<HostPeers | null>(null);
  const client = useRef<SignalingClient | null>(null);
  const token = useRef(sessionStorage.getItem('hostToken') || '');
  const paused = useRef(false);
  const preset = useRef(quality);
  const viewerLimit = Math.max(1, Math.min(50, Number(maxViewers) || 1));
  /** Worst-case presenter upload: each viewer receives a separate WebRTC stream. */
  function estimatedUploadMbps(count: number) {
    const audioKbps = hasAudio && audio ? 96 : 0;
    return Math.round(count * (QUALITY_PRESETS[quality].targetBitrateKbps + audioKbps) * 11 / 1000) / 10;
  }
  const url = address ? `http://${address}:${serverPort}/join` : '';

  function cleanup() {
    const ws = client.current; client.current = null; ws?.disconnect();
    peers.current?.close(); peers.current = null;
    stopCapture(source.current); stopCapture(slate.current);
    source.current = null; outgoing.current = null; slate.current = null;
    paused.current = false;
    if (preview.current) preview.current.srcObject = null;
  }
  function end() { client.current?.send({ type: 'host:end' }); cleanup(); setSession(null); setViewers([]); setBusy(false); setUploaded(null); setDeckState(null); }
  function show(stream: MediaStream) { outgoing.current = stream; if (preview.current) preview.current.srcObject = stream; }
  function watch(stream: MediaStream) {
    stream.getVideoTracks()[0].onended = () => { if (source.current === stream) end(); };
    setHasAudio(stream.getAudioTracks().length > 0); setAudio(stream.getAudioTracks().length > 0);
  }
  useEffect(() => {
    let disposed = false;
    const secret = location.hash.slice(1);
    if (secret) {
      history.replaceState(null, '', location.pathname);
      fetch('/api/host/auth', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ secret }) })
        .then(async r => { const data = await r.json(); if (!r.ok) throw new Error(data.error); token.current = data.token; sessionStorage.setItem('hostToken', data.token); })
        .catch(e => setError(e.message));
    }
    const refresh = () => fetch('/api/network/interfaces').then(r => r.json()).then(data => {
      if (disposed) return;
      setAdapters(data.interfaces);
      setAddress(data.selectedAddress || ''); setServerPort(String(data.port || location.port || 3000));
    }).catch(() => { if (!disposed) setError('Local server unavailable. Start the server and reload.'); });
    void refresh(); const timer = setInterval(refresh, 10000);
    return () => { disposed = true; clearInterval(timer); cleanup(); };
  }, []);
  useEffect(() => { let active = true; setQr(''); if (url) QRCode.toDataURL(url).then(image => { if (active) setQr(image); }).catch(e => setError(e.message)); return () => { active = false; }; }, [url]);
  useEffect(() => { if (preview.current) preview.current.srcObject = outgoing.current; }, [session?.id]);
  useEffect(() => {
    const sampler = new StatsSampler();
    const timer = setInterval(async () => {
      const peer = peers.current?.peers.values().next().value;
      if (!peer) { setStats('Waiting for viewer media statistics'); return; }
      try { const s = await sampler.sample(peer.pc); setStats(`${s.width || '—'} × ${s.height || '—'} · ${s.fps || 0} fps · ${s.kbps || 0} kbps · lost ${s.lost || 0} · RTT ${s.rtt ?? '—'} s`); } catch { /* Peer closed during sampling. */ }
    }, 2000);
    return () => clearInterval(timer);
  }, []);

  async function start() {
    setError('');
    if (sourceChoice === 'screen' && !window.RTCPeerConnection) { setError('This browser does not support WebRTC screen streaming. Use desktop Chrome or Edge.'); return; }
    if (!token.current) { setError('Open the one-use presenter link printed by the server.'); return; }
    if (pin && !/^\d{6}$/.test(pin)) { setError('PIN must contain exactly six digits.'); return; }
    if (sourceChoice === 'deck' && !uploaded) { setError('Upload and preview a deck before starting.'); return; }
    const limit = Number(maxViewers);
    if (!Number.isInteger(limit) || limit < 1 || limit > 50) { setError('Maximum viewers must be a whole number between 1 and 50.'); return; }
    setBusy(true);
    try {
      if (sourceChoice === 'screen') { const stream = await capture(quality); source.current = stream; show(stream); watch(stream); }
      const ws = new SignalingClient({ url: signalingUrl(), maxReconnectAttempts: 0,
        onStateChange: state => {
          setConnection(state);
          if (state === 'connected') ws.send({ type: 'host:create', token: token.current, title, pin: pin || undefined, maxViewers: viewerLimit, source: sourceChoice });
          if (state === 'disconnected' && client.current) { cleanup(); setSession(null); setBusy(false); setError('Session stopped. Start again to approve a new capture.'); }
        },
        onMessage: m => {
          if (m.type === 'host:created') { setSession(m.session); setBusy(false); peers.current = new HostPeers(m.session.id, message => ws.send(message), setError, setStillCount); if (source.current) void peers.current.still.configure(source.current, !alwaysLiveRef.current, false); if (uploaded) ws.send({ type: 'deck:activate', deckId: uploaded.id, deckRevision: 1, totalSlides: uploaded.totalSlides }); }
          else if (m.type === 'deck:state') setDeckState(current => acceptDeckState(current, m.state) ? m.state : current);
          else if (m.type === 'session:state') { setSession(m.session); setViewers(m.viewers || []); if (m.status === 'ended') end(); }
          else if (m.type === 'viewer:ready' && outgoing.current) void peers.current?.add(m.viewerId, outgoing.current).then(() => peers.current?.quality(preset.current)).catch(e => setError(e.message));
          else if (m.type === 'viewer:left') peers.current?.remove(m.viewerId);
          else if (m.type === 'error') { setError(m.message); if (!peers.current) { cleanup(); setBusy(false); } }
          else void peers.current?.handle(m).catch(e => setError(e.message));
        },
      });
      client.current = ws; ws.connect();
    } catch (e) { cleanup(); setBusy(false); setError(e instanceof Error ? e.message : 'Capture cancelled. Try again.'); }
  }
  async function togglePause() {
    if (!source.current) return;
    setBusy(true);
    try {
      const next = !paused.current;
      if (!next && source.current.getVideoTracks()[0]?.readyState !== 'live') throw new Error('Capture ended. Start a new session.');
      if (next) {
        slate.current ||= createSlate();
        source.current.getTracks().forEach(t => { t.enabled = false; });
      } else source.current.getTracks().forEach(t => { t.enabled = t.kind === 'video' || audio; });
      const stream = next ? slate.current! : source.current;
      await peers.current?.still.configure(source.current, !alwaysLiveRef.current, next);
      await peers.current?.replace(stream); paused.current = next; show(stream);
      client.current?.send({ type: 'host:status', status: next ? 'paused' : 'live' });
    } catch (e) { end(); setError(`Media update failed; session stopped: ${String(e)}`); }
    finally { setBusy(false); }
  }
  async function changeSource() {
    setBusy(true); setError('');
    let stream: MediaStream | undefined;
    try {
      stream = await capture(quality);
      if (paused.current) stream.getTracks().forEach(t => { t.enabled = false; });
      await peers.current?.still.configure(stream, !alwaysLiveRef.current, paused.current);
      if (!paused.current) await peers.current?.replace(stream);
      stopCapture(source.current); source.current = stream; watch(stream);
      if (!paused.current) show(stream);
    } catch (e) { if (stream) { stopCapture(stream); end(); setError(`Source replacement failed; session stopped: ${String(e)}`); } else setError(`Source unchanged: ${String(e)}`); }
    finally { setBusy(false); }
  }
  async function changeQuality(value: QualityPreset) {
    setQuality(value); preset.current = value;
    const p = QUALITY_PRESETS[value];
    try { await source.current?.getVideoTracks()[0]?.applyConstraints({ width: { ideal: p.width }, height: { ideal: p.height }, frameRate: { max: p.maxFps } }); await peers.current?.quality(value); }
    catch (e) { setError(`Quality setting unavailable: ${String(e)}`); }
  }
  return <main className="container page">
    <header className="toolbar"><h1>LocalStage</h1><span>Server: {connection}</span></header>
    {error && <p role="alert" className="error">{error}</p>}
    {!session ? <section className="card setup">
      <h2>Start a presentation</h2>
      <label className="label">Presentation title<input className="input" maxLength={128} value={title} onChange={e => setTitle(e.target.value)} /></label>
      <label className="label">Optional six-digit PIN<input className="input" inputMode="numeric" maxLength={6} value={pin} onChange={e => setPin(e.target.value.replace(/\D/g, ''))} /></label>
      <label className="label">Source<select className="input" value={sourceChoice} onChange={e => setSourceChoice(e.target.value as 'screen' | 'deck')}><option value="screen">Share screen</option><option value="deck">Upload presentation</option></select></label>
      <label className="label">Maximum viewers (1–50)<input className="input" type="number" inputMode="numeric" min={1} max={50} value={maxViewers} onChange={e => setMaxViewers(e.target.value.replace(/\D/g, '').slice(0, 2))} onBlur={() => setMaxViewers(String(viewerLimit))} /></label>
      <p className="capacity">{sourceChoice === 'deck'
        ? `Uploaded decks are served as slide images rather than continuous video, so ${viewerLimit} viewers is practical with a normal Wi-Fi access point.`
        : `Live screen video is uploaded separately to every viewer — up to ~${estimatedUploadMbps(viewerLimit)} Mbps from the presenter at ${viewerLimit} viewers with ${QUALITY_PRESETS[quality].label}.`}</p>
      {viewerLimit > 10 && <p className="capacity capacity-danger">Above ~10 viewers a phone hotspot is not enough: most hotspots allow only 5–10 connected devices. Use a dedicated Wi-Fi 5/6 access point with client isolation disabled.{sourceChoice === 'screen' ? ' Connect the presenter by Ethernet if possible, and switch to Low Bandwidth or an uploaded deck if viewers report freezing.' : ' Uploaded decks are the lightest option, but each slide change still sends a full image to every viewer.'}</p>}
      <button className="btn btn-primary" disabled={busy} onClick={start}>{busy ? 'Starting…' : 'Start sharing'}</button>
    </section> : <>
      <header className="toolbar"><h2>{session.title}</h2><span>{session.status} · {viewers.length} / {session.maxViewers} viewers</span><button className="btn btn-danger" onClick={end}>End session</button></header>
      <div className="dashboard"><section hidden={session.source === 'deck'}>
        <div className="video-container"><video ref={preview} autoPlay playsInline muted /></div>
        <div className="toolbar">
          <button className="btn btn-secondary" disabled={busy} onClick={togglePause}>{session.status === 'paused' ? 'Resume' : 'Pause'}</button>
          <button className="btn btn-secondary" disabled={busy} onClick={changeSource}>Change source</button>
          <button className="btn btn-secondary" disabled={!hasAudio} onClick={() => { const enabled = !audio; setAudio(enabled); source.current?.getAudioTracks().forEach(t => { t.enabled = enabled && !paused.current; }); }}>{hasAudio ? audio ? 'Mute audio' : 'Enable audio' : 'No captured audio'}</button>
          <select aria-label="Quality" className="input" value={quality} onChange={e => changeQuality(e.target.value as QualityPreset)}>{Object.entries(QUALITY_PRESETS).map(([key, p]) => <option key={key} value={key}>{p.label}</option>)}</select>
        </div><p>{stats}</p>
        <label><input type="checkbox" checked={alwaysLive} onChange={e => { const value = e.target.checked; setAlwaysLive(value); alwaysLiveRef.current = value; if (source.current) void peers.current?.still.configure(source.current, !value, paused.current); }} /> Always live</label>
        <p>{stillCount ? `Still image — ${stillCount} viewer(s)` : 'Live video'} · Automatic still after 5 seconds without pixel changes. Hidden dashboards use live video.</p>
      </section><aside className="card"><h3>Viewers</h3>
        <button className="btn btn-secondary" onClick={() => client.current?.send({ type: 'host:lock', locked: !session.joinLocked })}>{session.joinLocked ? 'Unlock joining' : 'Lock joining'}</button>
        <button className="btn btn-secondary" style={{ marginTop: '.5rem', width: '100%' }} aria-pressed={session.reviewLocked} onClick={() => client.current?.send({ type: 'host:review-lock', locked: !session.reviewLocked })}>{session.reviewLocked ? 'Unlock previous slides' : 'Lock previous slides'}</button>
        <p style={{ fontSize: '.8rem' }}>{session.reviewLocked ? 'Viewers can only see your current slide.' : 'Viewers can review earlier slides independently.'}</p>
        <p style={{ fontSize: '.8rem' }}>{viewers.length} of {session.maxViewers} viewer slots used</p>
        {session.source !== 'deck' && session.maxViewers > 10 && <p className="capacity capacity-danger">Presenter upload can reach ~{estimatedUploadMbps(Math.max(viewers.length, session.maxViewers))} Mbps at the full limit. If viewers freeze, choose Low Bandwidth or switch to an uploaded deck.</p>}
        {viewers.map(v => <div className="toolbar" key={v.id}><span>{v.displayName} · {v.state}</span><button className="btn btn-secondary" onClick={() => client.current?.send({ type: 'host:remove-viewer', viewerId: v.id })}>Remove</button></div>)}
        {pin && <p>PIN: {pin}</p>}
      </aside></div>
    </>}
    <div className="presenter-workspace">
      <DeckPresenter token={token.current} state={deckState} uploaded={uploaded} onUploaded={setUploaded} send={session ? message => client.current?.send(message) || false : undefined} />
      <PresenterScript />
    </div>
    <section className="card join-info"><h2>Invite viewers</h2>
      <label className="label">Network adapter (changing it ends the session)<select className="input" value={address} onChange={async e => {
        const address = e.target.value;
        if (session) end();
        try { const r = await fetch('/api/network/select', { method: 'POST', headers: { Authorization: `Bearer ${token.current}`, 'Content-Type': 'application/json' }, body: JSON.stringify({ address }) }); const data = await r.json(); if (!r.ok) throw new Error(data.error); setAddress(data.selectedAddress || ''); }
        catch (error) { setError(String(error)); }
      }}>{adapters.map(a => <option key={`${a.name}-${a.address}`} value={a.address}>{a.name} — {a.address}</option>)}</select></label>
      {url ? <><a href={url}>{url}</a> <button className="btn btn-secondary" onClick={() => navigator.clipboard.writeText(url).catch(() => setError('Copy unavailable. Select and copy the address.'))}>Copy address</button>{qr && <img className="qr" src={qr} alt="QR code for viewer address" />}</> : <p>No LAN adapter found. Connect to Wi-Fi or a hotspot.</p>}
      <p>Connect every device to the same Wi-Fi or hotspot.</p>
      <details><summary>Connection help</summary><p>Page won't open: verify the selected adapter, address above, TCP port {location.port || 80}, firewall rule and hotspot client isolation.</p><p>Page opens but video won't connect: allow the browser's WebRTC UDP media traffic and disable Wi-Fi client isolation. No internet or external relay is used.</p></details>
    </section>
  </main>;
}
