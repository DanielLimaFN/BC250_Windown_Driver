# 493 — Corrida 149 (M1): 600 s sem trava no roteiro que travava; DX.91 agiu pela primeira vez; despejo não caiu; harness perdeu o Present

> **Corrida:** 28/09/2026. Bind às 07:48:26 após boot frio, sem KD. Rollback online (`RECOVERY_OK_ONLINE`, rodada 2). **Candidato:** M1 / 3.299.1.0.
> **Evidência:** `docs/evidence/2026-09-28_07-48-26-DESKTOP-HQAG6T4-lab-binding` (logs `-FINAL`).
> **Relato do Daniel:** "abri alguns programas e, em teoria, tudo visível, sem bugs".

## 1. Relato obrigatório
**1. Houve avanço?** Sim, funcional: 600 s sem trava, sem KD, com o roteiro que travou a 148 (Gerenciador de Tarefas e navegador), e sem corrupção visual. Amostra de 1, então isso não prova que a M1 curou a trava. Houve também avanço de diagnóstico: a DX.91 foi exercitada.

**2. Sucessos (P0, exceto o visual):**
- Paginação 89.840/89.840; 3.311 Presents no KMD, 5 `BYTE_PROOF PASS`; zero TDR, fault ou `DEVICE_REMOVED` no KMD.
- **A M1 está ativa:** `[M1_SYSTEM_SEGMENT]` chegou ao #5120, ou seja, mais de 5.000 alocações com SegSet 0x3 e preferência pelo segmento 2. Há `Allocate M1 GPU_VA_ONLY` nos logs da UMD do DWM, do Explorer e de outros processos.
- **A DX.91 foi validada em hardware:** 8 resoluções que cruzariam processos foram **recusadas** (`RANGE_SCAN RECUSADO ... candidatas=0 de_outro_dono=2/3`), e as 4 `COMPOSITE_BLT RECUSADO (resolucao)` correspondentes não copiaram memória alheia. As outras 2.542 composições passaram. Visual sem corrupção pelo relato (P2).

**3. Falhas:**
- **O despejo não caiu:** 8.128 `TRANSFER Dir=0` (~2.400 MB) em 600 s, contra 7.340 na 146. A M1 não aliviou a métrica que devia aliviar.
- **O harness falhou no 1º Present** (0/60): `DXGI Present RECUSADO: preparacao de scanout falhou`, seguido de `DXGI_ERROR_DEVICE_REMOVED` com motivo `0x887A0020`. A causa está no KMD: o slot de Present do harness (`DstVa=0x200011000`) **não estava no snapshot com o próprio dono**, e só havia faixas de outros processos. Antes da DX.91, isso virava cópia sobre memória alheia; agora é recusa.
- 12.559 `PUBLICACAO_FALHA` e 332 `NO_SPACE`: o journal no teto de 1.024 esconde as faixas novas.

**4. Veredito frente ao doc 492 §3: B.** As marcas da M1 estão presentes e não houve trava, mas `Dir=0` não caiu. A falha do harness é consequência do journal no teto, visível agora por causa da DX.91, e não da M1: nenhuma recusa envolveu o segmento 2.

**5. Recuperação e pendências:**
- BasicDisplay Código 0 no mesmo boot; `CLEAN_BOOT_REQUIRED` armado.
- **NÃO VALIDADOS EM HARDWARE:** a M1 reduzir o despejo, a trava ter acabado (n=1) e a apresentação de apps novos com o journal no teto.

## 2. Leitura
1. **O snapshot do journal é fonte de verdade ruim para o display.** Com o teto atingido, alocações novas (o slot do harness, janelas novas) não entram no snapshot, e o resolvedor da composição e do Present não as acha. A fonte real é a **tabela de página do processo dono**, que o KMD já sabe percorrer (`Bc250ActiveWalkGpuVaQuiet`, usado no gate de IB).
2. **O despejo não caiu**, e o motivo ainda não é conhecido. Hipóteses:
   - o grosso do despejo é de alocações fora da M1: render targets e backbuffers do DWM, e a memória da RADV (`vkAllocateMemory`), que a M1 não cobre;
   - ou as alocações da M1 nasceram no segmento 1 apesar da preferência.

   O log de movimento traz `hAlloc` e o da M1 traz o token; hoje não dá para cruzar os dois.

## 3. Próximos passos (offline)
1. **Resolvedor pelo dono via tabela de página:** quando o snapshot não tem a faixa do dono, caminhar a raiz do processo dono em vez de desistir. Isso conserta a apresentação de apps novos com o journal no teto, sem voltar a cruzar processos.
2. **Atribuir o despejo:** incluir o handle da alocação no log da M1 e o segmento de destino das transferências, para saber o que está sendo despejado.
3. Depois, **M2** (VRAM além do BAR) e a memória da RADV.
