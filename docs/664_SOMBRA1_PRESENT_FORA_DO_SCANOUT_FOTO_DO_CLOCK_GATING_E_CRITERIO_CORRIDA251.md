# 664 — SOMBRA.1: Present fora do scanout, foto do clock gating e refresh real (critério da Corrida 251)

**Data:** 08/10/2026. **Decisão do Daniel:** opção "Present sem scanout + foto" (no lugar da subtração que eu tinha proposto).
**Correção minha (Lei 1):** a subtração original (desligar RLC.1, SDMA1, IH/EOP, doorbells, GART do MMHUB, anel na RAM, P1b) não informava nada. Todos esses blocos nasceram depois da Corrida 218, e as corridas 202–212 travaram sem nenhum deles.

## 1. A pergunta

Em todas as travas existia o mesmo núcleo. Dentro dele, a diferença mais nossa em relação ao `amdgpu` é esta: **o SDMA0 copia ~11 MB para a região que o DCN herdado do GOP está varrendo, ~23 vezes por segundo** (Present/flip).
- O `amdgpu` não faz isso: com dc=1 ele programa o DCN e faz flip de superfícies.
- A L1 (doc 662) não teve isso: o OTG varreu memória reutilizada, mas sem um blt de tela cheia por quadro sobre ela.

**SOMBRA.1:** o blt continua real no SDMA0, mas vai para uma **superfície sombra em VRAM**. Ela tem o mesmo tamanho, é persistente, fica fora do segmento do VidMm (corte DX.67) e é visível pela BAR para as provas de bytes. Muda **só o endereço de destino**. O único ponto de escrita é `g_Bc250SdmaPresent.ScanoutGpuAddress`, definido em `Bc250SdmaPresentInit`: Present, dirty rects, flip MPO e o `PRESENT_BLT` do escape passam todos por ele.

**Efeitos declarados:**
- **A tela congela** na imagem do momento do bind. O Daniel opera o menu às cegas.
- O segmento do VidMm perde ~11 MB (a sombra sai dele). Isso **aperta** a memória, ou seja, puxa a favor da trava. Se mesmo assim a trava sumir, o resultado fica mais forte.

## 2. Item 3: dados de carona (só leitura, uma vez no fim do bring-up)

- **`[CG_FOTO]`:** 69 registradores de clock gating por bloco. São todos os `CGTT_*` do GC, `RLC_CGTT_MGCG_OVERRIDE`, `RLC_CGCG_CGLS_CTRL`, `RLC_PG_CNTL`, `SDMA0/1_CLK_CTRL` e `POWER_CNTL`, `IH_CLK_CTRL` e os CGTT do MMEA/DAGB/ATC. A tabela vem de `tools/linux/bc250-l1b-regs.txt`, com proveniência de `inc/linux_ref` (Lei 7). É lida depois de RLC, G.1, SDMA, CP e IH.
- **`[REFRESH_REAL]`:** o quadro do OTG0 é lido na Bateria A e de novo no fim do bring-up, contra o QPC, com pelo menos 1 s de intervalo (`KeDelayExecutionThread` só se faltar tempo, em PASSIVE). Mede o refresh real contra os 56,130 Hz do EDID sintetizado; a L1 mediu ~60 Hz no Linux.

## 3. Pacote

Lab **3.396.1.0** = 3.395.1.0 (250) + SOMBRA.1 + foto CG/refresh. Todo o resto igual: RLC rodando, G.1, TMR.1, P1b, D2, P7, P2, P3, P4, MEC.1, LOGMIN.1, VRD.1. RADV `B4AE2E14`. Hashes em §6.

## 4. Roteiro (Daniel)

1. Boot frio. Lançador da 251 (com ou sem KD, §6).
2. Depois do bind a tela **fica congelada**. É o esperado: o nosso Present não escreve mais na tela.
3. **Mesmo roteiro da 250, às cegas:** espere 20 s depois do bind; **tecla Win** (abre o menu), espere 10 s, **Esc**, espere 30 s; repita até o rollback (~15 ciclos). **Não digite** nada no menu, como nas corridas 239–250, para manter a comparação.
4. O monitor (`monitor-corrida.py`) mostra FILLs/s e Presents/s ao vivo e serve para conferir que a carga está chegando.
5. Se travar (o monitor parar e o teclado não responder), siga o procedimento de sempre.

## 5. Critério (escrito antes, Lei 10)

**Pré-condição** (todas, senão a corrida é inválida):
- linha `[SOMBRA1] ATIVO` com MC da sombra ≠ MC do scanout;
- Presents com `SDMA=YES` e destino = MC da sombra;
- **zero** blt com destino = scanout depois do `ATIVO`;
- `[CG_FOTO]` com 69 linhas e `[REFRESH_REAL]` presente;
- o menu operado (crescimento de FILLs no monitor).

**Régua:** FILLs até a trava ou até o fim. Base com RLC rodando + TMR.1: 1.754 e 4.429.

| Desfecho | Assinatura (o que o log vai mostrar) | Implicação |
|---|---|---|
| **A** | 600 s sem trava, rollback limpo, **≥ 2.000 FILLs** | A escrita do SDMA0 na região varrida pelo DCN é o mecanismo provável. **Repetição obrigatória (252).** Se repetir, o caminho é tirar o blt de tela cheia de cima do scanout: flip de superfície (P9, DCN básico) ou sincronizar o blt com o vblank |
| **A0** | 600 s sem trava, mas **< 2.000 FILLs** | Carga insuficiente: inconclusivo (como a 237 parada). Repetir operando mais o menu |
| **B** | Trava dura com a assinatura de sempre, pré-condição OK | A escrita no scanout **não** é o mecanismo. Ele está no resto do núcleo: paginação pelo SDMA0, GCVM de 2 níveis, PTE/TLB. Próximo forte: **P5 (4 níveis)** |
| **C** | `[SOMBRA1] RECUSADO` ou blt com destino no scanout depois do `ATIVO` | Corrida inválida; corrigir offline |
| **D** | Regressão: Código 43, BugCheck, TDR em série | Ler o log do StartDevice e a linha da sombra; reverter a SOMBRA.1 |
| **E** | Fora do mapa | Descrever antes de julgar |

A foto CG e o refresh não entram no desfecho: são dados de carona.

## 6. Pacote pronto

| Artefato | Valor |
|---|---|
| Pacote | **3.396.1.0** (INF fonte 3.396.0.0) |
| SYS lab | `74F5D404…` |
| SYS assinado | `36A6FC4F…` |
| INF | `A4833D7B…` |
| CAT | `96ABB117…` |
| UMD | `B73F9BDA…` (a mesma) |
| RADV | `B4AE2E14` (a mesma) |
| Auditorias | segurança exit 0; pacote de laboratório **PASS** |
| Conferência por string no SYS assinado | `[SOMBRA1] ATIVO`, `[SOMBRA1] RECUSADO`, `[CG_FOTO] %s`, `[REFRESH_REAL] quadros`, `CGTT_SQ_CLK_CTRL` |
| Pacote 3.396 guardado em | `driver\wdk\package\output_lab_3.396.1.0_251\` |
| Lançadores | `tools\EXECUTAR_CORRIDA251_SOMBRA1_FOTO_CG_MENU_600S.cmd` (sem KD) e `..._COM_KD_MENU_600S.cmd` |

**Código:**
- **Chave e sombra:** `BC250_SOMBRA1_PRESENT_FORA_DO_SCANOUT 1` em `bc250_active_miniport.c`. A sombra vem do alocador do bootstrap (`TEST_BUFFER`, `TOP_DOWN|CPU_VISIBLE|PERSISTENT`, alinhada a 64 KB), logo antes de `Bc250SdmaPresentInit`. Se ela não for alocada, o Present segue no scanout e a linha `RECUSADO` torna a corrida inválida.
- **Foto e refresh:** `Bc250BootProbeCgAndRefresh` em `bc250_dcn_probe.c`, chamada depois de `Bc250ActiveP3ArmIh`. A espera do refresh só existe no SYS (`BC250_ENABLE_DXGK_REGISTRATION`); o link offline das suítes não tem `KeDelayExecutionThread`.

**Limites conhecidos:**
- Com a tela congelada, o monitor também não aparece; o CSV continua sendo gravado.
- Com KD, o WinDbg no host é o único sinal ao vivo.
- `PresentDisplayOnly` e `SystemDisplayWrite` ainda escrevem no scanout pela CPU. O primeiro só existe em driver display-only (não é o nosso modo), o segundo só na tela de bugcheck.

**NÃO VALIDADO EM HARDWARE.**
