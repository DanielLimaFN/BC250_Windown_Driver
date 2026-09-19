# Análise da Corrida das 11:22 e Correção da Sincronização HDP (Candidato 3.126.1.0)

## 1. O Marco Histórico Atingido na Corrida das 11:22

Pela primeira vez na história deste projeto, **pixels reais da área de trabalho do Windows foram compostos, apresentados e exibidos fisicamente no monitor**:
```text
[Present #1] Spread SLOT_SRC  @ 0x15B0000: NonZero=1/256, Xor=A275C718 (Seg=1)
[Present #1] *** DESKTOP REAL DETECTADO NA SUPERFICIE *** NonZero=1/256, Hash=0xA275C718 (Phys=0x15B0000)
[Present #1] BYTE_PROOF PASS (dados conferidos, frame com conteudo): Source=0x15B0000 Dest=0x0 Bytes=11059200
[Present #1] Blt 11059200 bytes (src 0x15B0000 -> scanout 0x0): SDMA=YES
```
O DWM e o Explorer.exe abriram a interface Direct3D 11 na `bc250umd.dll`, criaram o swapchain de 2560x1080 e o SDMA copiou os dados para o scanout GOP. Daniel viu a tela anterior na tela do monitor ("ficou a tela que a gente tava").

---

## 2. Diagnóstico dos Dois Sintomas Observados

### Sintoma A: Por que a imagem ficou "toda distorcida"?
1. **Transição de Modo BasicDisplay -> WDDM**:
   - O Microsoft Basic Display Adapter operava em **1920x1080** com pitch de **7680 bytes** (confirmado no log: `[GetStdAllocData] GdiSurface: 1920x1080, Type=6, Pitch=7680`).
   - O nosso display e o monitor físico operam em **2560x1080** com pitch de **10240 bytes** (`Pitch=10240`).
   - Durante a transição, o DWM ainda transferia superfícies GDI/BasicDisplay com passo de 7680 bytes. Ao copiar de forma linear para uma linha de 10240 bytes, cada linha horizontal sofre um deslocamento de 2560 bytes (640 pixels), gerando o efeito clássico de imagem inclinada/cisalhada ("toda distorcida").
2. **Pipelines 3D na UMD**:
   - O log da UMD registrou: `DrawIndexed: RECUSADO -- compilador/shader-state/draw PM4 ausente (Rejected=1)`.
   - Como o backend de desenho 3D ainda não gera pacotes PM4 de rasterização, o DWM não conseguiu redesenhar todas as janelas em 3D e recorreu à memória de transição.

---

### Sintoma B: Por que deu tela azul (`BugCheck 0x119`, Arg1=2, Arg2=0xC0000185)?
O log do KMD registrou a causa com precisão cirúrgica na submissão FenceId=770:
```text
[SubmitVirtual Node0 SDMA] IB_TRIPOD TIMEOUT: TargetWPTR=1092, WPTR=1092, RPTR=50244, STATUS=46DC6452, FenceExp=00001304, FenceObs=43DAFFFE
[SubmitVirtual Node0 SDMA] FENCE_VISIBILIDADE: antes_hdp=43DAFFFE depois_hdp=00001304 esperado=00001304 | RPTR_writeback=50244 RPTR_reg=50244
```
1. **A GPU havia concluído tudo**:
   - `TargetWPTR = 1092`.
   - `RPTR = 50244`. Como o anel tem 4096 bytes: `50244 = 12 * 4096 + 1092`. O RPTR alcançou o WPTR exato após 12 voltas de anel!
   - A GPU gravou a fence esperada (`0x00001304`) sobre o poison.
2. **O Buffer HDP segurou a visibilidade para a CPU**:
   - A escrita da fence pela GPU fica no buffer de gravação do Host Data Path (HDP).
   - O loop de polling em `bc250_sdma_present.c` ficava lendo a memória RAM sem emitir o flush de HDP.
   - A CPU leu `0x43DAFFFE` (POISON) durante todos os 200.000 polls.
   - Assim que o timeout estourou e chamou `Bc250SdmaPresentHdp(&g_Bc250SdmaPresent)`, a fence apareceu instantaneamente (`depois_hdp = 00001304`).
   - Por ter dado timeout no loop, o driver devolveu `STATUS_IO_DEVICE_ERROR`, o que levou o subsistema de agendamento de vídeo (`dxgmms2`) a disparar o BugCheck `0x119`.

---

## 3. Correções Implementadas

1. **Sincronização Ativa de HDP nos Loops do Tripé**:
   - Em `driver/wdk/bc250_sdma_present.c` (tanto no `Bc250SdmaPresentSubmitVirtualIndirect` quanto no `Bc250SdmaPresentBlt`):
   - Se `fenceObserved != nextFence`, o driver verifica se `Bc250SdmaRptrMatches(rptr, rptrWb, targetWptr)` já é verdadeiro (ou a cada 128 polls).
   - Se o RPTR alcançou o WPTR, a GPU já emitiu o pacote de fence. O driver chama `Bc250SdmaPresentHdp()` imediatamente, forçando o flush do HDP.
   - O fence é lido como válido no primeiro poll, eliminando falsos timeouts e o BugCheck 0x119.
2. **Reconstrução e Incremento de Versão**:
   - `build-contract.bat`: Todos os 42 testes unitários passaram com `EXIT=0`.
   - `build-bc250mcdm-lab-bringup.ps1`: Imagem binária de laboratório compilada com sucesso (`EF0F3D2E...`).
   - Versão do driver no INF incrementada para **`09/10/2026,3.126.1.0`**.
   - Pacote `driver\wdk\package\output_lab` gerado e assinado com catálogo atualizado.
   - Todas as auditorias (`audit-bc250mcdm-lab-package.ps1`, `audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py`, `audit-umd-node1-submission.py`) **PASSARAM**.

---

## 4. Passos para o Próximo Teste (Daniel)

Como a máquina reiniciou por tela azul, o fusível de proteção anti-boot-loop foi acionado para proteger o hardware. É necessário resetar o fusível e depois executar o bind:

### Passo 1: Resetar o fusível (PowerShell como Administrador)
```powershell
powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset
```
*(Deve retornar: `Estado: CLEAN (0)`)*

### Passo 2: Executar o bind do novo pacote (3.126.1.0)
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -EnableRenderNode -EnableD3D11LabPromotion -Execute -IAcceptBlackScreenRisk
```

---

## 🤖 5. Orientações para a Próxima IA (Passagem de Bastão)

### Contexto Normativo e Leis do Projeto
1. **Barra de Progresso:** Toda resposta técnica deve abrir com a barra de progresso oficial contendo medições reais (`AGENTS.md` / `GEMINI.md`).
2. **Empirismo Estrito (Lei 1):** Nada é considerado funcionando até que haja evidência gravada em hardware (log físico com hashes, registros e contadores).
3. **Fail-Closed e Zero Falso Sucesso (Lei 4):** Se um recurso não está pronto, recuse honestamente com log explícito. Nunca minta para o subsistema do Windows.
4. **Regra 10:** O agente **NUNCA executa comandos de instalação/binding de driver** diretamente pelo terminal automatizado (risco de tela preta/queda de conexão). Forneça o comando formatado para o operador Daniel rodar no PowerShell elevado.

### Arquitetura de Memória da BC-250 (Fator Crítico para Não Quebrar)
- **16 GB GDDR6 Unificada (UMA):** CPU e GPU compartilham o mesmo barramento físico.
- **VRAM da BIOS (UMA Framebuffer Size):** Apenas reserva mínima exclusiva para a GPU (visível como Segmento 1). O restante dos 16 GB é RAM do sistema.
- **Segmentos no WDDM:**
  - Segmento 1: VRAM dedicada (carveout local).
  - Segmento 2: GART / Aperture (2048 MB) para paginação de superfícies pelo VidMm quando o Segmento 1 lota.
  - Se o VidMm chamar paginação no Segmento 0 ou 2, `Bc250McdmEncodeVidMmPteWithLevel` **deve** usar `BC250_PTE_SYSTEM | BC250_PTE_SNOOPED`. Qualquer rejeição de parâmetro derruba o kernel com `BugCheck 0x10E`.

### Próximos Passos de Desenvolvimento Após a Corrida:
1. **Conferência Pós-Bind:**
   - Conferir `docs\evidence\...\kmd_display_log.txt`.
   - Verificar se as submissões pós-wrap (`FenceId > 770`) fecharam sem nenhum timeout graças ao flush do HDP.
   - Confirmar ausência de BugCheck 0x119.
2. **Resolução da Distorção de Imagem (Passo de Stride):**
   - O monitor opera em 2560x1080 (pitch 10240). Se o DWM entregar superfície em 1920x1080 (pitch 7680) ou stride divergente:
     - Em [`driver/wdk/bc250_sdma_present.c`](../driver/wdk/bc250_sdma_present.c), a rotina de Blt deve ser estendida para realizar cópia linha por linha respeitando o stride da fonte e do destino (em vez de cópia plana contígua de $Width \times Height \times 4$), ou emitir pacotes de cópia 2D do SDMA.
3. **Evolução do Backend Gráfico D3D11 na UMD (Node 1):**
   - No log da UMD, `DrawIndexed` foi recusado porque ainda não há pipeline PM4 de rasterização.
   - Conectar os comandos de desenho D3D11 aos pacotes PM4 no anel gráfico (MEC/Node 1) para permitir que o DWM desenhe botões, janelas e sombras em aceleração nativa por hardware.

