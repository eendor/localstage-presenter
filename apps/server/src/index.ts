import express from 'express';
import { createServer } from 'http';
import path from 'path';
import { fileURLToPath } from 'url';
import { getNetworkInterfaces, getDefaultLanAddress } from './network.js';
import { setupHttpRoutes } from './http.js';
import { setupSignaling } from './signaling.js';
import { SessionStore } from './session-store.js';
import { HostAuth } from './auth.js';
import { spawn } from 'child_process';
import { DeckStore } from './decks.js';
import { setupSlideAccess } from './slide-access.js';

const port = Number(process.env.PORT || 3000);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid PORT');
const store = new SessionStore();
const auth = new HostAuth();
const app = express();
const decks = new DeckStore();
let viewerAuthorized = (_token: string) => false;
let selectedAddress = getDefaultLanAddress();
let switching = false;
const server = createServer(app);
const lanServer = createServer(app);
async function bindLan(address: string | null) {
  if (switching) throw new Error('Network reconfiguration already in progress');
  switching = true;
  try {
    if (lanServer.listening) { lanServer.closeAllConnections(); await new Promise<void>(resolve => lanServer.close(() => resolve())); }
    selectedAddress = null;
    if (address) await new Promise<void>((resolve, reject) => {
      const failed = (error: Error) => reject(error);
      lanServer.once('error', failed);
      lanServer.listen(port, address, () => { lanServer.off('error', failed); selectedAddress = address; resolve(); });
    });
  } finally { switching = false; }
}
setupHttpRoutes(app, path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../web/dist'), store, auth, () => {
  decks.setup(app, auth); setupSlideAccess(app, decks, auth, token => viewerAuthorized(token));
}, { info: () => ({ selectedAddress, port }), select: async address => { if (address !== selectedAddress) { closeSignaling.end(); await bindLan(address); } } });
const closeSignaling = setupSignaling([server, lanServer], store, auth, decks, check => { viewerAuthorized = check; });
server.on('error', error => { console.error(error.message); process.exitCode = 1; closeSignaling(); });
server.listen(port, '127.0.0.1', () => {
  const url = `http://127.0.0.1:${port}/host#${auth.getBootstrapSecret()}`;
  console.log(`Presenter (one-use link, expires in 5 minutes): ${url}`);
  for (const adapter of getNetworkInterfaces()) console.log(`${adapter.name}: http://${adapter.address}:${port}/join`);
  if (process.env.OPEN_BROWSER === '1') {
    const command = process.platform === 'win32' ? 'explorer.exe' : process.platform === 'darwin' ? 'open' : 'xdg-open';
    spawn(command, [url], { stdio: 'ignore' }).on('error', () => console.log('Open the presenter link above manually.'));
  }
});
void bindLan(selectedAddress).catch(console.error);
const networkTimer = setInterval(() => {
  if (!switching && selectedAddress && !getNetworkInterfaces().some(a => a.address === selectedAddress)) {
    closeSignaling.end(); void bindLan(getDefaultLanAddress()).catch(console.error);
  }
}, 5000);
const shutdown = () => {
  clearInterval(networkTimer);
  closeSignaling();
  store.endAllSessions();
  server.close();
  server.closeAllConnections();
  lanServer.close(); lanServer.closeAllConnections();
};
process.once('SIGINT', shutdown);
process.once('SIGTERM', shutdown);
