import { QUALITY_PRESETS, type QualityPreset } from '@localstage/protocol';

export async function capture(preset: QualityPreset): Promise<MediaStream> {
  if (!navigator.mediaDevices?.getDisplayMedia) throw new Error('Screen capture requires desktop Chrome/Edge on the loopback presenter page.');
  const p = QUALITY_PRESETS[preset];
  return navigator.mediaDevices.getDisplayMedia({ video: { width: { ideal: p.width }, height: { ideal: p.height }, frameRate: { ideal: p.maxFps, max: p.maxFps } }, audio: true });
}

export function stopCapture(stream?: MediaStream | null) {
  stream?.getTracks().forEach(track => track.stop());
}

export function createSlate(): MediaStream {
  const canvas = document.createElement('canvas');
  canvas.width = 1280; canvas.height = 720;
  const ctx = canvas.getContext('2d')!;
  ctx.fillStyle = '#0a0a0f'; ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#ffffff'; ctx.font = '36px sans-serif'; ctx.textAlign = 'center';
  ctx.fillText('Presentation paused', 640, 360);
  return canvas.captureStream(1);
}
