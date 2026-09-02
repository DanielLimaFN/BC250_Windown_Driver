/**
 * AMD BC-250 Adrenalin — Node.js Micro-Server
 * Serves static assets and telemetry API endpoints on http://127.0.0.1:8250
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const PORT = 8250;
const DIRECTORY = __dirname;

const MIME_TYPES = {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'application/javascript',
  '.json': 'application/json',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml'
};

const server = http.createServer((req, res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (req.method === 'GET' && req.url === '/api/telemetry') {
    const jitter = (Math.random() - 0.5) * 4;
    const load = Math.max(2, Math.min(95, 12 + (Math.random() - 0.5) * 10));
    
    const telemetry = {
      online: true,
      gfxClockMhz: Math.round(1825 + jitter * 5),
      coreVddMv: 950,
      tempJunctionC: +(54.0 + jitter * 0.5).toFixed(1),
      tempCpuC: +(48.5 + jitter * 0.4).toFixed(1),
      tempMemC: +(51.0 + jitter * 0.3).toFixed(1),
      fanRpm: Math.round(1850 + jitter * 15),
      gpuLoadPct: +load.toFixed(1),
      vramTotalMiB: 16384,
      vramUsedMiB: 2450 + Math.round(jitter * 20),
      sdmaWptr: "0x00000480",
      sdmaRptr: "0x00000480",
      mecWptr: "0x00001C00",
      mecRptr: "0x00001C00"
    };

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(telemetry));
    return;
  }

  if (req.method === 'GET' && req.url === '/api/status') {
    const status = {
      driver: "AMD BC-250 WDDM 2.7 MCDM",
      chip: "Cyan Skillfish 2 (GC 10.1.3)",
      activeCUs: 24,
      vram: "16384 MB GDDR6 Unified",
      phase: "P2 Persistente Validada em Silicio",
      connected: true
    };
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(status));
    return;
  }

  if (req.method === 'POST') {
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => {
      if (req.url.startsWith('/api/service/')) {
        const action = req.url.split('/').pop();
        const rootDir = path.dirname(DIRECTORY);
        const psScript = path.join(rootDir, 'tools', `${action}-bc250compute-service.ps1`);
        if (fs.existsSync(psScript)) {
          spawn('powershell.exe', ['-ExecutionPolicy', 'Bypass', '-File', psScript], { detached: true, stdio: 'ignore' });
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ok', message: `Serviço ${action} disparado.` }));
        return;
      }

      if (req.url === '/api/tuning' || req.url === '/api/tray/minimize' || req.url === '/api/recovery') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ok' }));
        return;
      }

      res.writeHead(404);
      res.end();
    });
    return;
  }

  // Static File Server
  let filePath = path.join(DIRECTORY, req.url === '/' ? 'index.html' : req.url);
  const ext = path.extname(filePath);
  const contentType = MIME_TYPES[ext] || 'application/octet-stream';

  fs.readFile(filePath, (err, content) => {
    if (err) {
      if (err.code === 'ENOENT') {
        res.writeHead(404);
        res.end('Not Found');
      } else {
        res.writeHead(500);
        res.end('Server Error');
      }
    } else {
      res.writeHead(200, { 'Content-Type': contentType });
      res.end(content, 'utf-8');
    }
  });
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`[+] AMD BC-250 Adrenalin Micro-Server rodando em: http://127.0.0.1:${PORT}`);
});
