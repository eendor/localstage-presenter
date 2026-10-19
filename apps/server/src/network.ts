import os from 'os';

export interface NetworkInterface {
  name: string;
  address: string;
  family: 'IPv4' | 'IPv6';
  internal: boolean;
  mac: string;
}

/**
 * Enumerate usable (non-internal, IPv4) network interfaces.
 * Returns a list of interfaces the presenter can choose from.
 */
export function getNetworkInterfaces(): NetworkInterface[] {
  const raw = os.networkInterfaces();
  const results: NetworkInterface[] = [];

  for (const [name, entries] of Object.entries(raw)) {
    if (!entries) continue;
    for (const entry of entries) {
      // Only include external IPv4 interfaces
      if (entry.internal) continue;
      if (entry.family !== 'IPv4') continue;

      results.push({
        name,
        address: entry.address,
        family: 'IPv4',
        internal: false,
        mac: entry.mac,
      });
    }
  }

  return results;
}

/**
 * Get the best default interface address for LAN binding.
 * Prefers Wi-Fi/Wireless adapters, then Ethernet.
 */
export function getDefaultLanAddress(): string | null {
  const interfaces = getNetworkInterfaces();
  if (interfaces.length === 0) return null;

  // Prefer wireless adapters
  const wireless = interfaces.find(
    (i) => /wi-?fi|wireless|wlan/i.test(i.name)
  );
  if (wireless) return wireless.address;

  // Fall back to first available
  return interfaces[0].address;
}
