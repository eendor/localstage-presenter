<div align="center">

# 🎤 LocalStage

**Your slides on every phone in the room. No internet needed.**

[![Release](https://img.shields.io/github/v/release/eendor/localstage-presenter?style=for-the-badge&color=5b7cfa)](https://github.com/eendor/localstage-presenter/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/eendor/localstage-presenter/release.yml?style=for-the-badge&label=build)](https://github.com/eendor/localstage-presenter/actions)
[![Downloads](https://img.shields.io/github/downloads/eendor/localstage-presenter/total?style=for-the-badge&color=2ea44f)](https://github.com/eendor/localstage-presenter/releases)
<br>
![Windows](https://img.shields.io/badge/Windows-0078D6?style=flat-square&logo=windows&logoColor=white)
![macOS](https://img.shields.io/badge/macOS-000000?style=flat-square&logo=apple&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-FCC624?style=flat-square&logo=linux&logoColor=black)
![Written in C](https://img.shields.io/badge/written%20in-C-A8B9CC?style=flat-square&logo=c&logoColor=white)
![No accounts](https://img.shields.io/badge/accounts-none-ff69b4?style=flat-square)
![Works offline](https://img.shields.io/badge/internet-optional-orange?style=flat-square)

<img src="docs/images/presenter.png" alt="Presenter page" width="68%">
<img src="docs/images/join-phone.png" alt="Viewer join page on a phone" width="24%">

</div>

## 🚀 Get started in 30 seconds

1. **[Download](https://github.com/eendor/localstage-presenter/releases/latest)** the file for your computer and unzip it.
2. Start it:
   - 🪟 Windows: double-click `run-localstage.bat`
   - 🍎 Mac: double-click `start.command`
   - 🐧 Linux: run `./start.sh`
3. Show the QR code to your audience. They scan it, and they're in. 🎉

> 💡 To show PDF slides, install [Poppler](https://poppler.freedesktop.org/). For PowerPoint, also install [LibreOffice](https://www.libreoffice.org/).

## 🤔 What it's for

LocalStage turns your laptop into a small presentation server for the room you're in. You start it and get a link and a QR code. People scan it with their phone or laptop and see your slides or your screen live in their browser. Nothing leaves your local network.

It's meant for situations where the usual options don't fit:

- **No projector, or a bad one.** Everyone follows along on their own screen instead of squinting at the wall.
- **No internet, or internet you can't trust.** Video calls and cloud slide links need the internet. LocalStage only needs a shared Wi-Fi or hotspot, even one with no internet connection.
- **No sign-ups.** Viewers don't install an app or create an account. They open a link.
- **Privacy.** Your slides and screen go straight from your computer to the viewers' browsers over the local network. No third-party server sees them.

## 🎯 Use cases

- **Classrooms and workshops:** students follow slides on their phones and can flip back to an earlier slide when you allow it.
- **Meetups and small talks:** in a café, a meeting room or a hackathon without an HDMI cable.
- **Team meetings:** walk through a document or a demo on your screen without setting up a call.
- **Offline events:** field trainings, rural schools, or venues with no internet, using a phone hotspot.
- **Accessibility:** people at the back of the room or with low vision can zoom in on their own device.
- **Rehearsing:** read your speaker notes in the presenter script panel while viewers see only the slides.

## 🎬 Two ways to present

| Mode | What viewers see | When to use it |
|---|---|---|
| **Upload a deck** (PDF, PPTX, PPT) | Sharp slide images, which you advance from your laptop | Slide presentations. No screen capture is needed and it uses very little bandwidth. Portrait PDFs are fitted automatically. |
| **Share screen** | A live video of a window or your whole screen | Live demos, documents, anything that isn't a slide deck. |

## ⚠️ Good to know

- The presenter and viewers must be on the same network.
- Up to 50 viewers. Screen sharing sends a separate video stream to each viewer, so with many viewers a deck works better.

## 📡 Trouble connecting?

See **[docs/NETWORK.md](docs/NETWORK.md)** for firewall and Wi-Fi tips.

## 🛠️ For developers

```
src/      C server
web/      the browser page (built into the server)
tests/    end-to-end tests  ->  make test
scripts/  launchers
docs/     guides and screenshots
```

Building from source, settings and how it works inside: see **[docs/TECHNICAL.md](docs/TECHNICAL.md)**.

## 🙏 Credits

- Original app: [dizshh/localstage-presenter](https://github.com/dizshh/localstage-presenter). This fork rewrites it in C.
- Live video: [WebRTC](https://webrtc.org/), built into every modern browser.
- PDF slides: [Poppler](https://poppler.freedesktop.org/) (`pdftoppm`).
- PowerPoint slides: [LibreOffice](https://www.libreoffice.org/).
- Badges: [Shields.io](https://shields.io/).

<div align="center"><sub>Made for rooms with bad projectors and no Wi-Fi password. ✨</sub></div>
