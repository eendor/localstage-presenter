import { ChangeDetector } from './change-detector';
import { MAX_SNAPSHOT_BYTES, sameSnapshot, sendSnapshot, type SnapshotMeta } from './snapshot-channel';
interface Target { channel: RTCDataChannel; sender: RTCRtpSender; pc: RTCPeerConnection; pending?: SnapshotMeta; committed: boolean }
export class StillMode {
  private detector?: ChangeDetector;
  private source?: MediaStream;
  private targets = new Map<string, Target>();
  private sourceGeneration = 0;
  private modeGeneration = 0;
  private enabled = true;
  private paused = false;
  private queue = Promise.resolve();
  private snapshotCache?: { meta: SnapshotMeta; blob: Blob };
  constructor(private sessionId: string, private report: (stillCount: number) => void) {}
  add(id: string, channel: RTCDataChannel, sender: RTCRtpSender, pc: RTCPeerConnection) {
    const target: Target = { channel, sender, pc, committed: false }; this.targets.set(id, target);
    channel.onclose = () => {
      target.pending = undefined;
      if (target.committed) void sender.replaceTrack(this.source?.getVideoTracks()[0] || null).then(() => { target.committed = false; this.notify(); }).catch(() => {});
    };
    channel.onmessage = event => {
      try {
        if (typeof event.data !== 'string') return;
        const message = JSON.parse(event.data), pending = target.pending;
        if (message.type !== 'snapshot:ready' || !pending || !message.meta || !sameSnapshot(pending, message.meta)) return;
        this.queue = this.queue.then(async () => {
          if (target.pending !== pending || pending.modeGeneration !== this.modeGeneration || this.paused || !this.enabled) return;
          await target.sender.replaceTrack(null); target.committed = true; this.notify();
        }).catch(() => { void this.live(); });
      } catch { /* Ignore malformed acknowledgements. */ }
    };
    channel.onopen = () => {
      const cached = this.snapshotCache;
      if (cached && cached.meta.modeGeneration === this.modeGeneration && !this.paused && this.enabled) {
        target.pending = cached.meta;
        void sendSnapshot(channel, cached.meta, cached.blob, pc.sctp?.maxMessageSize || 16384, () => target.pending === cached.meta && this.modeGeneration === cached.meta.modeGeneration).catch(() => { target.pending = undefined; });
      } else this.restartDetector();
    };
  }
  remove(id: string) { const t = this.targets.get(id); if (t) { t.channel.onmessage = null; t.channel.onopen = null; t.channel.onclose = null; t.channel.close(); } this.targets.delete(id); this.notify(); }
  async configure(source: MediaStream, enabled: boolean, paused: boolean) {
    this.detector?.close(); this.detector = undefined;
    if (this.source !== source) ++this.sourceGeneration;
    this.source = source; this.enabled = enabled; this.paused = paused;
    await this.live(); this.restartDetector();
  }
  private restartDetector() {
    this.detector?.close(); this.detector = undefined;
    if (this.source && this.enabled && !this.paused) this.detector = new ChangeDetector(this.source, () => { void this.live(); }, canvas => { void this.snapshot(canvas); });
  }
  private notify() { this.report([...this.targets.values()].filter(t => t.committed).length); }
  private live() {
    this.snapshotCache = undefined;
    ++this.modeGeneration;
    for (const target of this.targets.values()) {
      const needsResume = target.pending || target.committed;
      target.pending = undefined;
      if ((needsResume || this.paused) && target.channel.readyState === 'open') target.channel.send(JSON.stringify({ type: this.paused ? 'clear' : 'live' }));
    }
    this.queue = this.queue.then(async () => {
      for (const target of this.targets.values()) if (target.committed) {
        await target.sender.replaceTrack(this.source?.getVideoTracks()[0] || null); target.committed = false;
      }
      this.notify();
    }).catch(() => { /* Closed peers are removed by host lifecycle. */ });
    return this.queue;
  }
  private async snapshot(canvas: HTMLCanvasElement) {
    const generation = ++this.modeGeneration, sourceGeneration = this.sourceGeneration;
    const width = canvas.width, height = canvas.height;
    const blob = await new Promise<Blob | null>(resolve => canvas.toBlob(resolve, 'image/png'));
    if (!blob || blob.size > MAX_SNAPSHOT_BYTES || generation !== this.modeGeneration || this.paused || !this.enabled) return;
    const meta: SnapshotMeta = { sessionId: this.sessionId, sourceGeneration, modeGeneration: generation, snapshotId: `${sourceGeneration}-${generation}`, mime: 'image/png', width, height, bytes: blob.size };
    this.snapshotCache = { meta, blob };
    for (const target of this.targets.values()) {
      if (target.channel.readyState !== 'open') continue;
      target.pending = meta;
      void sendSnapshot(target.channel, meta, blob, target.pc.sctp?.maxMessageSize || 16384, () => target.pending === meta && generation === this.modeGeneration).catch(() => { target.pending = undefined; });
    }
  }
  close() { this.detector?.close(); ++this.modeGeneration; for (const id of this.targets.keys()) this.remove(id); this.source = undefined; this.snapshotCache = undefined; }
}
