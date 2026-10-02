# 550 — Controle da VRAM pela CMOS no Windows (`tools/vram-cmos/`)

> **Data:** 01/10/2026. **Pedido do Daniel:** "precisamos ter esse sistema para controlar a vram no windows também igual é no linux".
> **Base:** [doc 549](549_COLETA_LINUX_CMOS_APCB_CONTROLA_A_VRAM_E_AMDGPU_SEM_IP_DISCOVERY.md). Quem manda na UMA é o bloco APCB da CMOS, não o menu da BIOS (P0).
> **Estado (atualizado 01/10 13:45):** **V1, V2 e V3 VALIDADOS NA PLACA (P0)**, ver §6.1. A leitura de `VramTotalMiB=4096` pelo KMD (parte do V3) sai da Corrida 197.

---

## 1. Uso

Num PowerShell **como Administrador**, na raiz do repositório:

```powershell
# compilar (uma vez, ou depois de mudar a fonte); roda o teste offline antes
tools\vram-cmos\build.bat

# ler o valor atual (igual ao --ler-vram do Linux)
powershell -ExecutionPolicy Bypass -File tools\vram-cmos\bc250-vram.ps1 -Ler

# simular uma troca: mostra o plano, NÃO grava
powershell -ExecutionPolicy Bypass -File tools\vram-cmos\bc250-vram.ps1 -SetUma 4096

# gravar de verdade
powershell -ExecutionPolicy Bypass -File tools\vram-cmos\bc250-vram.ps1 -SetUma 4096 -Confirmo
```

Depois de gravar: **desligar da tomada** e ligar (boot frio). A ABL aplica o pedido e regrava a assinatura dela. Um `-Ler` depois do boot tem de mostrar `ABL_SIGNATURE` com o valor novo.

Cada execução grava a saída em `docs/evidence/vram-cmos/<data>-<máquina>-<modo>.txt`. O arquivo inclui os hashes do `.sys` e do `.exe` e a RAM que o Windows enxerga.

---

## 2. Peças

| Arquivo | Papel |
|---|---|
| `bc250_apcb_memconf.h/.c` | Contrato puro, sem I/O: layout do bloco `0x90..0xAB`, checksum, assinaturas, plano da gravação. Compila igual em user mode e kernel mode. |
| `bc250_apcb_memconf_test.c` | Teste offline com os **dois dumps reais** da placa (`histo.txt`). |
| `bc250cmos_abi.h` | IOCTLs entre a ferramenta e o driver. |
| `bc250cmos_driver.c` → `bc250cmos.sys` | Driver mínimo, sem PnP: só portas `0x72`/`0x73`. Não toca a GPU. |
| `bc250_vram_cli.c` → `bc250-vram.exe` | Linha de comando `--ler` / `--set-uma N [--confirmo]`. Dump no mesmo formato da ferramenta Linux. |
| `bc250-vram.ps1` | Exige admin e `testsigning`, cria o serviço temporário, roda, arquiva e **sempre** remove o serviço (`finally`). |
| `build.bat` | Teste offline primeiro (falhou = nada é gerado), depois CLI, driver e assinatura `BC250-WIN Test`. |

---

## 3. Protocolo (o mesmo que a ABL já aceitou)

**Procedência:**
- `referencias/linux/bc250_memcfg.cpp` (comunidade, `fanoush/bc250_memcfg`): índice `0x72`, dado `0x73`, bloco em `0x90`, `UMA_SIZE` = WORD em `0xAA` em MB, checksum = soma de 16 bits de `0x96..0xAB`;
- gravação com `LINUX_TOOL_SIGNATURE` (`0x42435041`); depois do boot, a releitura mostrou `ABL_SIGNATURE` (`0x4C424124`), ou seja, a ABL consome o pedido (P0, `histo.txt`).

**Vetores reais no teste:**
- dump com 512 MB: checksum `0x03FA`, conferido na mão e no teste;
- dump com 2048 MB: checksum `0x0400`;
- **o plano 512→2048 gerado pelo nosso contrato é byte a byte o bloco que a ABL aceitou**, exceto a assinatura, que a ABL troca.

Ou seja, a ferramenta Windows grava exatamente o que a Linux gravou.

---

## 4. Proteções (Lei 5)

| Etapa | O que acontece |
|---|---|
| **Preflight (user mode)** | Recusa bloco atual com assinatura de erro ou checksum errado. Recusa valor fora de **256..8192 MB** ou fora do passo de **256 MB**, e valor igual ao atual. Sem `-Confirmo`, só simula. |
| **Preflight (kernel)** | Refaz o plano com o **mesmo** contrato e exige que seja idêntico ao da ferramenta. Não confia no buffer de user mode. |
| **Snapshot** | Relê `0x90..0xAB` e exige igualdade com o que a ferramenta leu (comparar-e-trocar). Se mudou, **nada é gravado**. |
| **Apply / readback** | Grava os 28 bytes sob spinlock e relê. |
| **Rollback** | Se a releitura divergir, regrava o bloco original e relê de novo. O resultado diz se a restauração conferiu. |
| **Acesso** | Dispositivo com SDDL só para SYSTEM e Administradores. Serviço criado e apagado a cada execução. |

**Só `UMA_SIZE` muda.** A assinatura e o checksum são consequência. Os timings de memória nunca são tocados; o teste exige isso byte a byte.

**Faixa: 256..8192 MB, passo 256** (o piso subiu de 512 para 256 em 01/10, pela decisão do [doc 551](551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md)):
- 256 é o BAR da placa e o piso da ferramenta Linux: com ele toda a VRAM fica visível à CPU. **Nunca medido nesta placa**; que a ABL aceite é H, e a releitura depois do boot frio decide;
- 512 é o valor de fábrica medido;
- 8192 é o teto do próprio menu da BIOS;
- acima disso não há medição nesta placa.

---

## 5. Provas offline (01/10)

- `build.bat` EXIT 0: teste `PASS (dumps reais 512/2048, plano = gravacao aceita pela ABL, 10 recusas)`; CLI e driver em `/W4 /WX`; `.sys` assinado.
- **Mutação:** mover o início do checksum de `0x96` para `0x94` reprova o teste na linha do `0x03FA`.

---

## 6. Prova em hardware (Lei 3), ainda pendente

| Teste | Passa se | Evidência |
|---|---|---|
| **V1 — leitura** | `-Ler` mostra `ABL_SIGNATURE`, checksum `CONFERE` e `UMA_SIZE=2048`, igual ao último dump do Linux | `docs/evidence/vram-cmos/*-ler.txt` |
| **V2 — gravação** | `-SetUma N -Confirmo` termina em `OK (gravado e relido igual)` com `LINUX_TOOL_SIGNATURE` pendente | `*-set-N.txt` |
| **V3 — aplicação** | Depois do boot frio: `-Ler` mostra `ABL_SIGNATURE` e N; o KMD loga `[M2_SEGMENTO3] VramTotalMiB=N`; a RAM do Windows cai na proporção | `*-ler.txt` + pasta da corrida |

V1 é só leitura e pode rodar agora. V2 e V3 mudam a configuração do firmware: decisão do Daniel, com boot frio depois.

### 6.1 Resultado na placa (01/10, DESKTOP-HQAG6T4)

Binários usados nas três execuções: `bc250cmos.sys` `336001FC…`, `bc250-vram.exe` `201443E2…`.

| Teste | Quando | Medido | Veredito |
|---|---|---|---|
| **V1** | 13:42:14 | `ABL_SIGNATURE`, checksum `CONFERE`, `UMA_SIZE=2048`, igual ao último dump do Linux. RAM do Windows 14.739.120.128 bytes | ✅ |
| **V2** | 13:42:24 | `-SetUma 4096 -Confirmo` → `Resultado do kernel: OK (gravado e relido igual)`, releitura com `LINUX_TOOL_SIGNATURE` e `UMA_SIZE=4096` pendente | ✅ |
| **V3** | 13:45:29, depois do boot frio | `ABL_SIGNATURE`, checksum `0x0408` `CONFERE`, `UMA_SIZE=4096`. RAM do Windows 12.591.636.480 bytes: **exatamente 2 GiB a menos** que no V1 (2048 → 4096 MB de UMA) | ✅ na ferramenta; a parte do KMD (`[M2_SEGMENTO3] VramTotalMiB=4096`) sai da Corrida 197 |

Evidência: `docs/evidence/vram-cmos/2026-10-01_13-42-14-…-ler.txt`, `…13-42-24-…-set-4096.txt` e `…13-45-29-…-ler.txt`.

**Conclusão (P0):** o protocolo funciona igual no Windows e no Linux. O firmware consumiu o pedido gravado pela ferramenta Windows, aplicou o tamanho e regravou a própria assinatura. O checksum do bloco aplicado (`0x0408`) é o que o contrato prevê: `0x0400` com 2048, mais 8 do byte que foi de `0x08` para `0x10`.

---

## 7. Próximo passo possível (não feito)

O KMD poderia ler o bloco APCB no `StartDevice` (só leitura) e logar `[VRAM_CMOS] pedido=N lido_da_placa=M`. Isso detectaria pedido pendente ou não aplicado sem ferramenta externa.
- Ainda não implementado, porque mudaria o pacote 3.343.1.0 já selado para a 197.
- Fica para o candidato seguinte, como instrumentação isolada.
