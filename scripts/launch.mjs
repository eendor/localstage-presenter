import { spawn } from 'node:child_process';
const child = spawn(process.execPath, ['apps/server/dist/index.js'], {
  stdio: 'inherit', env: { ...process.env, OPEN_BROWSER: '1' },
});
child.on('exit', code => { process.exitCode = code || 0; });
process.on('SIGINT', () => child.kill('SIGINT'));
process.on('SIGTERM', () => child.kill('SIGTERM'));
