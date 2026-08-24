# Subsistema de Áudio OpenAL para AMD BC-250 (Cyan Skillfish / Oberon)

## 1. Visão Geral

Este módulo implementa a stack de áudio **OpenAL Soft 1.23+** com aceleração espacial 3D e compatibilidade total para a placa **AMD BC-250** (APU semi-custom Oberon / Ariel do PS5 — GFX1013).

Projetado especificamente para resolver o problema clássico de placas de mineração sem codec de áudio analógico integrado, onde aplicações como **Minecraft Java Edition (LWJGL 2 / LWJGL 3 com PaulsCode SoundSystem)**, **RPCS3**, **Ryujinx**, **FurMark** e jogos 3D falham ao tentar inicializar o motor de som.

---

## 2. Diagnóstico Técnico do Erro `0x80070490`

### O Problema Original
Quando o Minecraft Java Edition inicializa o subsistema de áudio no Windows, o LWJGL executa `AL.create()`, que invoca internamente `alcOpenDevice(NULL)`.

No Windows 10/11, o OpenAL Soft tenta acessar a API do Windows Audio Session (**WASAPI / MMDevApi**). Em placas como a AMD BC-250, que frequentemente operam em modo headless (sem monitor HDMI com alto-falante ou caixa de som P2 plugada):
```
AL lib: (EE) MMDevApiOpenPlayback: Device init failed: 0x80070490
Error in class 'LibraryLWJGLOpenAL'
    Unable to initialize OpenAL. Probable cause: OpenAL not supported.
    ERROR MESSAGE: Could not locate OpenAL library.
```

O código de erro `0x80070490` é o código de retorno Win32 `ERROR_NOT_FOUND` (`E_NOTFOUND`), indicando que o Windows não encontrou nenhum endpoint de reprodução padrão ativo. A tentativa falha, `alcOpenDevice` retorna `NULL` e o motor de som do jogo aborta com erro.

### A Solução Implementada
1. **Configuração Global `alsoft.ini` Resiliente**:
   Configurado com `drivers = -wasapi,null`. Quando a placa estiver sem dispositivo de som físico, o OpenAL Soft recorre instantaneamente ao backend `null`. O dispositivo é criado com sucesso, buffers de som são processados e o jogo roda com 100% de estabilidade sem travar ou emitir mensagens de erro.
2. **Suporte Plug-and-Play para Áudio Real**:
   Caso você conecte um fone de ouvido USB, monitor HDMI com áudio, fone Bluetooth ou instale o cabo virtual (`Install-VirtualAudio.ps1`), o som real é transmitido normalmente.
3. **Instalação Global em 64-bit e 32-bit**:
   Instala `OpenAL32.dll` e `soft_oal.dll` em `System32` e `SysWOW64`, registrando as chaves no Registro do Windows.

---

## 3. Estrutura de Arquivos

| Arquivo | Descrição |
| :--- | :--- |
| [x64/OpenAL32.dll](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/x64/OpenAL32.dll) | Biblioteca OpenAL Soft nativa 64-bit para `System32`. |
| [x64/soft_oal.dll](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/x64/soft_oal.dll) | Alias do OpenAL Router 64-bit. |
| [x86/OpenAL32.dll](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/x86/OpenAL32.dll) | Biblioteca OpenAL Soft nativa 32-bit para `SysWOW64`. |
| [x86/soft_oal.dll](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/x86/soft_oal.dll) | Alias do OpenAL Router 32-bit. |
| [alsoft.ini](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/alsoft.ini) | Arquivo de configuração mestre com perfil de latência baixa, HRTF e fallback headless. |
| [Install-OpenAL.ps1](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/Install-OpenAL.ps1) | Script PowerShell de instalação e configuração do OpenAL. |
| [Register-OpenAL.bat](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/Register-OpenAL.bat) | Instalador com 1 clique e elevação automática para Administrador. |
| [Test-OpenAL.ps1](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/Test-OpenAL.ps1) | Suite de diagnóstico para verificar inicialização de contexto e fontes de som. |
| [Install-VirtualAudio.ps1](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/openal/Install-VirtualAudio.ps1) | Assistente para download e instalação do driver de cabo virtual (VB-Audio CABLE). |

---

## 4. Instruções de Instalação e Uso

### Instalação Rápida:
1. Abra o PowerShell como Administrador ou execute `Register-OpenAL.bat`.
2. Para instalar o driver completo (Vulkan + OpenGL + OpenAL de uma só vez), execute [Install-BC250-All-Drivers.ps1](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/Install-BC250-All-Drivers.ps1) na raiz de `BC250-V2`.

### Validação do Funcionamento:
Execute o script de teste:
```powershell
powershell -ExecutionPolicy Bypass -File .\Test-OpenAL.ps1
```
A saída esperada deve exibir:
```
[APROVADO] O OpenAL Soft está 100% operacional no sistema AMD BC-250!
Minecraft Java Edition e outros jogos iniciarão seus motores de som perfeitamente.
```

---

## 5. Como Ter Áudio Real na AMD BC-250

Como a placa de mineração BC-250 não possui conector P2 tradicional:
1. **Fone ou Adaptador USB (Recomendado)**: Conecte qualquer fone USB ou placa de som USB simples de R$ 15.
2. **Fone Bluetooth**: Conecte um dongle USB Bluetooth 5.0 e pareie um fone ou caixa Bluetooth.
3. **Cabo de Áudio Virtual**: Execute `Install-VirtualAudio.ps1` para criar uma placa de som virtual. O áudio do jogo pode ser transmitido via Discord, Moonlight/Sunshine ou gravado no OBS Studio.
