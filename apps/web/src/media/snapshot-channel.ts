export interface SnapshotMeta {
  sessionId: string; sourceGeneration: number; modeGeneration: number; snapshotId: string;
  mime: 'image/png'; width: number; height: number; bytes: number;
}
export const MAX_SNAPSHOT_BYTES = 32 * 1024 * 1024;
export const sameSnapshot = (a: SnapshotMeta, b: SnapshotMeta) => a.sessionId === b.sessionId && a.sourceGeneration === b.sourceGeneration && a.modeGeneration === b.modeGeneration && a.snapshotId === b.snapshotId;
export async function sendSnapshot(channel: RTCDataChannel, meta: SnapshotMeta, blob: Blob, maxMessageSize: number, valid: () => boolean) {
  const bytes = new Uint8Array(await blob.arrayBuffer());
  const size = Math.min(16384, maxMessageSize || 16384);
  const deadline = performance.now() + 10000;
  const ready = () => valid() && channel.readyState === 'open' && performance.now() < deadline;
  if (!ready()) return;
  channel.send(JSON.stringify({ type: 'snapshot:start', meta }));
  for (let offset = 0; offset < bytes.length; offset += size) {
    while (channel.bufferedAmount > 256 * 1024 && ready()) await new Promise(resolve => setTimeout(resolve, 20));
    if (!ready()) return;
    channel.send(bytes.slice(offset, offset + size));
  }
  if (ready()) channel.send(JSON.stringify({ type: 'snapshot:end', meta }));
}
export class SnapshotReceiver {
  private meta?: SnapshotMeta;
  private chunks: ArrayBuffer[] = [];
  private size = 0;
  private epoch = 0;
  private url?: string;
  private timeout?: ReturnType<typeof setTimeout>;
  constructor(private channel: RTCDataChannel, private sessionId: string, private display: (url: string | null, ready?: () => void) => void, private resume: (done: () => void) => void) {
    channel.binaryType = 'arraybuffer'; channel.onmessage = event => { void this.receive(event.data).catch(() => this.clear()); };
    channel.onclose = () => this.clear();
  }
  private async receive(data: unknown) {
    if (data instanceof ArrayBuffer) {
      if (!this.meta || data.byteLength > 16384 || this.size + data.byteLength > this.meta.bytes) { this.clear(); return; }
      this.chunks.push(data); this.size += data.byteLength; return;
    }
    if (typeof data !== 'string' || data.length > 4096) return;
    const message = JSON.parse(data);
    if (message.type === 'live') {
      clearTimeout(this.timeout);
      const epoch = ++this.epoch; this.meta = undefined; this.chunks = []; this.size = 0;
      this.resume(() => { if (epoch === this.epoch) this.clear(); }); return;
    }
    if (message.type === 'clear') { this.clear(); return; }
    const m = message.meta as SnapshotMeta;
    if (!m || m.sessionId !== this.sessionId || !Number.isSafeInteger(m.sourceGeneration) || !Number.isSafeInteger(m.modeGeneration) || typeof m.snapshotId !== 'string' || m.snapshotId.length > 128 || m.mime !== 'image/png' || !Number.isInteger(m.bytes) || m.bytes < 1 || m.bytes > MAX_SNAPSHOT_BYTES || !Number.isInteger(m.width) || !Number.isInteger(m.height) || m.width < 1 || m.height < 1 || m.width * m.height > 16_777_216) return;
    if (message.type === 'snapshot:start') {
      this.clear(); this.meta = m; this.timeout = setTimeout(() => this.clear(), 15000);
    } else if (message.type === 'snapshot:end' && this.meta && sameSnapshot(m, this.meta) && this.size === m.bytes) {
      const epoch = this.epoch;
      const blob = new Blob(this.chunks, { type: 'image/png' }); this.chunks = [];
      const header = new DataView(await blob.slice(0, 24).arrayBuffer());
      if (header.byteLength !== 24 || header.getUint32(0) !== 0x89504e47 || header.getUint32(4) !== 0x0d0a1a0a || header.getUint32(16) !== m.width || header.getUint32(20) !== m.height) { this.clear(); return; }
      if (epoch !== this.epoch) return;
      const url = URL.createObjectURL(blob); this.url = url;
      this.display(url, () => {
        if (epoch === this.epoch && this.channel.readyState === 'open') { clearTimeout(this.timeout); this.channel.send(JSON.stringify({ type: 'snapshot:ready', meta: m })); }
      });
    }
  }
  clear() { ++this.epoch; clearTimeout(this.timeout); this.meta = undefined; this.chunks = []; this.size = 0; this.display(null); if (this.url) URL.revokeObjectURL(this.url); this.url = undefined; }
  close() { this.channel.onmessage = null; this.channel.onclose = null; this.clear(); }
}
