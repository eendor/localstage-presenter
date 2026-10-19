// ============================================================
// LocalStage Protocol — Shared message types and schemas
// ============================================================

// --- Session types ---

export type SessionStatus = 'waiting' | 'live' | 'paused' | 'ended';

export interface Session {
  source?: 'screen' | 'deck';
  id: string;
  title: string;
  status: SessionStatus;
  joinLocked: boolean;
  maxViewers: number;
  createdAt: number;
}

export interface Viewer {
  id: string;
  sessionId: string;
  displayName: string;
  state: 'joining' | 'connected' | 'reconnecting' | 'left';
  joinedAt: number;
}

// --- Deck / presentation types ---

export interface DeckState {
  deckId: string;
  deckRevision: number;
  totalSlides: number;
  presenterSlide: number;  // 1-based, authoritative on server
  stateSequence: number;   // monotonically increasing per committed navigation
}

export interface ViewerReviewState {
  mode: 'follow' | 'review';
  reviewSlide: number;
  deckId: string;
  deckRevision: number;
}

// --- Visual / still mode types ---

export type VisualMode = 'live' | 'still';

export interface VisualState {
  mode: VisualMode;
  sourceGeneration: number;
  modeGeneration: number;
  snapshotId?: string;
}

// --- Quality presets ---

export type QualityPreset = 'slides' | 'balanced' | 'low-bandwidth';

export const QUALITY_PRESETS: Record<QualityPreset, {
  width: number;
  height: number;
  maxFps: number;
  targetBitrateKbps: number;
  label: string;
}> = {
  slides: {
    width: 1920,
    height: 1080,
    maxFps: 15,
    targetBitrateKbps: 2000,
    label: 'Slides (1080p, 15 fps)',
  },
  balanced: {
    width: 1280,
    height: 720,
    maxFps: 30,
    targetBitrateKbps: 2500,
    label: 'Balanced (720p, 30 fps)',
  },
  'low-bandwidth': {
    width: 960,
    height: 540,
    maxFps: 15,
    targetBitrateKbps: 1000,
    label: 'Low Bandwidth (540p, 15 fps)',
  },
};

// --- Protocol messages ---

// Direction: Host → Server
export interface HostCreateMessage {
  type: 'host:create';
  token: string;
  source?: 'screen' | 'deck';
  title: string;
  pin?: string;
  maxViewers: number;
}

// Direction: Viewer → Server
export interface ViewerJoinMessage {
  type: 'viewer:join';
  sessionId: string;
  displayName?: string;
  pin?: string;
  token?: string;
}

// Direction: Server → Viewer
export interface ViewerAcceptedMessage {
  type: 'viewer:accepted';
  viewerId: string;
  sessionId: string;
  token: string;
}

// Direction: Server → Host
export interface ViewerReadyMessage {
  type: 'viewer:ready';
  viewerId: string;
  displayName: string;
}

// Direction: Host → Viewer via Server
export interface RtcOfferMessage {
  type: 'rtc:offer';
  targetViewerId: string;
  sessionId: string;
  connectionGeneration: number;
  sdp: string;
}

// Direction: Viewer → Host via Server
export interface RtcAnswerMessage {
  type: 'rtc:answer';
  viewerId: string;
  sessionId: string;
  connectionGeneration: number;
  sdp: string;
}

// Direction: Both ways via Server
export interface RtcIceMessage {
  type: 'rtc:ice';
  targetId: string;  // viewerId or 'host'
  sessionId: string;
  connectionGeneration: number;
  candidate: RTCIceCandidateInit | null;
}

// Direction: Server → All members
export interface SessionStateMessage {
  type: 'session:state';
  sessionId: string;
  status: SessionStatus;
  viewerCount: number;
  session: Session;
  viewers?: Viewer[];
}

// Direction: Host → Server
export interface DeckActivateMessage {
  type: 'deck:activate';
  deckId: string;
  deckRevision: number;
  totalSlides: number;
}

// Direction: Host → Server
export interface PresenterNavigateMessage {
  type: 'presenter:navigate';
  deckId: string;
  deckRevision: number;
  targetSlide: number;  // 1-based
  stateSequence: number;
}

// Direction: Server → All members
export interface DeckStateMessage {
  type: 'deck:state';
  state: DeckState;
}

// Direction: Host → Server
export interface HostLockMessage {
  type: 'host:lock';
  locked: boolean;
}

// Direction: Host → Server
export interface HostRemoveViewerMessage {
  type: 'host:remove-viewer';
  viewerId: string;
}

// Direction: Host → Server
export interface HostEndMessage {
  type: 'host:end';
}

// Direction: Server → Host
export interface ViewerLeftMessage {
  type: 'viewer:left';
  viewerId: string;
  reason: 'left' | 'removed' | 'disconnected';
}

// Direction: Host → Server (visual/still mode)
export interface VisualStateMessage {
  type: 'visual:state';
  state: VisualState;
}

// Direction: Server → Viewer (error)
export interface ErrorMessage {
  type: 'error';
  code: string;
  message: string;
}

// --- Union type of all messages ---

export type ClientMessage =
  | { type: 'host:status'; status: 'live' | 'paused' }
  | { type: 'viewer:leave' }
  | HostCreateMessage
  | ViewerJoinMessage
  | RtcOfferMessage
  | RtcAnswerMessage
  | RtcIceMessage
  | DeckActivateMessage
  | PresenterNavigateMessage
  | HostLockMessage
  | HostRemoveViewerMessage
  | HostEndMessage
  | VisualStateMessage;

export type ServerMessage =
  | { type: 'host:created'; session: Session }
  | ViewerAcceptedMessage
  | ViewerReadyMessage
  | RtcOfferMessage
  | RtcAnswerMessage
  | RtcIceMessage
  | SessionStateMessage
  | DeckStateMessage
  | ViewerLeftMessage
  | VisualStateMessage
  | ErrorMessage;

// --- Validation helpers ---

const VALID_CLIENT_TYPES = new Set<string>([
  'host:create', 'viewer:join', 'rtc:offer', 'rtc:answer', 'rtc:ice',
  'deck:activate', 'presenter:navigate', 'host:lock', 'host:remove-viewer',
  'host:end', 'visual:state',
  'host:status', 'viewer:leave',
]);

export function isValidClientMessageType(type: string): boolean {
  return VALID_CLIENT_TYPES.has(type);
}

/** Validate untrusted payloads before routing or mutating session state. */
export function validateClientMessage(value: unknown): value is ClientMessage {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const m = value as Record<string, unknown>;
  const text = (key: string, max = 128) => typeof m[key] === 'string' && (m[key] as string).length <= max;
  const optional = (key: string, max = 128) => m[key] === undefined || text(key, max);
  const integer = (key: string) => Number.isSafeInteger(m[key]) && Number(m[key]) >= 1;
  switch (m.type) {
    case 'host:create': return (m.source === undefined || m.source === 'screen' || m.source === 'deck') && text('token') && text('title') && optional('pin', 6) && (!m.pin || /^\d{6}$/.test(String(m.pin))) && integer('maxViewers') && Number(m.maxViewers) <= 50;
    case 'viewer:join': return text('sessionId') && optional('displayName', 64) && optional('pin', 6) && optional('token');
    case 'host:lock': return typeof m.locked === 'boolean';
    case 'host:remove-viewer': return text('viewerId');
    case 'host:status': return m.status === 'live' || m.status === 'paused';
    case 'host:end': case 'viewer:leave': return true;
    case 'deck:activate': return text('deckId') && integer('deckRevision') && integer('totalSlides');
    case 'presenter:navigate': return text('deckId') && integer('deckRevision') && integer('targetSlide') && Number.isSafeInteger(m.stateSequence) && Number(m.stateSequence) >= 0;
    case 'rtc:offer': case 'rtc:answer': return text('sessionId') && integer('connectionGeneration') && text(m.type === 'rtc:offer' ? 'targetViewerId' : 'viewerId') && text('sdp', 60000);
    case 'rtc:ice': {
      const c = m.candidate as Record<string, unknown> | null;
      return text('sessionId') && text('targetId') && integer('connectionGeneration') && (c === null || (!!c && typeof c === 'object' && typeof c.candidate === 'string' && c.candidate.length < 8192 && (c.sdpMid == null || typeof c.sdpMid === 'string') && (c.sdpMLineIndex == null || Number.isInteger(c.sdpMLineIndex))));
    }
    default: return false;
  }
}

export function isHostMessage(msg: ClientMessage): boolean {
  return msg.type.startsWith('host:') ||
    msg.type === 'rtc:offer' ||
    msg.type === 'deck:activate' ||
    msg.type === 'presenter:navigate' ||
    msg.type === 'visual:state';
}

export function isViewerMessage(msg: ClientMessage): boolean {
  return msg.type === 'viewer:join' ||
    msg.type === 'rtc:answer' ||
    msg.type === 'rtc:ice';
}

// --- Constants ---

export const MAX_MESSAGE_SIZE = 64 * 1024;  // 64 KiB
export const MAX_PIN_LENGTH = 6;
export const DEFAULT_MAX_VIEWERS = 5;
export const STILL_MODE_DELAY_MS = 5000;
export const SNAPSHOT_CHUNK_SIZE = 16 * 1024;  // 16 KiB
export const PIN_RATE_LIMIT_MAX_ATTEMPTS = 5;
export const PIN_RATE_LIMIT_WINDOW_MS = 60_000;
