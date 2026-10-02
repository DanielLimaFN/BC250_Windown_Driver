# 562 — Corrida 203: X2b válido, travou igual — o `CONST_FILL` não é a causa

**Data:** 02/10/2026, bind às 09:50:57 · **Candidato:** lab **3.350.1.0** (SYS `586E3E4F…`) · **CMOS:** 256 MB · **Boot:** frio
**Evidência:** `docs/evidence/2026-10-02_09-50-57-DESKTOP-HQAG6T4-lab-binding/`
- Cauda do KMD e rastro recuperados com SHA-256 (`SHA256-recuperados.txt`).
- **Logs da UMD pós-trava ainda não coletados:** falta rodar `coletar-pos-trava.ps1`.

**Critério:** [doc 561](561_X2B_ZERAR_POR_COPIA_EM_VEZ_DE_CONST_FILL_CRITERIO_CORRIDA203.md) §3.

---

## 1. O que aconteceu (P0)

- O Daniel abriu o menu Iniciar e a máquina travou.
- O log do KMD cobre ~100,4 s: `qpc` de 245,96 s a 346,32 s, 28.742 linhas.
- `[SDMA_CG] INIT … RESTAURADOS`; BCG1 com `SDMA0_CLK_CTRL=0x000C0100` até o fim.
- **Ensaio válido:**
  - 504 `op=FILL_POR_COPIA` e 503 `[X2B_FILL] FONTE` com `Confere=1`;
  - zero `Confere=0`, zero `FONTE DIVERGENTE`, zero `SEM DmaBufferGpuVirtualAddress`;
  - **zero `op=FILL ` em `CONST_FILL`**. Nenhum zeramento por `CONST_FILL` na corrida inteira;
  - `WriteOff+DmaSize=0x10000` em toda linha conferida: o `DmaBufferWriteOffset` é o deslocamento no buffer de 64 KiB (P0, confirma a leitura P1 do contrato);
  - 77 multipasses legais por falta de espaço para a região de zeros.
- **Os FILLs por cópia executaram:** IBs de até 40.416 bytes, `RPTR==WPTR` e fence sobre poison.
- **Ausências:** zero TDR, zero `IB_TRIPOD TIMEOUT`, zero `GFXHUB_FAULT`, zero `NO_SPACE`.
- **Calor:** Tctl entre 74,5 e 81,5 °C (último 80,0).
- **Rastro durável, última amostra (346,07 s, ~250 ms antes do fim do log):**
  - SDMA0 ocioso, anel drenado (`RPTR=WPTR=0x1BB14`), fora de IB;
  - GFX ocioso (`CP_RB0 RPTR=WPTR=0x1810`, fence em dia).

## 2. Os últimos ~4 ms (P0, linhas cruas)

1. `DX93_ALLOC_MAP` de uma alocação nova de 64 KiB, segmento 1, CPU visível.
2. **Submit gráfico com troca de VMID 6 → 1** (`VMID_SWITCH_VM_FLUSH #15`, `REQ=0x00F80002 ACK_MASK=0x2`, `WaitFence=394`), aceito em −2,4 ms. **Não foi retirado** antes da parada.
3. Construído o `FILL_POR_COPIA` da alocação nova (64 KiB → `Dst=0x240000`, fonte `0x227DE0`).
4. Começa a submissão do FILL:
   - `CTX_PROBE`;
   - `[PT_WALK] RootMC=…`.
5. **Fim do log** no meio da caminhada da tabela de páginas pela CPU (leitura de VRAM pela BAR), **antes** do `KMD_SDMA_SUBMIT` desse FILL. A cópia de zeros **não chegou a ser entregue à engine**.

## 3. Veredito pelo critério escrito antes

**Desfecho B, pela letra:** ensaio válido, trava, e o último trabalho de paginação é um `FILL_POR_COPIA`.

**Implicação que se sustenta (P0):** **H1 está refutada como causa suficiente.** A máquina travou numa corrida sem um único `CONST_FILL` de zeramento. O pacote não era o gatilho.

**Ressalva que manda na leitura (Lei 1):** o doc 561 ligava B a "o destino, a primeira escrita da GPU" (H2). Nesta corrida o FILL **ainda não tinha sido submetido** quando tudo parou. A escrita da GPU na alocação nova não aconteceu. Esta amostra **não sustenta H2 como formulada**.

O que se repete nas quatro travas com 256 MB (199, 200, 202 e 203) é mais largo: **o ciclo de criação de uma alocação nova.**
- O que sempre está presente: mapa, tabela de páginas e zeramento.
- O que varia de trava para trava:
  - o trabalho da GPU em voo: nada, um IB gráfico já retirado, ou um IB gráfico com troca de VMID ainda em voo;
  - a última atividade da CPU: esperando paginação na 199, lendo pela BAR o buffer recém-zerado na 200 e na 202, caminhando a tabela de páginas pela BAR na 203.

## 4. Hipótese para minerar antes de qualquer corrida (H, não P0)

Em três das quatro travas, a última atividade conhecida da CPU é uma **leitura de VRAM pela BAR** concorrente com trabalho da GPU recém-submetido:
- 200 e 202: grade de 86.400 amostras do DWM;
- 203: caminhada da tabela de páginas.

A série 166–172 tinha leituras de canários pela BAR como última migalha em 170 e 171. A 172 é contraexemplo pelo lado do KMD (última operação: leitura do registrador RPTR). Mas leituras de modo usuário, do DWM, não deixam migalha.

Isso **não** é a hipótese "leitura pela BAR" já refutada: aquela removeu só os canários do KMD (X3/X4). A caminhada da tabela pela CPU e as leituras do DWM continuaram.

Antes de propor ensaio: minerar o corpus de travas atrás da última leitura de VRAM pela BAR de **qualquer** origem, contra as janelas equivalentes que não travaram.

## 5. Estado da máquina e pendências

- BC-250 em **Código 43** (`CM_PROB_FAILED_POST_START`), recusada pelo fusível depois do reboot das 09:53. O watchdog ainda não rodou a recuperação.
- **Pendente:**
  - rodar `tools\coletar-pos-trava.ps1` elevado, para os logs da UMD (inclusive se houve `res=-2`) e o motivo do reset;
  - recuperação antes de novo bind.
- O X2b fica no código por ora (correto e sem regressão medida). Reverter ou manter é decisão separada.

**NÃO VALIDADO EM HARDWARE:** desktop estável com 256 MB.
