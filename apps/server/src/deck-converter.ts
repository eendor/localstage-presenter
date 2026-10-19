import { spawn } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

function run(command: string, args: string[], timeout = 120000): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
    let errors = '';
    child.stderr.on('data', data => { errors = (errors + data).slice(-4096); });
    const timer = setTimeout(() => { child.kill('SIGKILL'); reject(new Error('Conversion exceeded two-minute time limit')); }, timeout);
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.on('exit', code => { clearTimeout(timer); if (code === 0) resolve(); else reject(new Error(errors || `Converter exited with code ${code}`)); });
  });
}

export async function convertDeck(directory: string, extension: string, maxPages: number): Promise<number> {
  let pdf = path.join(directory, `original.${extension}`);
  if (extension !== 'pdf') {
    const office = process.env.LIBREOFFICE_PATH;
    if (!office) throw new Error('Office conversion requires local LibreOffice. Set LIBREOFFICE_PATH to soffice.exe, or export your deck to PDF.');
    const profile = path.join(directory, 'office-profile');
    await mkdir(path.join(profile, 'user'), { recursive: true });
    await writeFile(path.join(profile, 'user/registrymodifications.xcu'), `<?xml version="1.0"?><oor:items xmlns:oor="http://openoffice.org/2001/registry"><item oor:path="/org.openoffice.Office.Common/Security/Scripting"><prop oor:name="MacroSecurityLevel" oor:op="fuse"><value>3</value></prop></item><item oor:path="/org.openoffice.Office.Common/Load"><prop oor:name="UpdateDocMode" oor:op="fuse"><value>0</value></prop></item></oor:items>`);
    await run(office, [`-env:UserInstallation=${pathToFileURL(profile).href}`, '--headless', '--nologo', '--nodefault', '--norestore', '--convert-to', 'pdf:impress_pdf_Export:{"ExportNotesPages":{"type":"boolean","value":"false"},"ExportHiddenSlides":{"type":"boolean","value":"false"}}', '--outdir', directory, pdf]);
    pdf = path.join(directory, 'original.pdf');
  }
  const renderer = fileURLToPath(new URL('../dist/deck-renderer.js', import.meta.url));
  await run(process.execPath, ['--max-old-space-size=512', renderer, pdf, directory, String(maxPages)]);
  return JSON.parse(await readFile(path.join(directory, 'count.json'), 'utf8'));
}
