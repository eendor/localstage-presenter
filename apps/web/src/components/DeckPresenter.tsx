import { useEffect, useState } from 'react';
import type { ClientMessage, DeckState } from '@localstage/protocol';
import { SlideImage } from './SlideImage';
export interface UploadedDeck { id: string; totalSlides: number }
const MAX_UPLOAD_BYTES = 150 * 1024 * 1024;
export function DeckPresenter({ token, state, uploaded, onUploaded, send }: { token: string; state: DeckState | null; uploaded: UploadedDeck | null; onUploaded: (deck: UploadedDeck) => void; send?: (message: ClientMessage) => boolean }) {
  const [busy, setBusy] = useState(false); const [error, setError] = useState(''); const [preview, setPreview] = useState(1);
  const navigate = (slide: number) => {
    if (state && send && slide >= 1 && slide <= state.totalSlides && slide !== state.presenterSlide) send({ type: 'presenter:navigate', deckId: state.deckId, deckRevision: state.deckRevision, stateSequence: state.stateSequence, targetSlide: slide });
    else if (!state && uploaded && slide >= 1 && slide <= uploaded.totalSlides) setPreview(slide);
  };
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement).closest('input, textarea, select, [contenteditable="true"], .presenter-script, .script-dialog')) return;
      if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') { e.preventDefault(); navigate((state?.presenterSlide || preview) + (e.key === 'ArrowRight' ? 1 : -1)); }
    };
    window.addEventListener('keydown', key); return () => window.removeEventListener('keydown', key);
  }, [state, preview, uploaded, send]);
  const deck = state ? { id: state.deckId, totalSlides: state.totalSlides } : uploaded;
  const slide = state?.presenterSlide || preview;
  return <section className="card join-info"><h2>Presentation deck</h2>
    <label className="label">Upload PDF, PPTX or PPT (up to 150 MB)<input className="input" type="file" accept=".pdf,.pptx,.ppt" disabled={busy} onChange={async e => {
      const file = e.target.files?.[0]; if (!file) return;
      if (file.size > MAX_UPLOAD_BYTES) { setError('File is too large. The maximum upload size is 150 MB.'); e.target.value = ''; return; }
      setBusy(true); setError('');
      try {
        const response = await fetch(`/api/decks?format=${file.name.split('.').pop()?.toLowerCase()}`, { method: 'POST', headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/octet-stream' }, body: file });
        const data = await response.json(); if (!response.ok) throw new Error(data.error || 'Upload failed'); onUploaded(data); setPreview(1);
      } catch (error) { setError(String(error)); } finally { setBusy(false); }
    }} /></label>
    {busy && <p role="status">Uploading and converting locally… This may take up to two minutes.</p>}{error && <p role="alert" className="error">{error}</p>}
    {uploaded && send && uploaded.id !== state?.deckId && <button className="btn btn-primary" onClick={() => send({ type: 'deck:activate', deckId: uploaded.id, deckRevision: 1, totalSlides: uploaded.totalSlides })}>Present uploaded deck</button>}
    {deck && <><SlideImage deckId={deck.id} revision={state?.deckRevision || 0} slide={slide} token={token} /><div className="toolbar">
      <button className="btn btn-secondary" disabled={slide <= 1} onClick={() => navigate(slide - 1)}>Previous</button><span>Slide {slide} / {deck.totalSlides}</span><button className="btn btn-secondary" disabled={slide >= deck.totalSlides} onClick={() => navigate(slide + 1)}>Next</button>
    </div><div className="thumbnails">{Array.from({ length: deck.totalSlides }, (_, i) => <button aria-label={`Go to slide ${i + 1}`} className="btn btn-secondary" key={i} onClick={() => navigate(i + 1)}><SlideImage deckId={deck.id} revision={state?.deckRevision || 0} slide={i + 1} token={token} /><span>{i + 1}</span></button>)}</div></>}
  </section>;
}
