import { useEffect, useState } from 'react';
import { ViewerPage } from './ViewerPage';

export function JoinPage() {
  const [session, setSession] = useState<{ id: string; title: string; hasPin: boolean } | null>(null);
  const [name, setName] = useState('');
  const [pin, setPin] = useState('');
  const [error, setError] = useState('');
  const [watching, setWatching] = useState(false);
  useEffect(() => {
    const controller = new AbortController();
    fetch('/api/session', { signal: controller.signal }).then(async r => { const data = await r.json(); if (!r.ok) throw new Error(data.error); setSession(data); }).catch(e => { if (e.name !== 'AbortError') setError(e.message); });
    return () => controller.abort();
  }, []);
  if (watching && session) return <ViewerPage admission={{ sessionId: session.id, displayName: name, pin }} title={session.title} onLeave={() => { setWatching(false); setPin(''); }} />;
  return <main className="container page setup"><form className="card" onSubmit={e => { e.preventDefault(); setWatching(true); }}>
    <h1>LocalStage</h1><h2>{session?.title || 'Join presentation'}</h2>
    <label className="label">Your name (optional)<input className="input" maxLength={64} value={name} onChange={e => setName(e.target.value)} /></label>
    {session?.hasPin && <label className="label">Six-digit PIN<input className="input" inputMode="numeric" maxLength={6} value={pin} onChange={e => setPin(e.target.value.replace(/\D/g, ''))} /></label>}
    {error && <p role="alert" className="error">{error}</p>}
    <button className="btn btn-primary" disabled={!session}>Watch presentation</button>
    <p>Use the same network as the presenter. Viewing requires no capture, camera or microphone permission.</p>
  </form></main>;
}
