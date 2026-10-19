import { test } from 'node:test';
import assert from 'node:assert/strict';
import express from 'express';
import { createServer } from 'node:http';
import { WebSocket } from 'ws';
import { setupSignaling } from './signaling.js';
import { setupHttpRoutes } from './http.js';
import { SessionStore } from './session-store.js';
import { HostAuth, PinRateLimiter } from './auth.js';
import { validateClientMessage } from '@localstage/protocol';

test('reject malformed protocol payloads', () => {
  for (const value of [null, [], { type: 'host:lock', locked: 'false' }, { type: 'rtc:offer', connectionGeneration: -1 }, { type: 'host:create', title: 'x', maxViewers: 500, token: 'x' }]) assert.equal(validateClientMessage(value), false);
});

test('PIN limiter blocks the sixth attempt and isolates clients', () => {
  const limiter = new PinRateLimiter();
  for (let attempt = 0; attempt < 5; attempt++) assert.equal(limiter.checkLimit('client-a'), true);
  assert.equal(limiter.checkLimit('client-a'), false);
  assert.equal(limiter.checkLimit('client-b'), true);
});

test('authenticated admission, role isolation, relay, generations, removal and end', async () => {
  const store = new SessionStore(); const auth = new HostAuth();
  const app = express(); setupHttpRoutes(app, '.', store, auth);
  const server = createServer(app); const close = setupSignaling(server, store, auth);
  await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address() as { port: number };
  const origin = `http://127.0.0.1:${address.port}`;
  const sockets: WebSocket[] = [];
  async function connect() {
    const socket = new WebSocket(origin.replace('http:', 'ws:') + '/ws', { origin }); sockets.push(socket);
    const messages: any[] = [];
    socket.on('message', data => messages.push(JSON.parse(data.toString())));
    await new Promise<void>((resolve, reject) => { socket.once('open', resolve); socket.once('error', reject); });
    return {
      socket,
      send: (m: object) => socket.send(JSON.stringify(m)),
      next: async (type: string) => {
        const deadline = Date.now() + 3000;
        while (Date.now() < deadline) {
          const index = messages.findIndex(m => m.type === type);
          if (index >= 0) return messages.splice(index, 1)[0];
          await new Promise(resolve => setTimeout(resolve, 5));
        }
        throw new Error(`Timed out waiting for ${type}: ${JSON.stringify(messages)}`);
      },
    };
  }
  try {
    const rejected = new WebSocket(origin.replace('http:', 'ws:') + '/ws', { origin: 'http://untrusted.example' });
    await new Promise<void>((resolve, reject) => { rejected.once('error', () => resolve()); rejected.once('open', () => { rejected.terminate(); reject(new Error('Untrusted origin admitted')); }); });
    const secret = auth.getBootstrapSecret();
    const response = await fetch(`${origin}/api/host/auth`, { method: 'POST', headers: { 'Content-Type': 'application/json', Origin: origin }, body: JSON.stringify({ secret }) });
    assert.equal(response.status, 200); const { token } = await response.json() as { token: string };
    assert.equal(auth.authenticateHost(secret), null);
    assert.equal((await fetch(`${origin}/api/network/interfaces`)).status, 200);
    const host = await connect();
    host.send({ type: 'host:create', token: 'invalid', title: 'Test', maxViewers: 1 }); assert.match((await host.next('error')).message, /authentication/);
    host.send({ type: 'host:create', token, title: 'Test', pin: '123456', maxViewers: 1 });
    const { session } = await host.next('host:created');
    const viewer = await connect();
    viewer.send({ type: 'viewer:join', sessionId: session.id, pin: '000000' }); assert.match((await viewer.next('error')).message, /Wrong PIN/);
    viewer.send({ type: 'viewer:join', sessionId: session.id, pin: '123456' }); const accepted = await viewer.next('viewer:accepted');
    assert.ok(accepted.token); assert.equal((await host.next('viewer:ready')).viewerId, accepted.viewerId);
    viewer.send({ type: 'host:end' }); assert.match((await viewer.next('error')).message, /Presenter only/); assert.ok(store.getActiveSession());
    const another = await connect(); another.send({ type: 'viewer:join', sessionId: session.id, pin: '123456' }); assert.match((await another.next('error')).message, /full/);
    host.send({ type: 'rtc:offer', sessionId: session.id, targetViewerId: accepted.viewerId, connectionGeneration: 1, sdp: 'test-offer' }); assert.equal((await viewer.next('rtc:offer')).sdp, 'test-offer');
    viewer.send({ type: 'rtc:answer', sessionId: session.id, viewerId: accepted.viewerId, connectionGeneration: 1, sdp: 'test-answer' }); assert.equal((await host.next('rtc:answer')).sdp, 'test-answer');
    host.send({ type: 'rtc:offer', sessionId: session.id, targetViewerId: accepted.viewerId, connectionGeneration: 1, sdp: 'stale' }); assert.match((await host.next('error')).message, /stale/);
    host.send({ type: 'host:remove-viewer', viewerId: accepted.viewerId }); await host.next('viewer:left');
    another.send({ type: 'viewer:join', sessionId: session.id, token: accepted.token }); assert.match((await another.next('error')).message, /expired/);
    host.send({ type: 'host:lock', locked: true });
    await new Promise(resolve => setTimeout(resolve, 20));
    another.send({ type: 'viewer:join', sessionId: session.id }); assert.match((await another.next('error')).message, /locked/);
    host.send({ type: 'host:end' });
    await new Promise(resolve => setTimeout(resolve, 20)); assert.equal(store.getActiveSession(), null);
  } finally { for (const s of sockets) s.terminate(); close(); server.closeAllConnections(); await new Promise<void>(resolve => server.close(() => resolve())); }
});
