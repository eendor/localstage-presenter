/**
 * WebSocket signaling client with automatic reconnection.
 */

import type { ClientMessage, ServerMessage } from '@localstage/protocol';

export type ConnectionState = 'connecting' | 'connected' | 'reconnecting' | 'disconnected';

export interface SignalingClientOptions {
  url: string;
  onMessage: (msg: ServerMessage) => void;
  onStateChange: (state: ConnectionState) => void;
  maxReconnectAttempts?: number;
  reconnectBaseDelay?: number;
}

export class SignalingClient {
  private ws: WebSocket | null = null;
  private options: Required<SignalingClientOptions>;
  private reconnectAttempts = 0;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private intentionalClose = false;
  private _state: ConnectionState = 'disconnected';

  constructor(options: SignalingClientOptions) {
    this.options = {
      maxReconnectAttempts: 10,
      reconnectBaseDelay: 1000,
      ...options,
    };
  }

  get state(): ConnectionState {
    return this._state;
  }

  connect(): void {
    this.intentionalClose = false;
    this.setState('connecting');
    this.createConnection();
  }

  disconnect(): void {
    this.intentionalClose = true;
    this.clearReconnectTimer();
    if (this.ws) {
      this.ws.onopen = null;
      this.ws.onmessage = null;
      this.ws.onclose = null;
      this.ws.onerror = null;
      this.ws.close(1000, 'Client disconnect');
      this.ws = null;
    }
    this.setState('disconnected');
  }

  send(msg: ClientMessage): boolean {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.warn('[Signaling] Cannot send, not connected');
      return false;
    }
    this.ws.send(JSON.stringify(msg));
    return true;
  }

  private createConnection(): void {
    try {
      this.ws = new WebSocket(this.options.url);

      this.ws.onopen = () => {
        this.reconnectAttempts = 0;
        this.setState('connected');
      };

      this.ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data as string) as ServerMessage;
          this.options.onMessage(msg);
        } catch (err) {
          console.error('[Signaling] Failed to parse message:', err);
        }
      };

      this.ws.onclose = () => {
        if (!this.intentionalClose) {
          this.attemptReconnect();
        }
      };

      this.ws.onerror = (err) => {
        console.error('[Signaling] WebSocket error:', err);
      };
    } catch (err) {
      console.error('[Signaling] Failed to create WebSocket:', err);
      this.attemptReconnect();
    }
  }

  private attemptReconnect(): void {
    if (this.reconnectAttempts >= this.options.maxReconnectAttempts) {
      this.setState('disconnected');
      return;
    }

    this.setState('reconnecting');
    const delay = Math.min(
      this.options.reconnectBaseDelay * Math.pow(2, this.reconnectAttempts),
      30_000
    );
    this.reconnectAttempts++;

    this.reconnectTimer = setTimeout(() => {
      this.createConnection();
    }, delay);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  private setState(state: ConnectionState): void {
    if (this._state !== state) {
      this._state = state;
      this.options.onStateChange(state);
    }
  }
}

export const signalingUrl = () => `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;
