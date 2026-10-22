# Network Setup & Troubleshooting

## Supported Arrangements

| Arrangement | How It Works | What to Check |
|-------------|-------------|---------------|
| Shared router | Laptop and viewers on the same router | Devices must be allowed to communicate |
| Laptop hotspot | Laptop hosts both app and hotspot | Hotspot adapter address, firewall, client capacity |
| Phone hotspot | Laptop and viewers connect to the phone | Hotspot must allow client-to-client traffic |
| Wired + Wi-Fi | Laptop on Ethernet, viewers on Wi-Fi | Both must be in reachable network segments |

## Common Issues

### "Page won't open" — Can't reach the server

1. **Check the address:** Are you using the correct LAN IP shown by LocalStage? `localhost` on a phone refers to the phone, not the presenter's laptop.
2. **Same network:** Ensure all devices are on the same Wi-Fi or hotspot.
3. **Firewall:** The presenter's firewall must allow incoming TCP connections on port 3000.
   - **Windows:** Allow localstage.exe through Windows Firewall → Inbound Rule for port 3000.
4. **Client isolation:** Some routers and guest Wi-Fi networks block device-to-device traffic. Try a different network or the laptop's hotspot.
5. **VPN:** A running VPN may route traffic away from the local network. Disconnect it.

### "Page opens but video won't connect" — WebRTC issues

1. **WebRTC ports:** Video uses dynamically negotiated UDP/TCP ports beyond just port 3000. Firewall rules must allow these.
2. **Browser mDNS/privacy:** Some browsers anonymize local network candidates. Test with Chrome or Edge first.
3. **Network segmentation:** VLANs or subnets may prevent direct peer-to-peer connections even when the web page loads.
4. **Browser permissions:** Check for local network access prompts (Chrome may ask for permission).

### Windows Firewall Setup

1. Open "Windows Defender Firewall with Advanced Security"
2. Click "Inbound Rules" → "New Rule..."
3. Select "Port" → "TCP" → enter `3000`
4. Allow the connection
5. Apply to Private/Domain networks (not Public unless intended)
6. Name it "LocalStage"

> **Note:** This opens port 3000 for the web server. WebRTC media is sent by the
> presenter's **browser**, not the LocalStage server. Allow the presenter browser's media traffic
> on the Private network profile, with the remote scope restricted to the local
> subnet. Keep the firewall enabled.

### Adapter selection and address changes

The authenticated presenter page lists actual IPv4 adapters, the selected address,
port and QR code. The server listens on `127.0.0.1` and only the selected LAN IPv4
address. Changing the dropdown ends the current session and rebinds the viewer
listener. If the selected IP disappears, the server ends the session and selects
an available adapter; start a new session and redistribute its address.
The HTTP listener binding does not constrain WebRTC's browser-managed ICE adapters.

### Hotspot Notes

- **Windows hotspot:** The hotspot adapter typically uses `192.168.137.x`. LocalStage will detect and show the correct address.
- **Phone hotspot:** Most phone hotspots allow client-to-client traffic, but some restrict it. If video fails, try a router instead.
- **Client limit:** Hotspots typically support 8-10 clients. The actual streaming capacity depends on the Wi-Fi link quality.

## Security Notes

- The initial version uses **plain HTTP** — suitable for trusted local networks only.
- WebRTC encrypts media transport (DTLS-SRTP), but the web page and signaling are unencrypted.
- For stronger protection, deploy with HTTPS/WSS using locally trusted certificates.
- Never disable browser security or bypass capture permissions.
