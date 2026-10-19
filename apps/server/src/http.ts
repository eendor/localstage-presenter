import express, { Express, Request, Response, NextFunction } from 'express';
import path from 'path';
import { SessionStore } from './session-store.js';
import { getNetworkInterfaces } from './network.js';
import { HostAuth } from './auth.js';

export function isLoopback(address?: string): boolean {
  return address === '127.0.0.1' || address === '::1' || address === '::ffff:127.0.0.1';
}

export function setupHttpRoutes(
  app: Express,
  webDistPath: string,
  sessionStore: SessionStore,
  auth: HostAuth,
  setupFeatures?: () => void,
  network?: { info: () => object; select: (address: string) => Promise<void> }
): void {
  // --- API routes ---

  // Get available network interfaces
  app.get('/api/network/interfaces', (_req: Request, res: Response) => {
    res.json({ interfaces: getNetworkInterfaces(), ...network?.info() });
  });
  app.post('/api/network/select', express.json({ limit: '2kb' }), async (req, res) => {
    if (!isLoopback(req.socket.remoteAddress) || !auth.validateHostToken((req.headers.authorization || '').replace(/^Bearer /, ''))) { res.sendStatus(403); return; }
    if (!network || typeof req.body?.address !== 'string' || !getNetworkInterfaces().some(a => a.address === req.body.address)) { res.status(400).json({ error: 'Adapter is unavailable' }); return; }
    try { await network.select(req.body.address); res.json(network.info()); }
    catch (error) { res.status(500).json({ error: String(error) }); }
  });

  app.post('/api/host/auth', express.json({ limit: '2kb' }), (req, res) => {
    if (!isLoopback(req.socket.remoteAddress)) { res.sendStatus(403); return; }
    const origin = req.headers.origin;
    if (origin && origin !== `http://${req.headers.host}`) { res.sendStatus(403); return; }
    const token = auth.authenticateHost(req.body?.secret);
    res.setHeader('Cache-Control', 'no-store');
    if (!token) { res.status(403).json({ error: 'Invalid or expired launcher link. Restart the server for a new link.' }); return; }
    res.json({ token });
  });

  // Get current session info (public, no credentials)
  app.get('/api/session', (_req: Request, res: Response) => {
    const session = sessionStore.getActiveSession();
    if (!session) {
      res.status(404).json({ error: 'No active session' });
      return;
    }
    res.json({
      id: session.id,
      title: session.title,
      status: session.status,
      joinLocked: session.joinLocked,
      viewerCount: sessionStore.getViewerCount(session.id),
      hasPin: sessionStore.sessionHasPin(session.id),
    });
  });

  // --- Static file serving ---
  setupFeatures?.();
  app.use('/api', (_req, res) => { res.status(404).json({ error: 'Unknown API endpoint' }); });

  // Serve built frontend assets
  app.use(express.static(webDistPath));

  // SPA fallback: serve index.html for all unmatched routes
  app.get('*', (_req: Request, res: Response) => {
    const indexPath = path.join(webDistPath, 'index.html');
    res.sendFile(indexPath, (err) => {
      if (err) {
        res.status(500).send('Frontend not built. Run: npm run build');
      }
    });
  });
  app.use((error: { status?: number }, _req: Request, res: Response, _next: NextFunction) => {
    res.status(error.status || 500).json({ error: error.status === 413 ? 'Upload exceeds the configured file-size limit' : 'Request could not be processed' });
  });
}
