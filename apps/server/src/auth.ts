import crypto from 'crypto';
import { PIN_RATE_LIMIT_MAX_ATTEMPTS, PIN_RATE_LIMIT_WINDOW_MS } from '@localstage/protocol';

/**
 * Host bootstrap secret — issued once by the local launcher,
 * exchanged only on the loopback service.
 */
export class HostAuth {
  private bootstrapSecret: string;
  private hostToken: string | null = null;
  private expiresAt = Date.now() + 5 * 60_000;

  constructor() {
    this.bootstrapSecret = crypto.randomBytes(32).toString('hex');
  }

  getBootstrapSecret(): string {
    return this.bootstrapSecret;
  }

  /**
   * Validate bootstrap secret and issue a host token.
   * Only valid on loopback.
   */
  authenticateHost(secret: string): string | null {
    if (!this.bootstrapSecret || Date.now() > this.expiresAt || secret !== this.bootstrapSecret) return null;
    this.bootstrapSecret = '';
    this.hostToken = crypto.randomBytes(32).toString('hex');
    return this.hostToken;
  }

  validateHostToken(token: string): boolean {
    return token !== null && token === this.hostToken;
  }
}

/**
 * PIN rate limiter — prevents brute-force guessing of the join PIN.
 */
export class PinRateLimiter {
  private attempts = new Map<string, { count: number; firstAttempt: number }>();

  /**
   * Check if an IP has exceeded the rate limit.
   * Returns true if the attempt is allowed.
   */
  checkLimit(clientIp: string): boolean {
    const now = Date.now();
    const record = this.attempts.get(clientIp);

    if (!record || now - record.firstAttempt > PIN_RATE_LIMIT_WINDOW_MS) {
      this.attempts.set(clientIp, { count: 1, firstAttempt: now });
      return true;
    }

    if (record.count >= PIN_RATE_LIMIT_MAX_ATTEMPTS) {
      return false;
    }

    record.count++;
    return true;
  }

  /**
   * Reset rate limit for a client (e.g., after successful join).
   */
  reset(clientIp: string): void {
    this.attempts.delete(clientIp);
  }

  /**
   * Periodic cleanup of expired entries.
   */
  cleanup(): void {
    const now = Date.now();
    for (const [ip, record] of this.attempts.entries()) {
      if (now - record.firstAttempt > PIN_RATE_LIMIT_WINDOW_MS) {
        this.attempts.delete(ip);
      }
    }
  }
}
