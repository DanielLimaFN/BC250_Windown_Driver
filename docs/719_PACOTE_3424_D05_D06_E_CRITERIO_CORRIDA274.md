# 719 — Pacote lab 3.424.1.0 (D05 + D06) e critério da Corrida 274

**Data:** 12-13/10/2026, offline (sem corrida). Fecha a preparação dos docs [717](717_FLIP_COMPLETO_PELA_INTERRUPCAO_D05_OFFLINE_SEM_PACOTE_NOVO.md) e [718](718_D06_D12_JANELA_AMPLIADA_OFFLINE_PARA_A_CORRIDA_274.md): `build-contract.bat` completo, build de laboratório, empacotamento, auditorias e lançador.

## 1. O que foi construído e auditado

- `driver\wdk\build-contract.bat` completo: todas as ~70 suítes offline, incluindo `DCN FLIP CONTRACT` e `DCN EXT CONTRACT` (que cobrem D05/D06/D12), **OK**; `bc250mcdm.sys`, `bc250readonly.sys` e `bc250compute.sys` linkados.
- `tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`: imagem de laboratório construída em `driver\wdk\output_lab\bc250mcdm.sys` (839.680 bytes; bring-up compilado CONFIRMADO no binário).
- **DriverVer fonte subido de 3.423.0.0 para 3.424.0.0** antes de empacotar (o 3.423.1.0 já foi instalado na Corrida 273; empacotar sem subir repetiria o defeito histórico do doc 201/202 §"NOTA DE REGISTRO").
- `tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -DebugTrace -DcnFlip -DcnExt` (mesmos flags da 273). **Sem `-ArmAuxHwMode`** — D12 não armado.
- `tools\audit-bc250mcdm-lab-package.ps1`: **PASS**.
- `python tools\audit-bc250mcdm-safety.py`: **PASS** (todas as auditorias, incluindo as duas novas `audit_dcn1b_flip_completo_pela_interrupcao`, `audit_dcn06_irq_dest_readonly`, `audit_dcn12_aux_pad_hw_mode`).
- `python tools\audit-mcdm-ddi-matrix.py`: **PASS (STRUCTURAL ONLY)**.
- `python tools\audit-ddi-noop-callbacks.py`: **APROVADO** (3 dívidas aceitas pré-existentes do Gate 1.5, 1 aviso pré-existente, 0 falhas novas).

## 2. Candidato: lab 3.424.1.0

| Chave | Valor | Efeito |
|---|---|---|
| `DcnFlip` | 1 | Flip pelo HUBP0 (DCN.1) |
| `DcnExt` | **125 (0x7D)** | `0x3D` da 273 (IRQ+CURSOR+EDID+DPMS+TIMING, CRC fora) **+ `0x40` D06** (leitura do roteamento) |
| `DcnExtAuxHwModeArmed` | **false** | **D12 (pad AUX em modo hardware, 0x80) NÃO armado** — precisa do OK do Daniel |
| `DebugTrace` | 1 | Modo debug |
| `GfxWaveProbe` | 0 | Desarmada |

Hashes do pacote (`driver\wdk\package\output_lab\`):
- `bc250mcdm.sys` = `AB4A59C93302E725ACC04FF1E54CAD9E144F561469AEAA7C955FBBD98C63A61C`
- `bc250mcdm.inf` = `8DB76384B7938E134588A292135E6098C16F7A6C700D3500484B13635D80C0F2`
- `bc250mcdm.cat` = `22FF6EC11B801C1C735FCA5C8690154FD75108E80E2DDA0B3C71F0459CD9AF53`
- `bc250umd.dll` = `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66` (igual à 273)
- RADV `vulkan_radeon.dll` = `B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D` (igual à 273)

Lançador: `tools\EXECUTAR_CORRIDA274_D05_D06_180S.cmd` (CRLF conferido). Preflight: `tools\prepare-corrida274-d05-d06.ps1`, que recusa o bind se `DcnExtAuxHwModeArmed` não for `false` — ou seja, **o preflight por si só impede que D12 suba por engano** nesta corrida.

## 3. O que mudou desde a 273

1. **D05** (doc 717): o flip pelo HUBP0 agora é completado pela própria interrupção (fonte 0x4F), não só pelo tique periódico de 4 ms. Contador novo `latchados_via_irq_do_flip`, publicado em `[DCN1_RESUMO]`/`[DCN1_TEARDOWN]`.
2. **D06** (doc 718, só leitura): `OTG0_INTERRUPT_DEST`/`DCHUB_INTERRUPT_DEST2` lidos e decodificados, log `[DCN2_IRQDEST]`.
3. Janela de MMIO ampliada de `[0xE000, 0x15400)` para `[0xD800, 0x17800)`.
4. **D12 preparado mas não armado** nesta corrida.

Nada do que já funcionava na 273 foi tocado: a lógica de elegibilidade do flip, a programação do `DCSURF_FLIP_CONTROL`, DIRQL.1, DCN.2/DCN.3 (timing, cursor, DPMS, EDID sintetizado, CRC desligado) ficam bit a bit como estavam.

## 4. Critério da corrida, escrito antes (Lei 10)

A corrida roda 180 s com a T3 (como a 273), mesmo roteiro operador. Quatro desfechos, todos distinguíveis pelo log, nenhum parcial:

**A — D05 confirmado.** `[DCN1_TEARDOWN]` mostra `latchados_via_irq_do_flip` como a **maioria** de `latchados` (não um número qualquer maior que zero — maioria, já que D05 mede a interrupção chegando 1:1 desde a 269) **e** `conta=FECHA` **e** `[DCN2_IRQDEST]` aparece com os quatro bits decodificados. → A interrupção do flip, não o timer de 4 ms, está completando a resolução; D05 fecha por medição. D06 fica disponível para a próxima decisão sobre a semântica do bit (ainda H).

**B — D05 não chega a decidir.** `latchados_via_irq_do_flip` é zero ou pequeno (minoria) mesmo com `latchados` alto. → A interrupção não está chegando a tempo do DPC (ou o IH não estava armado); o mecanismo da fonte do flip sai como suspeito e o próximo foco passa a ser a latência ISR→DPC, não a posição do código (que já foi corrigida e testada offline).

**C — Trava dura de sempre, com os dois logs capturados antes dela.** A assinatura de sempre (GPU ociosa, ~1 s de vida de um processo novo) reaparece, mas `[DCN1_RESUMO]`/`[DCN2_IRQDEST]` já tinham sido publicados antes — então a corrida ainda mede D05/D06 (não fica em branco) **e** confirma, mais uma vez, que a trava não está ligada ao DCN (como L1, 251b, 263b, 268, 270 e a mineração do IH já indicaram). Isso é desfecho válido, não falha do ensaio.

**D — Regressão.** Qualquer coisa que a 273 já tinha fechado some ou piora: `conta=DIVERGE` em vez de `FECHA`, flips sem latch, DPMS ou cursor deixando de funcionar, BugCheck novo. → A mudança desta sessão introduziu um defeito; nenhuma parte do resultado é aceita antes de entender qual.

**E — Algo fora do mapa.** Descrever o observado antes de julgar, inclusive se parecer bom (ex.: a trava não aparecer dentro dos 180 s — isso é dispersão conhecida de 8 a 600 s, não confirmação de nada, como os docs 678/691 já avisaram).

## 5. Pendências

- **NÃO VALIDADO EM HARDWARE.** Nada foi instalado nem ligado na placa.
- **D12 fica para quando o Daniel autorizar.** Para armar: `tools\package-bc250mcdm-lab.ps1 ... -DcnExt -ArmAuxHwMode -IAcceptAuxPadRegisterWrite` (materializa `DcnExt=253`/`0xFD`), repetir a auditoria do pacote, gerar um novo lançador com os hashes novos.
- D17 (CRC por fase do contador do dither) continua fora, opcional.
- Antes do bind: desligar a máquina (não reiniciar) e ligar de novo; o fusível deve estar limpo (última corrida terminou em rollback `RECOVERY_OK_ONLINE`, Código 0).
