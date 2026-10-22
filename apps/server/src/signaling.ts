import { Server as HttpServer, IncomingMessage } from 'http';
import crypto from 'crypto';
import { WebSocketServer, WebSocket } from 'ws';
import { SessionStore } from './session-store.js';
import { HostAuth, PinRateLimiter } from './auth.js';
import { isLoopback } from './http.js';
import { MAX_MESSAGE_SIZE, validateClientMessage } from '@localstage/protocol';
import { DeckStore } from './decks.js';

interface Member extends WebSocket {
  role?: 'host' | 'viewer';
  viewerId?: string;
  sessionId?: string;
  alive?: boolean;
}

export function setupSignaling(server: HttpServer | HttpServer[], store: SessionStore, auth: HostAuth, decks = new DeckStore(), authorizeViewer?: (check: (token: string) => boolean) => void): (() => void) & { end: () => void } {
  const wss = new WebSocketServer({ noServer: true, path: '/ws', maxPayload: MAX_MESSAGE_SIZE,
    verifyClient: ({ req, origin }: { req: IncomingMessage; origin: string }) => {
      try { return new URL(origin).origin === `http://${req.headers.host}`; } catch { return false; }
    },
  });
  const servers = Array.isArray(server) ? server : [server];
  const upgrade: Parameters<HttpServer['on']>[1] = (req: IncomingMessage, socket: any, head: Buffer) => {
    if (req.url !== '/ws') { socket.destroy(); return; }
    wss.handleUpgrade(req, socket, head, ws => wss.emit('connection', ws, req));
  };
  for (const listener of servers) listener.on('upgrade', upgrade);
  const limiter = new PinRateLimiter();
  const tokens = new Map<string, { sessionId: string; displayName: string }>();
  const memberTokens = new Map<string, string>();
  const generations = new Map<string, number>();
  authorizeViewer?.(token => {
    const membership = tokens.get(token);
    return !!membership && membership.sessionId === store.getActiveSession()?.id && [...memberTokens.values()].includes(token);
  });
  let host: Member | undefined;
  const send = (socket: WebSocket | undefined, message: object) => {
    if (socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify(message));
  };
  const error = (socket: Member, message: string) => send(socket, { type: 'error', code: 'REJECTED', message });
  const viewers = () => [...wss.clients].filter(s => (s as Member).role === 'viewer') as Member[];
  const state = () => {
    const session = store.getActiveSession();
    if (!session) return;
    for (const socket of wss.clients as Set<Member>) if (socket.sessionId === session.id) send(socket, {
      type: 'session:state', sessionId: session.id, status: session.status, session,
      viewerCount: store.getViewerCount(session.id),
      ...(socket.role === 'host' ? { viewers: store.getViewers(session.id) } : {}),
    });
  };
  const remove = (socket: Member, reason: 'removed' | 'left' | 'disconnected') => {
    if (!socket.viewerId || !socket.sessionId) return;
    const id = socket.viewerId;
    const token = memberTokens.get(id);
    if (reason !== 'disconnected' && token) tokens.delete(token);
    memberTokens.delete(id);
    generations.delete(id);
    store.removeViewer(socket.sessionId, id);
    socket.viewerId = undefined;
    socket.sessionId = undefined;
    send(host, { type: 'viewer:left', viewerId: id, reason });
    state();
  };
  const end = () => {
    const session = store.getActiveSession();
    if (session) {
      store.updateStatus(session.id, 'ended');
      state();
      store.endSession(session.id);
    }
    host = undefined;
    void decks.clear().catch(console.error);
    tokens.clear(); memberTokens.clear(); generations.clear();
    for (const s of wss.clients as Set<Member>) { s.sessionId = undefined; s.viewerId = undefined; s.role = undefined; s.close(1000, 'Session ended'); }
  };
  const heartbeat = setInterval(() => {
    limiter.cleanup();
    for (const s of wss.clients as Set<Member>) {
      if (s.alive === false) s.terminate();
      else { s.alive = false; s.ping(); }
    }
  }, 15000);
  wss.on('connection', (ws, req) => {
    const s = ws as Member;
    s.alive = true;
    s.on('pong', () => { s.alive = true; });
    s.on('error', () => s.terminate());
    s.on('message', data => {
      let m: unknown;
      try { m = JSON.parse(data.toString()); } catch { error(s, 'Invalid JSON'); return; }
      if (!validateClientMessage(m)) { error(s, 'Invalid message'); return; }
      if (m.type === 'host:create') {
        if (s.role || host || !isLoopback(req.socket.remoteAddress) || !auth.validateHostToken(m.token)) { error(s, 'Host authentication required or session already active'); return; }
        const session = store.createSession(m.title.trim() || 'Presentation', m.pin, m.maxViewers, m.source);
        s.role = 'host'; s.sessionId = session.id; host = s;
        store.updateStatus(session.id, 'live');
        send(s, { type: 'host:created', session: store.getActiveSession() }); state(); return;
      }
      if (m.type === 'viewer:join') {
        const session = store.getActiveSession();
        if (s.role || !host || !session || session.id !== m.sessionId) { error(s, 'Host unavailable'); return; }
        const previous = m.token ? tokens.get(m.token) : undefined;
        if (m.token && previous?.sessionId !== session.id) { error(s, 'Membership expired. Join again.'); return; }
        if (session.joinLocked) { error(s, 'Joining is locked'); return; }
        if (!previous && store.sessionHasPin(session.id)) {
          if (!limiter.checkLimit(req.socket.remoteAddress || 'unknown')) { error(s, 'Too many PIN attempts. Wait one minute.'); return; }
          if (!store.validatePin(session.id, m.pin || '')) { error(s, 'Wrong PIN'); return; }
        }
        if (m.token) for (const old of viewers()) if (old.viewerId && memberTokens.get(old.viewerId) === m.token) { remove(old, 'disconnected'); old.close(); }
        const viewer = store.addViewer(session.id, previous?.displayName || m.displayName?.trim());
        if (!viewer) { error(s, 'Session is full'); return; }
        viewer.state = 'connected';
        const token = m.token || crypto.randomBytes(32).toString('hex');
        tokens.set(token, { sessionId: session.id, displayName: viewer.displayName }); memberTokens.set(viewer.id, token);
        s.role = 'viewer'; s.sessionId = session.id; s.viewerId = viewer.id;
        send(s, { type: 'viewer:accepted', viewerId: viewer.id, sessionId: session.id, token });
        send(host, { type: 'viewer:ready', viewerId: viewer.id, displayName: viewer.displayName }); state();
        if (decks.state) send(s, { type: 'deck:state', state: decks.state });
        return;
      }
      if (!s.sessionId || s.sessionId !== store.getActiveSession()?.id) { error(s, 'Session membership required'); return; }
      if (m.type === 'deck:activate' || m.type === 'presenter:navigate') {
        if (s !== host) { error(s, 'Presenter only'); return; }
        if (m.type === 'deck:activate') {
          if (!decks.activate(m.deckId)) { error(s, 'Unknown converted deck'); return; }
        } else if (!decks.navigate(m.deckId, m.deckRevision, m.stateSequence, m.targetSlide)) return;
        for (const member of wss.clients as Set<Member>) if (member.sessionId === s.sessionId) send(member, { type: 'deck:state', state: decks.state });
        return;
      }
      if (m.type === 'viewer:leave' && s.role === 'viewer') { remove(s, 'left'); s.close(); return; }
      if (m.type.startsWith('host:')) {
        if (s !== host) { error(s, 'Presenter only'); return; }
        if (m.type === 'host:end') end();
        else if (m.type === 'host:lock') { store.setJoinLocked(s.sessionId, m.locked); state(); }
        else if (m.type === 'host:review-lock') { store.setReviewLocked(s.sessionId, m.locked); state(); }
        else if (m.type === 'host:status') { store.updateStatus(s.sessionId, m.status); state(); }
        else if (m.type === 'host:remove-viewer') {
          const target = viewers().find(v => v.viewerId === m.viewerId);
          if (target) { error(target, 'Removed by presenter'); remove(target, 'removed'); target.close(4003, 'Removed'); }
        }
        return;
      }
      if (m.type === 'rtc:offer' || m.type === 'rtc:answer' || m.type === 'rtc:ice') {
        if (m.sessionId !== s.sessionId) { error(s, 'Wrong session'); return; }
        const id = m.type === 'rtc:offer' ? m.targetViewerId : s === host && m.type === 'rtc:ice' ? m.targetId : s.viewerId;
        const target = viewers().find(v => v.viewerId === id);
        if (!id || !target) { error(s, 'Unknown viewer'); return; }
        if (m.type === 'rtc:offer') {
          if (s !== host || m.connectionGeneration <= (generations.get(id) || 0)) { error(s, 'Unauthorized or stale offer'); return; }
          generations.set(id, m.connectionGeneration);
        } else if (m.connectionGeneration !== generations.get(id)) { error(s, 'Stale connection generation'); return; }
        if (m.type === 'rtc:answer' && (s.role !== 'viewer' || m.viewerId !== s.viewerId)) { error(s, 'Unauthorized answer'); return; }
        if (m.type === 'rtc:ice' && s !== host && m.targetId !== 'host') { error(s, 'Invalid ICE target'); return; }
        send(s === host ? target : host, m.type === 'rtc:ice' && s !== host ? { ...m, targetId: id } : m);
      }
    });
    s.on('close', () => { if (s === host) end(); else remove(s, 'disconnected'); });
  });
  const close = () => { clearInterval(heartbeat); for (const listener of servers) listener.off('upgrade', upgrade); end(); for (const s of wss.clients) s.terminate(); wss.close(); };
  return Object.assign(close, { end });
}
