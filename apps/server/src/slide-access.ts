import type { Express } from 'express';
import path from 'node:path';
import { DeckStore } from './decks.js';
import { HostAuth } from './auth.js';
import { isLoopback } from './http.js';

export function setupSlideAccess(app: Express, decks: DeckStore, auth: HostAuth, viewerAuthorized: (token: string) => boolean) {
  app.get('/api/decks/:id/slides/:slide', (req, res) => {
    const token = (req.headers.authorization || '').replace(/^Bearer /, '');
    const host = isLoopback(req.socket.remoteAddress) && auth.validateHostToken(token);
    const deck = decks.get(String(req.params.id)); const slide = Number(req.params.slide);
    const state = decks.state;
    res.setHeader('Cache-Control', 'no-store');
    if (!host && (!viewerAuthorized(token) || !state || state.deckId !== req.params.id || Number(req.query.revision) !== state.deckRevision || slide > state.presenterSlide)) { res.sendStatus(403); return; }
    if (!deck || !Number.isInteger(slide) || slide < 1 || slide > deck.totalSlides) { res.sendStatus(404); return; }
    res.sendFile(path.join(deck.directory, `${slide}.png`));
  });
}
