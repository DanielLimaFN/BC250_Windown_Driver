# DX.37 — CONTEXT_CONTROL na troca de VMID. Critério da Corrida 83

> 23/09/2026. Preparação OFFLINE, sem bind. Resultado anterior: [Corrida 82](357_CORRIDA82_DX36_RESULTADO.md).
> **NÃO VALIDADO EM HARDWARE.** Teto: uma corrida, somente após boot frio e decisão do Daniel.

## Motivo e procedência

- **P0:** na Corrida 82, três de três TDRs tinham ondas com o VMID do IB anterior e fault de busca de instrução na página do PC delas (`Cid=9`). O desktop apareceu com partes faltando; harness 0/60.
- **P1:** Linux v6.6, `amdgpu_ib.c:183,223–229` emite `emit_cntxcntl` após `amdgpu_vm_flush` e antes do IB quando muda o contexto do scheduler (`ring->current_ctx != fence_ctx`). `gfx_v10_0.c:8082–8111` envia `CONTEXT_CONTROL` com `load_enable`, `load_global_config`, `load_global_uconfig`, `load_cs_sh_regs`, `load_per_context_state` e `load_gfx_sh_regs`. Sem preâmbulo, o bit `load_ce_ram` fica desligado. Fontes: [amdgpu_ib.c](https://github.com/torvalds/linux/blob/v6.6/drivers/gpu/drm/amd/amdgpu/amdgpu_ib.c#L183-L229), [gfx_v10_0.c](https://github.com/torvalds/linux/blob/v6.6/drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c#L8082-L8111).
- **H:** emitir esse pacote ao trocar VMID no nosso anel recarrega o estado de dispatch que vinha retendo o VMID anterior. Nosso gatilho é troca de VMID, enquanto o Linux usa troca de contexto do scheduler; equivalência não demonstrada.

## Variável da faixa

Única mudança comportamental: após `PFP_SYNC_ME` e antes do `INDIRECT_BUFFER`, a troca de VMID emite `C0012800 81018003 00000000` (`CONTEXT_CONTROL`). A transição cresce de 31 para 34 DW; a reserva continua derivada do tamanho. O par `SWITCH_BUFFER` permanece proibido. Sondas de ondas e de faults, UMD e RADV permanecem como na 82.

**Verificação offline:** `build-contract` EXIT=0; build lab EXIT=0; auditorias de segurança, matriz DDI, callbacks, RADV, mutações DX.31 e pacote lab PASS. A mutação que zerou o payload em C fez o portão DX.37 reprovar; trocar no teste o esperado `0x81018003` por `0x81018007` fez `pm4_packet_builder_test` falhar. O SYS empacotado contém `DX37_CONTEXT_CONTROL=0x%08X`.

**Pacote lab 3.244.1.0**, assinado e auditado, **NÃO INSTALADO**: SYS SHA-256 `9D0FF839119FDDCA8B86FA47CE82966C0B334DDDC09E4FA0ECC369B8F9905ED9`; PDB `8588978AA2B5F259A4386D25CF2F5B70D814B1CC1AB27FEFA2B12268CCFA05BB`; UMD `239B046C9FDA264BB1579F46F48DDCFDCB4A8F6DA273F879928D629C25F9A75D` (inalterada); INF `A444FB33B0ADAB739442933025BFA9DA0E71641337C49FFB9A2560BBEB36ACB2`; CAT `2F8B72F0FC170C235A7D0F993035712D9E6FA4F791D2E4AA2FD8D6828FF2DC2C`.

Risco: o pacote pode travar o CP (como o `SWITCH_BUFFER` da 81), não alterar o VMID das ondas, ou carregar estado incompleto. Um 0x116 é resultado possível. A identificação vem do header ME/PFP e da posição RPTR, não apenas da presença de TDR.

## Sinais exigidos

- **ATIVO:** no primeiro `VMID_SWITCH_VM_FLUSH_PM4`, `PM4DW=34` e `C0004200 00000000 C0012800 81018003 00000000 C0023F00`; hash do SYS corresponde ao pacote auditado.
- **ANEL-VIVO:** submissões e fences aposentadas após trocas; RPTR avança além do novo pacote.
- **ONDA-VMID:** em cada TDR, comparar `WaveVmid` com VMID do IB travado (`CARIMBO -0`) e do IB anterior (`-1`). Casar fault de `GCVM_FAULT_TIMELINE` com PC e VMID da onda.
- **Controles:** `Eng=0`, zero `PREAMBLE_CNTL` no caminho de submissão, zero BugCheck, três autotestes 5/5 quando medidos; `MAP_FALHOU` ou device perdido é NÃO MEDIDO. Relatar imagem, cursor e desktop observados pelo Daniel, harness e recuperação.

## Desfechos — assinatura e implicação

| Código | Assinatura | Implicação |
|---|---|---|
| A | ATIVO, ANEL-VIVO, zero TDR na corrida inteira | H ganha suporte neste cenário. Ainda exigir desktop íntegro e interativo e 60 quadros para fechar a meta funcional. |
| B | ATIVO, ANEL-VIVO; há TDR, mas todas as ondas coerentes têm VMID do IB novo e não do anterior | A mistura de VMID observada na 82 desapareceu; classificar cada fault residual antes de outra alteração. |
| C | ATIVO; qualquer TDR com onda no VMID do IB anterior, diferente do novo | `CONTEXT_CONTROL` nessa posição não basta. Não repetir a mesma corrida sem novo discriminante. |
| D | Header ME/PFP em `0xC0012800`, ou RPTR parado nos 3 DW novos após uma troca; nenhuma aposentadoria posterior | O pacote travou o anel. Reverter a única variável e auditar binário, sem concluir nada sobre VMID de dispatch. |
| E | TDR sem leitura coerente de ondas ou sem fault próprio na linha do tempo | Classificação B/C fica inconclusiva para esse TDR; apresentar a lacuna. |
| F | BugCheck, autoteste FAIL medido, `PREAMBLE_CNTL` ou perda de `Eng=0` | Regressão de controle; identificar primeiro o mecanismo e recuperar. |
| H | Bind no mesmo boot após rollback, marcador ausente ou pacote divergente | Falha do host/ensaio; nenhum veredito de hardware. |
| G | Qualquer assinatura não coberta acima | Fora do mapa: descrever dados e implicação antes de julgar. |

**Desfecho parcial não é desfecho.** Classificar todos os TDRs, sem escolher só o primeiro. Código 0 e Exit 0 isolados não encerram A.

## Execução e recuperação

Daniel executa `tools\EXECUTAR_D3D11_NATIVO.cmd` elevado, sem `-Force`, após desligamento completo (`shutdown /s /t 0`) e novo boot frio. Não remover `C:\BC250_REQUIRE_CLEAN_BOOT.json`. Se houver 0x116, recuperar o log perdido com `tools\recover-bc250mcdm-lost-tail.ps1 -MaxFolders 1`; pedir dump ao Daniel com `cdb` elevado e saída absoluta. Registrar driver ativo, rollback e itens NÃO VALIDADOS EM HARDWARE.

## Frente independente, somente offline

Na 82, `hProc=000001D0000001D0` mapeou 16 páginas graváveis em `0x2401A0000` (linha 4329 do `display-log-this-bind-FINAL.txt`), desmapeou (4357–4358), depois mapeou 20 somente leitura (4371), antes de submeter o IB de 552 DW que escreve em `CB_COLOR0_BASE=0x2401A0000` (4422–4433). A associação `hProc`→PID/recurso não aparece nesses logs. A causa do reuso de VA permanece **H**; não se altera a UMD/RADV na Corrida 83.
