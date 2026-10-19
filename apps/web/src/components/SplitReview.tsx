import { useEffect, useState, type ReactNode } from 'react';
import type { DeckState } from '@localstage/protocol';
import { clampReview } from '../deck/review-state';
import { SlideImage } from './SlideImage';
export function SplitReview({ state, token, live }: { state: DeckState; token: string; live?: ReactNode }) {
  const [selected, setSelected] = useState<{ revision: number; slide: number | null }>({ revision: state.deckRevision, slide: null });
  const review = selected.revision === state.deckRevision ? clampReview(selected.slide, state) : null;
  useEffect(() => { if (review === null && selected.slide !== null) setSelected({ revision: state.deckRevision, slide: null }); }, [state.deckRevision, state.presenterSlide]);
  return <section>
    <div className={review === null ? '' : 'split-review'}>
      <section><h2>Presenter — Live · Slide {state.presenterSlide}</h2>{live || <SlideImage deckId={state.deckId} revision={state.deckRevision} slide={state.presenterSlide} token={token} />}</section>
      {review !== null && <section><h2>Your Review · Slide {review}</h2><SlideImage deckId={state.deckId} revision={state.deckRevision} slide={review} token={token} />
        <div className="toolbar"><button className="btn btn-secondary" disabled={review <= 1} onClick={() => setSelected({ revision: state.deckRevision, slide: review - 1 })}>Previous review slide</button>
        <button className="btn btn-secondary" onClick={() => setSelected({ revision: state.deckRevision, slide: review + 1 })}>Next review slide</button>
        <button className="btn btn-primary" onClick={() => setSelected({ revision: state.deckRevision, slide: null })}>Back to Live</button></div>
      </section>}
    </div>
    <label className="label">Review an available slide<select className="input" value={review || state.presenterSlide} onChange={e => setSelected({ revision: state.deckRevision, slide: Number(e.target.value) })}>
      {Array.from({ length: state.presenterSlide }, (_, i) => <option key={i} value={i + 1}>Slide {i + 1}{i + 1 === state.presenterSlide ? ' — Live' : ''}</option>)}
    </select></label><p>{state.totalSlides - state.presenterSlide} future slides locked</p>
  </section>;
}
