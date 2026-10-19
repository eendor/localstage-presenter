import { QUALITY_PRESETS, type ClientMessage, type ServerMessage, type QualityPreset } from '@localstage/protocol';
import { StillMode } from './still-mode';

interface Peer {
  pc: RTCPeerConnection;
  generation: number;
  video: RTCRtpSender;
  audio: RTCRtpSender;
  hasAudio: boolean;
  ice: (RTCIceCandidateInit | null)[];
}

export class HostPeers {
  readonly peers = new Map<string, Peer>();
  private generation = 0;
  readonly still: StillMode;
  constructor(private sessionId: string, private send: (message: ClientMessage) => boolean, private report: (message: string) => void, reportStill: (count: number) => void) { this.still = new StillMode(sessionId, reportStill); }

  async add(id: string, stream: MediaStream) {
    this.remove(id);
    const pc = new RTCPeerConnection({ iceServers: [] });
    const video = pc.addTransceiver('video', { direction: 'sendonly' }).sender;
    const hasAudio = stream.getAudioTracks().length > 0;
    const audio = pc.addTransceiver('audio', { direction: hasAudio ? 'sendonly' : 'inactive' }).sender;
    const peer: Peer = { pc, video, audio, hasAudio, generation: ++this.generation, ice: [] };
    this.peers.set(id, peer);
    this.still.add(id, pc.createDataChannel('snapshots', { ordered: true }), video, pc);
    // SDP must be relayed before trickled candidates establish a generation.
    const pending: (RTCIceCandidateInit | null)[] = [];
    let offered = false;
    const sendIce = (candidate: RTCIceCandidateInit | null) => this.send({ type: 'rtc:ice', sessionId: this.sessionId, targetId: id, connectionGeneration: peer.generation, candidate });
    pc.onicecandidate = event => { const c = event.candidate?.toJSON() || null; if (offered) sendIce(c); else pending.push(c); };
    pc.onconnectionstatechange = () => { if (pc.connectionState === 'failed') this.report('Video connection failed. Ask the viewer to rejoin; check Wi-Fi isolation and firewall media rules.'); };
    await video.replaceTrack(stream.getVideoTracks()[0] || null);
    await audio.replaceTrack(stream.getAudioTracks()[0] || null);
    await pc.setLocalDescription(await pc.createOffer());
    this.send({ type: 'rtc:offer', sessionId: this.sessionId, targetViewerId: id, connectionGeneration: peer.generation, sdp: pc.localDescription!.sdp });
    offered = true; pending.forEach(sendIce);
  }

  async handle(message: ServerMessage) {
    if (message.type !== 'rtc:answer' && message.type !== 'rtc:ice') return;
    const peer = this.peers.get(message.type === 'rtc:answer' ? message.viewerId : message.targetId);
    if (!peer || peer.generation !== message.connectionGeneration) return;
    if (message.type === 'rtc:answer') {
      await peer.pc.setRemoteDescription({ type: 'answer', sdp: message.sdp });
      for (const candidate of peer.ice.splice(0)) await peer.pc.addIceCandidate(candidate || undefined);
    } else if (peer.pc.remoteDescription) await peer.pc.addIceCandidate(message.candidate || undefined);
    else peer.ice.push(message.candidate);
  }

  async replace(stream: MediaStream) {
    await Promise.all([...this.peers.entries()].map(async ([id, p]) => {
      if (p.hasAudio !== (stream.getAudioTracks().length > 0)) { await this.add(id, stream); return; }
      await p.video.replaceTrack(stream.getVideoTracks()[0] || null);
      await p.audio.replaceTrack(stream.getAudioTracks()[0] || null);
    }));
  }

  async quality(preset: QualityPreset) {
    await Promise.all([...this.peers.values()].map(async p => {
      const parameters = p.video.getParameters();
      if (!parameters.encodings?.length) return;
      parameters.encodings[0].maxBitrate = QUALITY_PRESETS[preset].targetBitrateKbps * 1000;
      parameters.encodings[0].maxFramerate = QUALITY_PRESETS[preset].maxFps;
      await p.video.setParameters(parameters);
    }));
  }

  remove(id: string) { this.still.remove(id); const p = this.peers.get(id); if (p) { p.pc.onicecandidate = null; p.pc.onconnectionstatechange = null; p.pc.close(); this.peers.delete(id); } }
  close() { this.still.close(); for (const id of this.peers.keys()) this.remove(id); }
}
