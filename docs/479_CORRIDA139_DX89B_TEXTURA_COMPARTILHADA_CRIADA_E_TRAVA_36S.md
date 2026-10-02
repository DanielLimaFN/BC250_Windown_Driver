# 479 — Corrida 139 (DX.89b): textura compartilhada criada pela primeira vez, e trava dura aos ~36 s

> **Corrida:** 27/09/2026, bind 12:51:44 após boot frio, trava ~12:52:20,9, reboot 13:04. **Candidato:** DX.89b / 3.295.1.0.
> **Evidência:** `docs/evidence/2026-09-27_12-51-44-DESKTOP-HQAG6T4-lab-binding` (sem `-FINAL`; logs da UMD em `umd-logs-pos-travamento`), cauda do KMD a partir do offset 3.206.585.279 de `C:\BC250_DISPLAY_LOG.txt`, rastro/BCR1 a partir do offset 82.634.368. Sem BugCheck (o `MEMORY.DMP` é o de 26/09).
> **Relato do Daniel:** "tela preta, tive que reiniciar".

## 1. Relato obrigatório
**1. Houve avanço?** Sim, de diagnóstico e funcional na textura compartilhada; **regressão** na estabilidade contra as 134–138.

**2. Sucessos (P0):**
- **Textura compartilhada criada pela primeira vez:** 3 de 3 com `DX59_ALLOC_QUERY ... Size=3407872 AllocateHr=0x00000000 KmdStatus=0 Flags=0x00000000`. **Hipótese (a) confirmada:** o `CpuVisible=1` que o KMD devolvia era o motivo da recusa `E_INVALIDARG`.
- O Explorer (pid 2936) **não caiu**: seguiu vivo por ~27 s depois da primeira criação compartilhada, criando recursos em ritmo alto.
- Até a trava: wrapper de paginação 6.274/6.274, 183 Presents no KMD, 1 CommitVidPn, zero TDR, fault, `InsideIb` ou `Blt Timeout`.

**3. Falhas:**
- **Trava dura da máquina ~36 s depois do bind.** Os logs da UMD (DWM, Explorer, harness) param no mesmo instante (12:52:20,7–20,9). O escritor do rastro morreu no mesmo tick da última paginação (tick 268, ~29 s de rastro). Tela preta relatada.
- Último registro BCR1 (≤ ~100 ms antes da trava): só um `PAGING_DDI 0x0C` (FLUSH_TLB) em andamento na CPU 4, com sequência mudando de registro a registro (progredindo), e **nenhum acesso MMIO em andamento.** Pelo critério do doc 470, a trava foi em menos de um tick e fora de acesso instrumentado; o ponto não fica nomeado.
- Último estágio do rastro: commit do journal (44) com **M=65, N=779 faixas vivas**, perto do teto de 1.024 (o N das corridas anteriores não foi comparado nesta análise); 12 `Journal NO_SPACE ... transacao parcial ABORTADA; Hardware PTEs written -> SUCCESS`.
- Harness só 2 Presents; DWM 18 em ~27 s.

**4. Veredito frente ao critério do doc 478: E — fora do mapa.** A exigia 600 s e Explorer sem quedas; a alocação passou (sinal do C), mas o que veio depois foi uma trava dura, que nenhum desfecho previa. A hipótese (a) está confirmada por medição direta.

**5. Recuperação e pendências:** rollback após o reboot, `RECOVERY_OK_ONLINE`, BasicDisplay Código 0; `CLEAN_BOOT_REQUIRED` armado. **NÃO VALIDADOS EM HARDWARE:** estabilidade com recurso compartilhado funcionando, Explorer estável, causa da trava.

## 2. Leitura
- Única mudança contra as 134–138 (quatro janelas limpas): a classe `RESOURCE` deixou de forçar `CpuVisible`, e com isso a textura compartilhada passou a existir e o Explorer passou a sobreviver e trabalhar muito mais.
- **H (não provado):** a trava vem da nova carga, não da flag em si. Candidatos: pressão no espaço de GPUVA perto do teto (N=779/1024, `NO_SPACE` com PTE escrita e journal abortado, deixando o snapshot divergente do hardware), ou um caminho novo de uso da textura compartilhada. **Uma corrida só não separa causa de coincidência:** a trava dura já aparecia antes (115, 128, 129, 133) sem essa mudança.

## 3. Próximo passo proposto
Repetir o mesmo pacote (3.295.1.0) uma vez, com boot frio: se travar cedo de novo, a ligação com a mudança fica forte, e o alvo passa a ser a capacidade/consistência do GPUVA sob o Explorer vivo; se completar 600 s, a trava da 139 fica como a intermitente de antes. Critério:

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava, texturas compartilhadas com `hr=0`, Explorer sem quedas | A trava da 139 foi a intermitente antiga; o recurso compartilhado funciona. Seguir. |
| **B** | Trava em menos de ~2 min, com N perto de 1.024 no último estágio 44 e `NO_SPACE` | Pressão de GPUVA é o suspeito principal; trabalhar capacidade e consistência do journal (offline). |
| **C** | Trava com N baixo | O suspeito passa a ser o caminho de uso do recurso compartilhado; ler o que DWM/Explorer fazem com ele no instante da trava. |
| **D** | Trava com acesso MMIO em fase ANTES persistente em BCR1 | O acesso nomeado é o suspeito direto. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |
