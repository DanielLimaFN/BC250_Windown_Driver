# Guia de Teste e Instalação em Hardware Real: Driver MCDM BC-250

Este documento fornece as instruções passo a passo para instalar, validar e diagnosticar o **Driver Ativo MCDM (`bc250mcdm`)** no hardware AMD BC-250 (`PCI\VEN_1002&DEV_13FE`).

---

## 1. Pacote de Produção Ativo

O pacote ativo assinado digitalmente está localizado em:
```
driver\wdk\package\output_active\
├── bc250mcdm.inf    (Descritor INF de ComputeAccelerator)
├── bc250mcdm.sys    (Binário de Kernel Miniport Ativo)
└── bc250mcdm.cat    (Catálogo de Segurança Assinado Authenticode)
```

---

## 2. Passo a Passo de Instalação no Windows

Abra um terminal PowerShell ou Prompt de Comando **como Administrador** e execute:

### Passo 1: Instalação do Pacote Ativo
```powershell
pnputil /add-driver "c:\Users\Daniel\Desktop\NamixWDDM\driver\wdk\package\output_active\bc250mcdm.inf" /install
```

### Passo 2: Verificação do Dispositivo
Abra o Gerenciador de Dispositivos ou execute no PowerShell:
```powershell
Get-PnpDevice -FriendlyName "*BC-250*"
```
O dispositivo deverá constar como:
- **Nome**: `BC-250 Active Compute Accelerator (MCDM)`
- **Classe**: `ComputeAccelerator`
- **Status**: `OK` (Código 0)

### Passo 3: Inspeção de Telemetria no Registro
Consulte os registros de hardware capturados pelo driver:
```powershell
Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe"
```

---

## 3. Procedimento de Rollback / Desinstalação (Segurança)

Caso deseje desinstalar ou reverter para o estado limpo a qualquer momento:

```powershell
# Localizar o OEM INF publicado:
$oem = (pnputil /enum-drivers | Select-String -Pattern "bc250mcdm.inf" -Context 1,0 | Out-String)
# Exemplo de remoção forçada:
pnputil /delete-driver oemXX.inf /uninstall /force
```

Ou execute o script de recuperação em caso de necessidade:
```cmd
C:\BC250_RECOVERY.cmd
```
