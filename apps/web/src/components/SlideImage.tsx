import { useEffect, useState } from 'react';
export function SlideImage({ deckId, revision, slide, token }: { deckId: string; revision: number; slide: number; token: string }) {
  const [image, setImage] = useState<{ key: string; url: string } | null>(null);
  const [error, setError] = useState('');
  const key = `${deckId}/${revision}/${slide}/${token}`;
  useEffect(() => {
    const controller = new AbortController(); let url: string | undefined;
    setError('');
    fetch(`/api/decks/${encodeURIComponent(deckId)}/slides/${slide}?revision=${revision}`, { headers: { Authorization: `Bearer ${token}` }, signal: controller.signal, cache: 'no-store' })
      .then(async r => { if (!r.ok) throw new Error('Slide unavailable or access changed'); return r.blob(); })
      .then(blob => { if (!controller.signal.aborted) { url = URL.createObjectURL(blob); setImage({ key, url }); } })
      .catch(e => { if (!controller.signal.aborted) setError(e.message); });
    return () => { controller.abort(); if (url) URL.revokeObjectURL(url); };
  }, [key]);
  return image?.key === key ? <img className="slide" src={image.url} alt={`Slide ${slide}`} /> : <p role="status">{error || 'Loading slide…'}</p>;
}
