# DX.32 — engine de invalidação próprio do anel, cópia de flip não repetida, `COMPUTE_VMID` lido; critério da Corrida 78

> **23/09/2026. OFFLINE; zero bind.** Revisão do trabalho das faixas DX.30/DX.31 e das Corridas 76/77
> (docs/345, 346 e 347, com as correções de revisão anexadas).
> Este critério foi escrito **antes** do ensaio (Leis 9 e 10). **NÃO VALIDADO EM HARDWARE.**

## 1. O que motiva (P0, Corridas 73–77)

- **Classe de TDR, 9 de 9:** os 9 TDRs de 240 ondas caíram num IB de **dispatch** (280/288 DW, cópia
  imagem→buffer do `DRAW_READBACK`) logo depois de troca de VMID. Resistiram à DX.29 (espera no PFP)
  e à DX.30 (flush de VM no anel, com ordem comprovada no dump da 76).
- **O flush no anel usava o engine 17**, o mesmo que a CPU usa por MMIO (flush de VMID sob `TlbLock`,
  serviço de compute), sem serialização entre os dois. O upstream dá um engine a cada anel
  (`amdgpu_gmc_allocate_vm_inv_eng`) e deixa o 17 para o flush do driver.
- **Cópia de flip redundante:** as 22 `PRIMARY_COPY` não-nulas da 77 copiavam a mesma origem que o
  `DxgkDdiPresent` imediatamente anterior já tinha copiado (Present #1 e cópia #4 de 0x153C000;
  #10 e #12 de 0x266C000), e as amostras do destino já batiam em 22 de 22. Eram ~11 MB síncronos a
  mais numa DDI documentada como de nível de interrupção.
- **`ObservedComputeVmid` prometido no docs/344 nunca existiu**, e sem ele os desfechos C e D daquele
  critério eram indistinguíveis.

## 2. O que mudou

| # | Mudança | Tipo |
|---|---|---|
| V1 | O anel gráfico invalida no **engine 0** (REQ 0xA28C, ACK 0xA2D4); a CPU continua no 17. `Bc250PagingBuildGfxhubTlbFlushPlanForEngine` é a fonte única dos offsets (`gc_10_1_0_offset.h`, `GC_BASE[0]=0x1260`); só aceita os engines 0 e 17, os dois com faixa 0xFFFFFFFF/0x1F programada pelo MMU global, como no `gfxhub_v2_0_program_invalidation`. O log `VMID_SWITCH_VM_FLUSH` passa a mostrar `Eng=`. | **Comportamental** |
| V2 | `PRIMARY_COPY` pula a SDMA **só se** as duas condições valem: (a) a última escrita no scanout foi a cópia **integral** desta mesma origem, sem escrita posterior — marca gravada e apagada apenas nos wrappers de `Bc250SdmaPresentBlt`/`BltRect`; (b) as 256 amostras pareadas conferem agora, com origem não-nula. Senão, copia como antes. Linhas `REDUNDANT_SKIP` e `REDUNDANT_CANDIDATE_REJEITADO`. | **Comportamental** |
| M1 | `[TDR_GFX_VMID] ObservedComputeVmid=` no snapshot de TDR: `COMPUTE_VMID` (0xB850, `DATA[3:0]`) e `GRBM_GFX_CNTL`, somente leitura. | Medição |

**Duas variáveis comportamentais**, por decisão do Daniel. Separação: V1 só age na troca de VMID do
anel gráfico; V2 só age na DDI de flip. O efeito de V2 no tempo do scheduler pode, em tese, mudar o
momento de TDRs, e isso fica declarado.

**Limites declarados:**
- a paginação do dxgkrnl (fill/INDIRECT) não passa pelos wrappers de Blt; ela não mira o scanout
  reservado do GOP;
- `COMPUTE_VMID` é registrador SH lido pela seleção GRBM corrente, então atribuí-lo ao pipe gráfico é H;
- o pulo de V2 é prova **amostral**, como o `END` da DX.31, e não prova integral.

**Verificação offline:**
- `build-contract` EXIT=0, com teste novo dos offsets dos engines 0 e 17 e recusa de engine sem faixa;
- SYS de laboratório compilado; UMD e ICD iguais aos da 77;
- auditorias de segurança, matriz de DDI, callbacks no-op e RADV: PASS;
- portão DX.32 novo, e localizadores de portões antigos ajustados à refatoração (blt no núcleo,
  comparação pareada na função auxiliar), sem afrouxar exigência;
- 7 mutações da auditoria reprovam, mais 1 de teste (offset errado do REQ derruba
  `paging_contract_test`);
- a ferramenta de mutação DX.31 continua pegando as 3 dela.

**Pacote lab 3.239.1.0**, assinado, `Lab package audit: PASS`, NÃO INSTALADO:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `9324073FE93F7EC3E2E972A3B3FA890EB4E97CBB6BD4DD1499178D00A07A311A` (fonte `71A84D72...`) |
| UMD | `239B046C9FDA264BB1579F46F48DDCFDCB4A8F6DA273F879928D629C25F9A75D` (igual à 77) |
| INF / CAT | `E33801A9...` / `90F8EBF8...` |
| ICD | `5988B38E...` (igual à 76 e à 77) |

**Riscos:**
- se o engine 0 não responder, o ME fica preso no `WAIT_REG_MEM` do ACK (0x28B5 em dword) logo
  depois de uma `VMID_SWITCH_VM_FLUSH ... Eng=0` (desfecho D);
- se o pulo de V2 estiver errado, o scanout fica com um quadro velho (desfecho E).

## 3. Critério da Corrida 78

### 3.1 Sinais
- **ENG0-ATIVO:** linhas `VMID_SWITCH_VM_FLUSH ... Eng=0`, **e** o dump da primeira transição com REQ
  no dword 0x28A3 e ACK no dword 0x28B5 (e não 0x28B4/0x28C6).
- **SEM-TDR-PÓS-TROCA:** zero TDR cujo IB travado, pelo `IB1 Base`, venha logo depois de IB de outro VMID.
- **VMID-OBS:** em cada TDR, `[TDR_GFX_VMID] ObservedComputeVmid=` comparado com o VMID do IB travado
  e com o do IB anterior (carimbos).
- **SKIP:** contagem de `REDUNDANT_SKIP` e de `REDUNDANT_CANDIDATE_REJEITADO`; `PRIMARY_COPY` sem FAIL.
- **Controles:**
  - autoteste 5/5 nas três variantes (uma variante NÃO MEDIDA por device perdido de outro processo
    não conta como falha);
  - zero `PREAMBLE_CNTL`, CSIB programado e zero IB cumulativo;
  - zero BugCheck;
  - `BYTE_PROOF PASS` do Present ≥ 1.
- **Visual (Daniel):** o que aparece antes do rollback, se muda, e se o mouse se mexe.

### 3.2 Desfechos (assinatura e implicação separadas)

**A — Engine próprio elimina a classe**
- Assinatura: ENG0-ATIVO, **zero TDR** na corrida inteira e controles.
- Implicação: a disputa do engine 17 entre anel e CPU era causa necessária. Próximo: desktop
  sustentado, mouse e 60 quadros do harness.

**B — A classe pós-troca some, sobra outro TDR**
- Assinatura: ENG0-ATIVO e SEM-TDR-PÓS-TROCA, com TDR de outra assinatura.
- Implicação: descrever a nova assinatura antes de propor qualquer coisa.

**C1 — A classe persiste e o dispatch roda com o VMID anterior**
- Assinatura: ENG0-ATIVO, TDR de 240 ondas pós-troca, e `ObservedComputeVmid` igual ao VMID do IB
  **anterior**.
- Implicação: o VMID do dispatch na fila gráfica não segue o `INDIRECT_BUFFER`. Próximo, por leitura
  upstream: `CONTEXT_CONTROL`/estado CS carregado na troca de contexto.

**C2 — A classe persiste e o VMID observado é o certo**
- Assinatura: ENG0-ATIVO, TDR de 240 ondas pós-troca, `ObservedComputeVmid` igual ao VMID do IB
  **travado**.
- Implicação: não é lançamento sob o VMID errado. Investigar `ACQUIRE_MEM`/visibilidade e o próprio
  shader de cópia.

**C3 — A classe persiste sem leitura útil**
- Assinatura: ENG0-ATIVO, TDR de 240 ondas pós-troca, `ObservedComputeVmid` INDETERMINADO ou incoerente
  com os dois VMIDs.
- Implicação: a leitura de `COMPUTE_VMID` pela seleção GRBM atual não serve. Não usar como
  discriminante até corrigir a seleção.

**D — O engine 0 não responde**
- Assinatura: TDR com ME em `WAIT_REG_MEM` (`ME_HEADER_DUMP=0xC0053C00`) logo depois de uma
  `VMID_SWITCH_VM_FLUSH ... Eng=0`, com o IB alvo sem partir.
- Implicação: o engine 0 não está operacional como configurado. Voltar o anel ao 17 com serialização
  pela CPU, ou revisar a faixa do engine 0. Nada sobre o dispatch é concluível.

**E — O pulo de V2 prejudica a imagem**
- Assinatura: `REDUNDANT_SKIP` presente, e o Daniel vê imagem pior que na 77 (preto ou quadro velho),
  **ou** amostras do scanout depois de um `REDUNDANT_SKIP` diferentes da origem do flip seguinte.
- Implicação: reverter V2 antes de interpretar o display.

**F — Regressão de controle**
- Assinatura: autoteste com **FAIL** (não NÃO MEDIDO), CSIB perdido, `PREAMBLE_CNTL` de volta, IB
  cumulativo ou BugCheck.
- Implicação: atribuir a regressão antes de ler TDRs.

**G — Fora do mapa**
- Descrever antes de julgar.

Desfecho parcial não é desfecho (Lei 10.3). V1 e V2 são julgadas separadamente: o display (E ou
normal) e a classe de TDR (A–D) podem sair em letras diferentes na mesma corrida.

### 3.3 Execução
Boot frio completo (`shutdown /s /t 0`, sem Fast Startup), preflight do binder PASS e então
`tools\EXECUTAR_D3D11_NATIVO.cmd`, elevado, **sem `-Force`**. Anotar a imagem e se o mouse se mexe.
Teto: uma corrida.
