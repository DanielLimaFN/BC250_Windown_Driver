# 713 — Corrida 271: a conta dos flips fecha; DCN.2 sobre o DCN do GOP (pacote 3.422) e critério da Corrida 272

**Data:** 12/10/2026. **NÃO VALIDADO EM HARDWARE** para nada da DCN.2.

## 1. Corrida 271 (19:27, lab 3.421.1.0, 180 s)

Evidência: `docs/evidence/2026-10-12_19-27-21-DESKTOP-HQAG6T4-lab-binding/`; teardown no log cumulativo pelo offset do baseline.

- **P2, contagem (P0):** `flips=3796 latchados=3796 substituidos=0 pendentes=0 conta=FECHA`, recusas 0, readback falho 0.
  - Os 15 flips "sem destino" da 269 eram mesmo contabilidade.
- **P3, VSYNC (P0):**
  - latch em 1 quadro: 3.570; em 2 quadros: 168; em 3+: 7; em 0: 51 (flip para o mesmo endereço);
  - latch em tempo: <16,7 ms 1.752, <33,4 ms 1.987, <50 ms 55, mais que isso 2 (máximo 62,6 ms);
  - OTG com 10.463 quadros = **59,997 Hz**; VSYNC vistos 10.019, ou seja **444 perdidos (4,2%)** pela consulta de 4 ms;
  - VSYNC notificados = vistos.
- **Rotas:** 3 `SET_ADDRESS` e 3.793 MPO3; zero imediato e zero primária fora do segmento 1, inclusive com vídeo e tela cheia (relato pendente).
- **Estabilidade:** 174 s armado, zero TDR e zero fault, rollback `RECOVERY_OK_ONLINE`, HUBP0 devolvido ao GOP com readback OK.
- **P4, sob a T3:** **sem resposta.** A T3 não rodou; o último registro do `C:\BC250_PROCESSO_NOVO.log` é das 15:07.
- **Veredito:**
  - P2 e P3: desfecho de sucesso.
  - P1: depende do relato do operador.
  - P4: não medido.
- **Achado de proveniência:** o doc 710 citava `dcn_2_0_1_offset.h`, que não existe. O `dcn201_resource.c` do `amdgpu` inclui `dcn_2_0_3_offset.h`/`sh_mask.h`, agora em `inc/linux_ref/dcn/`. Os 12 offsets da DCN.1 batem com ele; só o nome citado estava errado, e o contrato e a auditoria foram corrigidos.

## 2. DCN.2: o que entrou (P9 ampliada pelo Daniel em 12/10/2026)

Cada item tem um bit na chave nova **`DcnExt`** (INF, padrão 0; o empacotador escreve 0x3F com `-DcnExt`). Cada um falha fechado sozinho e volta como o GOP deixou no teardown e na passagem de posse.

**Proveniência:** `inc/linux_ref/dcn/` (headers e fontes do `amdgpu` v6.12: `dcn_2_0_3_*`, `irq_service_dcn201.c`, `irqsrcs_dcn_1_0.h`, `dcn10_optc.c`, `dcn20_hubp.c`, `dcn20_dpp.c`, `dcn10_hwseq.c`, `dce_aux.c`).

| Bit | Item | O que faz | Escreve |
|---|---|---|---|
| 0x20 | 1. Timing real | Lê `H/V_TOTAL`, blank e sync do OTG0 (inverso de `optc1_program_timing`) e mede o refresh pela varredura (quadros × VTotal + posição vertical, em ~200 ms). VidPn e EDID sintetizado passam a declarar o medido no lugar de 56,13 Hz. Modo-alvo com refresh >1% diferente não casa. | nada no DCN |
| 0x01 | 2. VSYNC por interrupção | Liga `VSTARTUP_INT_EN` do OTG0 (fonte IH 0x3C) e a interrupção de flip do HUBP0 (0x4F), cliente DCE 0x04, como `irq_service_dcn201`. O ISR do IH faz o ack (bit CLEAR, Lei 6) e enfileira o DPC de VSYNC. Com a interrupção viva (<50 ms), só ela gera VSYNC; o timer de 4 ms vira rede. | `OTG_GLOBAL_SYNC_STATUS`, `DCSURF_SURFACE_FLIP_INTERRUPT` |
| 0x02 | 3. CRC do OTG | CRC0 contínuo no quadro ativo inteiro (`optc1_configure_crc`). Uma amostra por quadro e um CRC por buffer exibido parado. **Não é prova byte a byte** (sem gabarito do algoritmo): mede se o quadro exibido muda e se cada buffer é estável. | `OTG_CRC_CNTL`, janelas |
| 0x04 | 4. Cursor por hardware | 64 KB de VRAM reservados no topo antes do corte do segmento (como a SOMBRA), mapeados pela DRAM em WC. Dois slots 64×64 ARGB. `SetPointerShape`/`Position` programam o HUBP0 (`CURSOR_*`) e o DPP0 (`CNVC_CUR0`, escala 1.0) como `hubp2/dpp2`. Caps `PointerCaps.Color` e máximo 64×64 só com o buffer pronto. | `CURSOR0_*`, `CNVC_CUR0_*`, `HUBPREQ0_CURSOR_SETTINGS` |
| 0x08 | 6. EDID real + HPD | Lê HPD0/1 (só leitura) e escolhe o AUX. Transação AUX por software (`dce_aux.c`): DPCD 0x000, depois EDID por I2C sobre AUX (0x50), até 2 blocos. Publica ao Windows **só** se o checksum fecha e o timing **preferido** (DTD 0) é exatamente o que o OTG varre; senão segue com o modo recomendado pelo driver. O AUX volta como o GOP deixou. | `DP_AUX*` (só durante a leitura) |
| 0x10 | 6. DPMS | Monitor em D3 apaga o OTG (`OTG_BLANK_DATA_EN`), D0 acende, como `optc1_blank/unblank_crtc`. | `OTG_BLANK_CONTROL` |

### O que NÃO entrou, e por quê

- **5. Flip direto (DirectFlip).** O `CheckDirectFlipSupport` só existe na tabela de funções do **D3D11.1**, e a nossa UMD negocia o D3D11.0 (`D3D11_0_DDI_INTERFACE_VERSION`). Ligar só `SupportDirectFlip` no KMD não faria nada e seria declarar capacidade sem efeito (Lei 4). Exige a UMD D3D11.1.
- **Cursor monocromático e de cor mascarada.** O WDK não deixa claro se `Height` do monocromático conta as duas máscaras, e errar ali leria além do buffer do `dxgkrnl` (BugCheck). Nesta rodada só cor ARGB; para as outras formas o Windows usa o cursor por software.
- **Troca de modo, planos de overlay extras e desligar o monitor de verdade (DPCD 0x600).** Reprogramam timing, clock de pixel ou link, com risco de tela preta. Fora do escopo aprovado.

### Verificação offline

- `dcn_ext_contract_test` com os números medidos: timing 2784×1111, refresh de 59,994 Hz, DTD ida e volta, EDID com checksum, conversão de cursor, recorte na borda, CRC, AUX.
- Build de contrato e de laboratório limpos.
- Portão novo `audit_dcn2_sobre_o_gop`, provado por **9 mutações**: IRQ sem chave, log no ISR, AUX sem restaurar, DPMS sem chave, cursor aceitando mono, teardown sem CRC, EDID publicado sem casar o DTD, offset errado, caps de mono.
- O portão de escritores da DCN.1 passou a listar os escritores da DCN.2 um a um.
- Auditoria do pacote: PASS.

## 3. Pacote 3.422.1.0 (Corrida 272)

| Item | Valor |
|---|---|
| SYS assinado | `4071FC99C9F96156318B2753E33FC49DF177754BAD34811C938892200C466B80` |
| SYS fonte | `57D770653AEE7C8B5CCECEBDC4192BADD9653C65CD6A3AF6B25B4F905C7E9E47` |
| INF | `0036C4DFF2DAB186D045F90EC7E9FFE054160E33176767C25F1B78D8D0E6516C` (DcnFlip=1, DcnExt=63, DebugTrace=1) |
| CAT | `93987F254FC58AB65D451810200A04196BF93BB50AFD8762FA0F9F6757F224A2` |
| UMD / RADV | `B73F9BDA` / `B4AE2E14`, iguais |
| Cópia | `driver/wdk/package/output_lab_3.422.1.0_272` |
| Lançador | `tools/EXECUTAR_CORRIDA272_DCN2_SOBRE_O_GOP_180S.cmd` (preflight `tools/prepare-corrida272-dcn2-sobre-o-gop.ps1`) |
| Apagar o monitor | `tools/DESLIGAR_MONITOR.cmd` (só manda `SC_MONITORPOWER` ao Windows) |

**Antes:** desligar e ligar (o rollback da 271 foi online, fusível limpo).

## 4. Critério da Corrida 272 (escrito antes)

**Pré-condição:**
- `[DCN2] chave DcnExt=0x3F`;
- `[DCN2_TIMING]` com recusa 0;
- `[DCN1_FLIP] ARMADO`.

Sem isso, a DCN.2 não foi exercitada. As perguntas são independentes; assinatura e implicação ficam separadas (Lei 10).

**1. Timing real**
- *Sucesso — assinatura:* `[DCN2_TIMING] APLICADO`, `[SignalInfo] VSync=~59994/1000`, desktop sobe (VidPn converge).
- *Sucesso — implicação:* o Windows agenda os quadros na taxa real.
- *Falha — assinatura:* sem `CommitVidPn` ou laço de VidPn.
- *Falha — implicação:* reverter o bit 0x20 e examinar o modo-alvo recusado.

**2. VSYNC por interrupção**
- *Sucesso — assinatura:*
  - `[DCN2_RESUMO]` com `vstartup` ≈ quadros do OTG;
  - `[DCN1_RESUMO]` com `perdidos` perto de 0 (271: 4,2%);
  - `ticks_sem_irq` baixo.
- *Sucesso — implicação:* D5 fechada; VSYNC fiel ao hardware.
- *Assinatura alternativa:* `vstartup=0`.
- *Implicação:* a interrupção do DCN não chega ao IH (falta habilitação no DMU/IH). A rede de 4 ms segura o VSYNC, e o próximo passo é rastrear o caminho do cliente DCE.

**3. CRC**
- *Sucesso — assinatura:* amostras ≈ quadros; cada buffer parado com CRC estável; `mudancas` subindo quando a tela muda.
- *Sucesso — implicação:* identidade do quadro exibido por hardware. Não é prova byte a byte.
- *Falha — assinatura:* CRC sempre 0 ou mudando todo quadro com a tela parada.
- *Falha — implicação:* CRC não confiável; desligar o bit 0x02.

**4. Cursor**
- *Sucesso — assinatura:* `[DCN2_CURSOR] forma #n cor` com readback igual, movimentos subindo, e **o operador vê o cursor acompanhar**, inclusive nos cantos.
- *Sucesso — implicação:* cursor por hardware.
- *Falha — assinatura:* cursor some, fica preso ou deixa rastro.
- *Falha — implicação:* reverter o bit 0x04 (o Windows volta ao cursor por software) e examinar o endereço e o DPP.

**6a. EDID real**
- *Assinatura:* `[DCN2_EDID]` com checksum OK e `DTD_igual_ao_OTG=0` e `PUBLICADO`, ou `NAO publicado` com o motivo.
- *Implicação:* identidade real do monitor no Windows; ou a confirmação de que o EDID dele não descreve o modo do GOP.

**6b. DPMS**
- *Assinatura:* `[DCN2_DPMS] D3 -> OTG apagado` e `D0 -> OTG aceso` com o monitor voltando normal (operador).
- *Implicação:* apagar e acender o monitor funciona.

**7. Sob a T3**
- **(a)** 180 s com tudo ligado: nenhum item trava a máquina nesta janela.
- **(b)** Trava: comparar com a assinatura de sempre. Se igual, nenhum item da DCN.2 é suspeito.

**E — algo fora do mapa:** descrever o observado antes de julgar.
