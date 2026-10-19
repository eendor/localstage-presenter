import type { ClientMessage, ServerMessage } from '@localstage/protocol';
import { SnapshotReceiver } from './snapshot-channel';

export class ViewerPeer {
  private pc?: RTCPeerConnection;
  private generation = 0;
  private ice: (RTCIceCandidateInit | null)[] = [];
  private snapshots?: SnapshotReceiver;
  constructor(private viewerId: string, private send: (message: ClientMessage) => boolean, private onStream: (stream: MediaStream) => void, private onState: (state: string) => void, private display: (url: string | null, ready?: () => void) => void, private resume: (done: () => void) => void) {}
  async handle(m: ServerMessage) {
    if (m.type === 'rtc:offer') {
      if (m.connectionGeneration <= this.generation) return;
      this.close(); this.generation = m.connectionGeneration;
      const pc = this.pc = new RTCPeerConnection({ iceServers: [] });
      pc.ondatachannel = event => { if (event.channel.label === 'snapshots') this.snapshots = new SnapshotReceiver(event.channel, m.sessionId, this.display, this.resume); };
      const stream = new MediaStream();
      pc.ontrack = event => { stream.addTrack(event.track); this.onStream(stream); };
      pc.onconnectionstatechange = () => this.onState(pc.connectionState);
      pc.onicecandidate = event => this.send({ type: 'rtc:ice', sessionId: m.sessionId, targetId: 'host', connectionGeneration: m.connectionGeneration, candidate: event.candidate?.toJSON() || null });
      await pc.setRemoteDescription({ type: 'offer', sdp: m.sdp });
      for (const candidate of this.ice.splice(0)) await pc.addIceCandidate(candidate || undefined);
      await pc.setLocalDescription(await pc.createAnswer());
      this.send({ type: 'rtc:answer', sessionId: m.sessionId, viewerId: this.viewerId, connectionGeneration: m.connectionGeneration, sdp: pc.localDescription!.sdp });
    } else if (m.type === 'rtc:ice' && m.connectionGeneration === this.generation) {
      if (this.pc?.remoteDescription) await this.pc.addIceCandidate(m.candidate || undefined);
      else this.ice.push(m.candidate);
    }
  }
  close() { this.snapshots?.close(); this.snapshots = undefined; if (this.pc) { this.pc.ondatachannel = null; this.pc.ontrack = null; this.pc.onicecandidate = null; this.pc.onconnectionstatechange = null; this.pc.close(); } this.pc = undefined; this.ice = []; }
}
