// Isolated process: large or malformed documents cannot block the signaling loop.
import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { createCanvas } from '@napi-rs/canvas';
import { getDocument } from 'pdfjs-dist/legacy/build/pdf.mjs';
import { createRequire } from 'node:module';

async function render() {
  const [input, directory, limit] = process.argv.slice(2);
  const assets = path.dirname(createRequire(import.meta.url).resolve('pdfjs-dist/package.json'));
  const toUrl = (folder: string) => {
    const full = path.resolve(assets, folder).replace(/\\/g, '/');
    return full.endsWith('/') ? full : full + '/';
  };
  const loading = getDocument({ data: new Uint8Array(await readFile(input)), useSystemFonts: true, disableFontFace: true,
    standardFontDataUrl: toUrl('standard_fonts'), cMapUrl: toUrl('cmaps'), cMapPacked: true, wasmUrl: toUrl('wasm'),
  });
  const document = await loading.promise;
  if (await document.getPermissions() !== null) throw new Error('Encrypted or password-protected PDFs are unsupported');
  if (document.numPages > Number(limit)) throw new Error(`Deck exceeds ${limit} slides`);
  for (let index = 1; index <= document.numPages; index++) {
    const page = await document.getPage(index);
    const base = page.getViewport({ scale: 1 });
    const viewport = page.getViewport({ scale: Math.min(1920 / base.width, 1080 / base.height, 2) });
    const canvas = createCanvas(Math.ceil(viewport.width), Math.ceil(viewport.height));
    await page.render({ canvasContext: canvas.getContext('2d') as any, viewport, canvas: canvas as any }).promise;
    await writeFile(path.join(directory, `${index}.png`), await canvas.encode('png'));
    page.cleanup();
  }
  await writeFile(path.join(directory, 'count.json'), JSON.stringify(document.numPages));
  await loading.destroy();
}
render().catch(error => { console.error(error.message); process.exitCode = 1; });
