# AMD Radeon Software Adrenalin — BC-250 Edition

> **Official Management & Telemetry Control Center for AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2) on Windows 11**

---

## 🌍 Multi-Language Internationalization (i18n)

The application supports **4 languages** and is designed to start by default in **English**, automatically adapting if the user's operating system is set to another supported language:

1. 🇬🇧 **English (`en`)** — Default startup language.
2. 🇷🇺 **Русский (`ru`)** — Полный перевод интерфейса и телеметрии.
3. 🇪🇸 **Español (`es`)** — Traducción completa de paneles y donación.
4. 🇧🇷 **Português (`pt-BR`)** — Tradução nativa completa.

*The user can switch languages at any time using the global language selector in the top bar, with preferences saved in `localStorage`.*

---

## 🎨 Design & Technologies

- **Radeon Light Theme**: Clean, sleek light theme inspired by AMD Adrenalin (pure white `#FFFFFF`, silver `#F8FAFC`, and AMD Crimson Red `#ED1C24` accents).
- **Frontend Stack**: Tailwind CSS + ApexCharts + Lucide Icons.
- **Desktop Host**: Native Windows WebView2 application with System Tray integration (`BC250_Adrenalin.exe`).
- **Startup Donation Popup**:
  1. **1st Option**: Buy Me a Coffee / Credit Card (Stripe / PayPal)
  2. **2nd Option**: Cryptocurrencies (BTC, ETH, USDT, SOL)
  3. **3rd Option**: PIX (Brazil - QR Code & Copy-Paste Key)

---

## 🚀 How to Run

### 1-Click Launch (Recommended)
Double click:
```cmd
start_adrenalin.bat
```
or run:
```cmd
BC250_Adrenalin.exe
```

### Manual Server Launch
With Python:
```bash
python server.py
```
With Node.js:
```bash
node server.js
```
Then open: `http://127.0.0.1:8250`
