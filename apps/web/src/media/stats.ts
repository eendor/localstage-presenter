export interface MediaStats { width?: number; height?: number; fps?: number; kbps?: number; lost?: number; rtt?: number }
export class StatsSampler {
  private previous = new Map<string, { bytes: number; time: number }>();
  async sample(pc: RTCPeerConnection): Promise<MediaStats> {
    const result: MediaStats = {};
    const stats = await pc.getStats();
    stats.forEach(report => {
      if (report.type === 'outbound-rtp' && report.kind === 'video') {
        result.width = report.frameWidth; result.height = report.frameHeight; result.fps = report.framesPerSecond;
        const old = this.previous.get(report.id);
        if (old && report.timestamp > old.time) result.kbps = Math.round(8 * (report.bytesSent - old.bytes) / (report.timestamp - old.time));
        this.previous.set(report.id, { bytes: report.bytesSent, time: report.timestamp });
      }
      if (report.type === 'remote-inbound-rtp' && report.kind === 'video') { result.lost = report.packetsLost; result.rtt = report.roundTripTime; }
    });
    return result;
  }
}
