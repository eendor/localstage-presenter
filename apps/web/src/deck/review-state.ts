import type { DeckState } from '@localstage/protocol';
export function acceptDeckState(current: DeckState | null, next: DeckState): boolean {
  return !current || next.deckRevision > current.deckRevision || (next.deckId === current.deckId && next.deckRevision === current.deckRevision && next.stateSequence > current.stateSequence);
}
export function clampReview(review: number | null, state: DeckState): number | null {
  return review === null || review >= state.presenterSlide || review < 1 ? null : review;
}
