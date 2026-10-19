export class ChangeDetector {
  private video = document.createElement('video');
  private canvas = document.createElement('canvas');
  private previous?: Uint8ClampedArray;
  private timer?: ReturnType<typeof setInterval>;
  private unchangedSince = performance.now();
  private lastCheck = performance.now();
  private still = false;
  private closed = false;
  constructor(stream: MediaStream, private change: () => void, private idle: (canvas: HTMLCanvasElement) => void) {
    this.video.muted = true; this.video.playsInline = true; this.video.srcObject = stream;
    void this.video.play().then(() => { if (!this.closed) this.timer = setInterval(() => this.check(), 200); }).catch(() => { if (!this.closed) this.change(); });
    document.addEventListener('visibilitychange', this.reset);
  }
  private reset = () => { this.previous = undefined; this.unchangedSince = performance.now(); this.still = false; this.change(); };
  private check() {
    const now = performance.now();
    const delayed = now - this.lastCheck > 1000; this.lastCheck = now;
    if (document.hidden || delayed || this.video.readyState < 2) { this.reset(); return; }
    const width = this.video.videoWidth, height = this.video.videoHeight;
    if (!width || !height || width * height > 16_777_216) { this.reset(); return; }
    if (this.canvas.width !== width || this.canvas.height !== height) { this.canvas.width = width; this.canvas.height = height; this.previous = undefined; }
    try {
      const ctx = this.canvas.getContext('2d', { willReadFrequently: true })!;
      ctx.drawImage(this.video, 0, 0);
      const pixels = ctx.getImageData(0, 0, width, height).data;
      let changed = !this.previous;
      // Full-resolution tiles include every pixel: small edits cannot vanish in downsampling.
      for (let start = 0; !changed && start < pixels.length; start += 4096) {
        for (let i = start; i < Math.min(start + 4096, pixels.length); i++) if (pixels[i] !== this.previous![i]) { changed = true; break; }
      }
      this.previous = pixels;
      if (changed) { this.unchangedSince = now; this.still = false; this.change(); }
      else if (!this.still && now - this.unchangedSince >= 5000) { this.still = true; this.idle(this.canvas); }
    } catch { this.reset(); }
  }
  close() {
    this.closed = true; clearInterval(this.timer); document.removeEventListener('visibilitychange', this.reset);
    this.video.pause(); this.video.srcObject = null; this.previous = undefined; this.canvas.width = this.canvas.height = 0;
  }
}
