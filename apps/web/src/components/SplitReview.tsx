import { useEffect, useRef, useState, type ReactNode } from 'react';
import type { DeckState } from '@localstage/protocol';
import { clampReview } from '../deck/review-state';
import { SlideImage } from './SlideImage';
import { createPortal } from 'react-dom';
export function SplitReview({ state, token, live, fullscreen = false, controlsTarget, onShowControls, reviewLocked = false }: { state: DeckState; token: string; live?: ReactNode; fullscreen?: boolean; controlsTarget?: HTMLElement | null; onShowControls?: () => void; reviewLocked?: boolean }) {
  const [selected, setSelected] = useState<{ revision: number; slide: number | null }>({ revision: state.deckRevision, slide: null });
  const review = !reviewLocked && selected.revision === state.deckRevision ? clampReview(selected.slide, state) : null;
  const pane = useRef<HTMLElement>(null);
  const bounds = useRef<HTMLDivElement>(null);
  const [position, setPosition] = useState<{ x: number; y: number } | null>(null);
  const [dragging, setDragging] = useState(false);
  const [liveClosed, setLiveClosed] = useState(false);
  const hideLive = liveClosed && review !== null;
  const drag = useRef<{ id: number; x: number; y: number; left: number; top: number } | null>(null);
  const floating = fullscreen && review !== null && !hideLive;
  useEffect(() => { if (review === null) setLiveClosed(false); }, [review]);
  const backToLive = () => { setLiveClosed(false); setSelected({ revision: state.deckRevision, slide: null }); };
  function constrain(x: number, y: number) {
    const area = bounds.current;
    const preview = pane.current;
    if (!area || !preview) return { x, y };
    return { x: Math.max(0, Math.min(x, area.clientWidth - preview.offsetWidth)), y: Math.max(0, Math.min(y, area.clientHeight - preview.offsetHeight)) };
  }
  useEffect(() => {
    drag.current = null; setDragging(false);
    if (!floating || !bounds.current || !pane.current) return;
    const observer = new ResizeObserver(() => setPosition(current => current ? constrain(current.x, current.y) : current));
    observer.observe(bounds.current); observer.observe(pane.current);
    return () => observer.disconnect();
  }, [floating]);
  useEffect(() => { if (review === null && selected.slide !== null) setSelected({ revision: state.deckRevision, slide: null }); }, [state.deckRevision, state.presenterSlide]);
  // When the presenter locks review, drop any open review immediately.
  useEffect(() => { if (reviewLocked) setSelected({ revision: state.deckRevision, slide: null }); }, [reviewLocked, state.deckRevision]);
  const displayedSlide = review ?? state.presenterSlide;
  const selectSlide = (slide: number) => { if (reviewLocked) return; setSelected({ revision: state.deckRevision, slide: clampReview(slide, state) }); };
  const touch = useRef<{ x: number; y: number } | null>(null);
  const suppressClick = useRef(false);
  useEffect(() => {
    if (!fullscreen) return;
    const navigate = (event: KeyboardEvent) => {
      if (event.defaultPrevented || event.altKey || event.ctrlKey || event.metaKey || (event.target as HTMLElement).closest('input, select, textarea, button, [contenteditable], .preview-drag-handle')) return;
      if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
      event.preventDefault(); selectSlide(Math.max(1, Math.min(state.presenterSlide, displayedSlide + (event.key === 'ArrowRight' ? 1 : -1))));
    };
    window.addEventListener('keydown', navigate); return () => window.removeEventListener('keydown', navigate);
  }, [fullscreen, displayedSlide, state.presenterSlide, state.deckRevision]);
  return <section className={`review-layout ${review === null ? 'following' : 'reviewing'} ${fullscreen ? 'fullscreen-review' : ''} ${hideLive ? 'live-preview-closed' : ''}`}
    onTouchStart={event => {
      suppressClick.current = false;
      if (!fullscreen || event.touches.length !== 1 || (event.target as HTMLElement).closest('button, select, input, nav, .preview-drag-handle, .reviewing .presenter-pane')) { touch.current = null; return; }
      touch.current = { x: event.touches[0].clientX, y: event.touches[0].clientY };
    }}
    onTouchCancel={() => { touch.current = null; }}
    onTouchEnd={event => {
      const start = touch.current; touch.current = null;
      if (!start || !event.changedTouches.length) return;
      const dx = event.changedTouches[0].clientX - start.x, dy = event.changedTouches[0].clientY - start.y;
      if (Math.abs(dx) >= 60 && Math.abs(dx) > Math.abs(dy) * 1.5) {
        suppressClick.current = true; selectSlide(Math.max(1, Math.min(state.presenterSlide, displayedSlide + (dx < 0 ? 1 : -1))));
      }
    }}
    onClickCapture={event => { if (suppressClick.current) { suppressClick.current = false; event.stopPropagation(); } }}>
    {fullscreen && <button className="slide-counter" onClick={onShowControls} aria-label={`Slide ${displayedSlide} of ${state.presenterSlide} available. Show controls.`}>{displayedSlide} / {state.presenterSlide}{review === null ? ' · Live' : ' · Review'}</button>}
    {fullscreen && !reviewLocked && <>
      <button className="slide-edge slide-edge-previous" disabled={displayedSlide <= 1} aria-label="Previous slide" title="Previous slide" onClick={event => { event.stopPropagation(); selectSlide(displayedSlide - 1); }}><span aria-hidden="true">‹</span></button>
      <button className="slide-edge slide-edge-next" disabled={displayedSlide >= state.presenterSlide} aria-label="Next slide" title={displayedSlide >= state.presenterSlide ? 'You are at the presenter’s current slide' : 'Next slide'} onClick={event => { event.stopPropagation(); selectSlide(displayedSlide + 1); }}><span aria-hidden="true">›</span></button>
    </>}
    {fullscreen && reviewLocked && <span className="review-locked-chip" role="status">🔒 Following presenter</span>}
    <div ref={bounds} className={review === null ? 'follow-pane' : 'split-review'}>
      <section ref={pane} hidden={hideLive} className={`presenter-pane ${dragging ? 'is-dragging' : ''}`} style={floating && position ? { left: position.x, top: position.y, bottom: 'auto' } : undefined}>
        {review !== null && <button className="close-live-preview" aria-label="Close presenter live preview" title="Close live preview" onClick={event => { event.stopPropagation(); setLiveClosed(true); }}>×</button>}
        <h2 className={floating ? 'preview-drag-handle' : ''} tabIndex={floating ? 0 : undefined} aria-label={floating ? 'Move live preview. Drag or use arrow keys.' : undefined}
          onPointerDown={event => {
            if (!floating || event.button !== 0 || !pane.current || !bounds.current) return;
            const rect = pane.current.getBoundingClientRect(), area = bounds.current.getBoundingClientRect();
            drag.current = { id: event.pointerId, x: event.clientX, y: event.clientY, left: rect.left - area.left, top: rect.top - area.top };
            event.currentTarget.setPointerCapture(event.pointerId); setDragging(true); event.preventDefault();
          }}
          onPointerMove={event => { const start = drag.current; if (start?.id === event.pointerId) setPosition(constrain(start.left + event.clientX - start.x, start.top + event.clientY - start.y)); }}
          onPointerUp={event => { if (drag.current?.id === event.pointerId) { drag.current = null; setDragging(false); if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId); } }}
          onPointerCancel={() => { drag.current = null; setDragging(false); }}
          onLostPointerCapture={() => { drag.current = null; setDragging(false); }}
          onKeyDown={event => {
            if (!floating || !['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key) || !pane.current || !bounds.current) return;
            event.preventDefault(); const rect = pane.current.getBoundingClientRect(), area = bounds.current.getBoundingClientRect();
            const step = event.shiftKey ? 40 : 10;
            setPosition(constrain(rect.left - area.left + (event.key === 'ArrowRight' ? step : event.key === 'ArrowLeft' ? -step : 0), rect.top - area.top + (event.key === 'ArrowDown' ? step : event.key === 'ArrowUp' ? -step : 0)));
          }}>
          {floating && <span aria-hidden="true">⠿ </span>}Presenter — Live · Slide {state.presenterSlide}
        </h2><div className="review-media">{live || <SlideImage deckId={state.deckId} revision={state.deckRevision} slide={state.presenterSlide} token={token} />}</div></section>
      {review !== null && <section className="personal-review-pane">{!fullscreen && <h2>Your Review · Slide {review}</h2>}<div className="review-media"><SlideImage deckId={state.deckId} revision={state.deckRevision} slide={review} token={token} /></div>
      </section>}
    </div>
    {hideLive && <button className="btn btn-primary restore-live" onClick={backToLive}>Back to Live</button>}
    {review !== null && <div className="toolbar inline-review-controls"><button className="btn btn-secondary" disabled={review <= 1} onClick={() => setSelected({ revision: state.deckRevision, slide: review - 1 })}>Previous review slide</button>
        <button className="btn btn-secondary" onClick={() => setSelected({ revision: state.deckRevision, slide: review + 1 })}>Next review slide</button>
        <button className="btn btn-primary" onClick={() => setSelected({ revision: state.deckRevision, slide: null })}>Back to Live</button></div>}
    {fullscreen && controlsTarget && !reviewLocked && createPortal(<nav className="fullscreen-slide-controls" aria-label="Slide review controls">
      <label className="fullscreen-slide-select"><span className="sr-only">Select an available slide</span><select className="input" value={displayedSlide} onChange={e => selectSlide(Number(e.target.value))}>
        {Array.from({ length: state.presenterSlide }, (_, i) => <option key={i} value={i + 1}>Slide {i + 1}{i + 1 === state.presenterSlide ? ' · Live' : ''}</option>)}
      </select></label>
      <button className="btn btn-primary" disabled={review === null} onClick={() => setSelected({ revision: state.deckRevision, slide: null })}>Back to Live</button>
    </nav>, controlsTarget)}
    {fullscreen && reviewLocked && controlsTarget && createPortal(<span className="review-locked-note">Presenter locked earlier slides — following live</span>, controlsTarget)}
    {!reviewLocked && <label className="label regular-slide-select">Review an available slide<select className="input" value={review || state.presenterSlide} onChange={e => setSelected({ revision: state.deckRevision, slide: Number(e.target.value) })}>
      {Array.from({ length: state.presenterSlide }, (_, i) => <option key={i} value={i + 1}>Slide {i + 1}{i + 1 === state.presenterSlide ? ' — Live' : ''}</option>)}
    </select></label>}
    <p>{reviewLocked ? 'Presenter locked earlier slides. You are following the live slide.' : `${state.totalSlides - state.presenterSlide} future slides locked`}</p>
  </section>;
}
