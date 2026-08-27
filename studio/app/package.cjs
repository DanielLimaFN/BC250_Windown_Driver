const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

console.log('=============================================================');
console.log(' Empacotando BC-250 Studio como Executavel Nativo Windows');
console.log('=============================================================\n');

console.log('[1/4] Compilando interface React (Vite + TypeScript)...');
execSync('npm run build', { stdio: 'inherit' });

console.log('\n[2/4] Preparando pasta standalone dist-win...');
const distWin = path.join(__dirname, 'dist-win');
const electronDist = path.join(__dirname, 'node_modules', 'electron', 'dist');

if (!fs.existsSync(electronDist)) {
  console.error('ERRO: Pasta electron/dist nao encontrada em node_modules.');
  process.exit(1);
}

if (fs.existsSync(distWin)) {
  fs.rmSync(distWin, { recursive: true, force: true });
}
fs.mkdirSync(distWin, { recursive: true });

// Copy electron runtime files
fs.cpSync(electronDist, distWin, { recursive: true });

// Rename electron.exe to BC250Studio.exe
const oldExe = path.join(distWin, 'electron.exe');
const newExe = path.join(distWin, 'BC250Studio.exe');
if (fs.existsSync(oldExe)) {
  fs.renameSync(oldExe, newExe);
}

console.log('[3/4] Copiando aplicativo para resources/app...');
const appResources = path.join(distWin, 'resources', 'app');
fs.mkdirSync(appResources, { recursive: true });

fs.copyFileSync(path.join(__dirname, 'package.json'), path.join(appResources, 'package.json'));
fs.cpSync(path.join(__dirname, 'electron'), path.join(appResources, 'electron'), { recursive: true });
fs.cpSync(path.join(__dirname, 'dist'), path.join(appResources, 'dist'), { recursive: true });

console.log('[4/4] Compilando launcher nativo BC250Studio.exe...');
const csc = 'C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\csc.exe';
if (fs.existsSync(csc)) {
  try {
    execSync(`"${csc}" /target:winexe /optimize+ /platform:x64 /out:"${path.join(__dirname, 'BC250Studio.exe')}" /r:"C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll" /r:"C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.Windows.Forms.dll" "${path.join(__dirname, 'Launcher.cs')}"`, { stdio: 'inherit' });
  } catch (e) {
    console.warn('Aviso ao compilar launcher:', e.message);
  }
}

console.log('\n=============================================================');
console.log(' [SUCESSO] Aplicativo executavel (.exe) criado com exito!');
console.log(' - Executavel Standalone: dist-win\\BC250Studio.exe');
console.log(' - Launcher Raiz: BC250Studio.exe');
console.log('=============================================================\n');
