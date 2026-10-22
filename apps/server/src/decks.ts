import express, { type Express, type Request } from 'express';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import type { DeckState } from '@localstage/protocol';
import { HostAuth } from './auth.js';
import { isLoopback } from './http.js';
import { convertDeck } from './deck-converter.js';

export interface Deck { id: string; directory: string; totalSlides: number }
export class DeckStore {
  private decks = new Map<string, Deck>();
  private revision = 0;
  state: DeckState | null = null;
  private uploading = false;
  private epoch = 0;
  private maxPages = Number(process.env.MAX_DECK_PAGES || 200);
  constructor() { if (!Number.isInteger(this.maxPages) || this.maxPages < 1 || this.maxPages > 1000) throw new Error('MAX_DECK_PAGES must be between 1 and 1000'); }
  get(id: string) { return this.decks.get(id); }
  activate(id: string) {
    const deck = this.decks.get(id); if (!deck) return null;
    this.state = { deckId: id, deckRevision: ++this.revision, totalSlides: deck.totalSlides, presenterSlide: 1, stateSequence: 0 };
    return this.state;
  }
  navigate(id: string, revision: number, sequence: number, slide: number) {
    const state = this.state;
    if (!state || state.deckId !== id || state.deckRevision !== revision || state.stateSequence !== sequence || !Number.isInteger(slide) || slide < 1 || slide > state.totalSlides || slide === state.presenterSlide) return false;
    this.state = { ...state, presenterSlide: slide, stateSequence: state.stateSequence + 1 }; return true;
  }
  async clear() {
    ++this.epoch;
    this.state = null;
    const decks = [...this.decks.values()]; this.decks.clear();
    await Promise.all(decks.map(d => rm(d.directory, { recursive: true, force: true })));
  }
  setup(app: Express, auth: HostAuth) {
    const authorize = (req: Request) => isLoopback(req.socket.remoteAddress) && auth.validateHostToken((req.headers.authorization || '').replace(/^Bearer /, ''));
    const maxUpload = process.env.MAX_DECK_BYTES || '150mb';
    const capacity = /^(\d+)(kb|mb|gb)$/i.exec(maxUpload);
    const maxUploadLabel = capacity ? `${capacity[1]} ${capacity[2].toUpperCase()}` : maxUpload;
    const rawUpload = express.raw({ type: 'application/octet-stream', limit: maxUpload });
    app.post('/api/decks', (req, res, next) => { if (!authorize(req)) { res.sendStatus(403); return; } next(); }, (req, res, next) => rawUpload(req, res, error => {
      if (error) { res.status(413).json({ error: `Upload too large. Maximum deck size is ${maxUploadLabel}.` }); return; }
      next();
    }), async (req, res) => {
      if (this.uploading) { res.status(409).json({ error: 'A conversion is already running' }); return; }
      if (this.decks.size >= 4) { res.status(409).json({ error: 'Four decks loaded. End the session before uploading more.' }); return; }
      const extension = String(req.query.format).toLowerCase();
      if (!['pdf', 'ppt', 'pptx'].includes(extension) || !Buffer.isBuffer(req.body) || req.body.length === 0) { res.status(400).json({ error: 'Upload a PDF, PPTX or PPT file' }); return; }
      if (extension === 'pdf' && req.body.subarray(0, 5).toString() !== '%PDF-') { res.status(400).json({ error: 'Invalid PDF file' }); return; }
      this.uploading = true;
      const epoch = this.epoch;
      let directory: string | undefined;
      try {
        directory = await mkdtemp(path.join(os.tmpdir(), 'localstage-deck-'));
        await writeFile(path.join(directory, `original.${extension}`), req.body);
        const totalSlides = await convertDeck(directory, extension, this.maxPages);
        if (epoch !== this.epoch) throw new Error('Session ended during conversion');
        const id = crypto.randomUUID(); this.decks.set(id, { id, directory, totalSlides });
        res.json({ id, totalSlides });
      } catch (error) {
        if (directory) await rm(directory, { recursive: true, force: true });
        res.status(400).json({ error: `Conversion failed: ${error instanceof Error ? error.message : String(error)}` });
      } finally { this.uploading = false; }
    });
  }
}
