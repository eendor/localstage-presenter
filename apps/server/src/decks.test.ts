import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, readFile, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { convertDeck } from './deck-converter.js';
import { DeckStore } from './decks.js';
import { HostAuth } from './auth.js';
import { setupSlideAccess } from './slide-access.js';
import express from 'express';
import { createServer } from 'node:http';

function pdf() {
  const objects = ['<< /Type /Catalog /Pages 2 0 R >>', '<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>', '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 640 360] /Contents 5 0 R >>', '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 640 360] /Contents 5 0 R >>', '<< /Length 29 >>\nstream\n1 0 0 rg 0 0 640 360 re f\nendstream'];
  let result = '%PDF-1.4\n'; const offsets = [0];
  objects.forEach((object, i) => { offsets.push(Buffer.byteLength(result)); result += `${i + 1} 0 obj\n${object}\nendobj\n`; });
  const xref = Buffer.byteLength(result);
  result += `xref\n0 ${objects.length + 1}\n0000000000 65535 f \n${offsets.slice(1).map(offset => `${String(offset).padStart(10, '0')} 00000 n \n`).join('')}trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF`;
  return Buffer.from(result);
}

test('PDF upload renders locally and slide access follows authoritative bounds', async () => {
  const auth = new HostAuth(); const token = auth.authenticateHost(auth.getBootstrapSecret())!;
  const decks = new DeckStore(); const app = express(); decks.setup(app, auth);
  setupSlideAccess(app, decks, auth, value => value === 'viewer-token');
  const server = createServer(app); await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${(server.address() as { port: number }).port}`;
  try {
    const response = await fetch(`${base}/api/decks?format=pdf`, { method: 'POST', headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/octet-stream' }, body: pdf() });
    const data = await response.json() as { id: string; totalSlides: number; error?: string };
    assert.equal(response.status, 200, data.error); assert.equal(data.totalSlides, 2);
    const state = decks.activate(data.id)!;
    const get = (slide: string, credential = 'viewer-token', revision = state.deckRevision) => fetch(`${base}/api/decks/${data.id}/slides/${slide}?revision=${revision}`, { headers: { Authorization: `Bearer ${credential}` } });
    assert.equal((await get('1')).status, 200);
    assert.equal((await get('2')).status, 403);
    assert.equal((await get('2', token)).status, 200);
    assert.equal((await get('1', 'invalid')).status, 403);
    assert.equal((await get('0')).status, 404);
    assert.equal(decks.navigate(data.id, state.deckRevision, 0, 2), true);
    assert.equal((await get('2')).status, 200);
    assert.equal(decks.navigate(data.id, state.deckRevision, 0, 1), false);
    assert.equal(decks.navigate(data.id, state.deckRevision, 1, 1), true);
    assert.equal((await get('2')).status, 403);
    assert.equal((await get('1', 'viewer-token', 999)).status, 403);
    await decks.clear(); assert.equal((await get('1')).status, 403);
  } finally { await decks.clear(); server.closeAllConnections(); await new Promise<void>(resolve => server.close(() => resolve())); }
});

test('renderer enforces page limit and rejects invalid PDF', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'localstage-test-'));
  try {
    await writeFile(path.join(directory, 'original.pdf'), pdf());
    await assert.rejects(convertDeck(directory, 'pdf', 1), /exceeds/);
    await writeFile(path.join(directory, 'original.pdf'), 'invalid');
    await assert.rejects(convertDeck(directory, 'pdf', 5), /PDF|Invalid/);
  } finally { await rm(directory, { recursive: true, force: true }); }
});
