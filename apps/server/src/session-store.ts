import crypto from 'crypto';
import type { Session, SessionStatus, Viewer } from '@localstage/protocol';
import { DEFAULT_MAX_VIEWERS } from '@localstage/protocol';

interface SessionInternal extends Session {
  pinHash?: string;
}

export class SessionStore {
  private sessions = new Map<string, SessionInternal>();
  private viewers = new Map<string, Viewer>();
  private activeSessionId: string | null = null;

  /**
   * Create a new presentation session.
   * Only one active session at a time.
   */
  createSession(title: string, pin?: string, maxViewers?: number, source: 'screen' | 'deck' = 'screen'): Session {
    if (this.activeSessionId) {
      this.endSession(this.activeSessionId);
    }

    const id = crypto.randomUUID();
    const session: SessionInternal = {
      id,
      source,
      title,
      status: 'waiting',
      joinLocked: false,
      maxViewers: maxViewers ?? DEFAULT_MAX_VIEWERS,
      createdAt: Date.now(),
      pinHash: pin ? this.hashPin(pin) : undefined,
    };

    this.sessions.set(id, session);
    this.activeSessionId = id;
    return this.toPublicSession(session);
  }

  getActiveSession(): Session | null {
    if (!this.activeSessionId) return null;
    const session = this.sessions.get(this.activeSessionId);
    return session ? this.toPublicSession(session) : null;
  }

  getSession(id: string): Session | null {
    const session = this.sessions.get(id);
    return session ? this.toPublicSession(session) : null;
  }

  updateStatus(sessionId: string, status: SessionStatus): boolean {
    const session = this.sessions.get(sessionId);
    if (!session) return false;
    session.status = status;
    return true;
  }

  setJoinLocked(sessionId: string, locked: boolean): boolean {
    const session = this.sessions.get(sessionId);
    if (!session) return false;
    session.joinLocked = locked;
    return true;
  }

  sessionHasPin(sessionId: string): boolean {
    const session = this.sessions.get(sessionId);
    return !!session?.pinHash;
  }

  validatePin(sessionId: string, pin: string): boolean {
    const session = this.sessions.get(sessionId);
    if (!session?.pinHash) return true;  // No PIN required
    return this.hashPin(pin) === session.pinHash;
  }

  // --- Viewer management ---

  addViewer(sessionId: string, displayName?: string): Viewer | null {
    const session = this.sessions.get(sessionId);
    if (!session || session.status === 'ended') return null;
    if (session.joinLocked) return null;
    if (this.getViewerCount(sessionId) >= session.maxViewers) return null;

    const viewer: Viewer = {
      id: crypto.randomUUID(),
      sessionId,
      displayName: displayName || `Viewer ${this.getViewerCount(sessionId) + 1}`,
      state: 'joining',
      joinedAt: Date.now(),
    };

    this.viewers.set(viewer.id, viewer);
    return viewer;
  }

  removeViewer(sessionId: string, viewerId: string): boolean {
    const viewer = this.viewers.get(viewerId);
    if (!viewer || viewer.sessionId !== sessionId) return false;
    viewer.state = 'left';
    this.viewers.delete(viewerId);
    return true;
  }

  getViewerCount(sessionId: string): number {
    let count = 0;
    for (const viewer of this.viewers.values()) {
      if (viewer.sessionId === sessionId) count++;
    }
    return count;
  }

  getViewers(sessionId: string): Viewer[] {
    const result: Viewer[] = [];
    for (const viewer of this.viewers.values()) {
      if (viewer.sessionId === sessionId) result.push(viewer);
    }
    return result;
  }

  // --- Session lifecycle ---

  endSession(sessionId: string): void {
    const session = this.sessions.get(sessionId);
    if (!session) return;
    session.status = 'ended';

    // Remove all viewers
    for (const viewer of this.viewers.values()) {
      if (viewer.sessionId === sessionId) {
        this.viewers.delete(viewer.id);
      }
    }

    if (this.activeSessionId === sessionId) {
      this.activeSessionId = null;
    }
    this.sessions.delete(sessionId);
  }

  endAllSessions(): void {
    for (const sessionId of this.sessions.keys()) {
      this.endSession(sessionId);
    }
  }

  // --- Internal helpers ---

  private hashPin(pin: string): string {
    return crypto.createHash('sha256').update(pin).digest('hex');
  }

  private toPublicSession(session: SessionInternal): Session {
    const { pinHash: _, ...publicFields } = session;
    return publicFields;
  }
}
