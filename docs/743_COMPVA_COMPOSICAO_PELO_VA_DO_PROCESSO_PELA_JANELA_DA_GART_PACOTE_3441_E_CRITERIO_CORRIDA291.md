# Doc 743 — COMP.VA: composição pelo VA do processo, pela janela da GART; pacote 3.441.1.0 e critério da Corrida 291

> **Data:** 13/10/2026, offline (sem corrida).
> **Pedido do Daniel:** fazer a composição também pelo VA do processo, junto com a APU.2c.
> **Base:** [doc 742](742_CORRIDA290_APU2B_FECHOU_O_0x116_TELA_PRETA_POR_COMPOSICAO_RECUSADA_APU2C_PACOTE_3440_E_CRITERIO_CORRIDA291.md). O pacote 3.440.1.0 do doc 742 **não foi instalado** e foi substituído pelo 3.441.1.0. A Corrida 291 passa a usar este pacote.

## 1. Problema

O `COMPOSITE_BLT` copia por endereço MC e só resolve página na VRAM. Na 290 a superfície do DWM estava na RAM (segmento 2), e o resultado foi 160 recusas, 0 cópias e tela preta.

A APU.2c reduz o problema porque a superfície da UMD passa a preferir a VRAM. Mesmo assim, o VidMm ainda pode despejá-la para a RAM sob pressão; a 288 já tinha 98 recusas desse tipo.

## 2. O que mudou: COMP.VA

É a mesma mecânica do `amdgpu_ttm_map_buffer` (janela de GTT do `buffer_funcs_ring`), em P1:

1. A resolução por MC continua sendo o primeiro caminho, porque é o provado (4.907 OK na 288). As duas resoluções agora rodam sempre; antes o `||` parava na primeira e o log mostrava `Dst=0x0` sem ter tentado a segunda.
2. Se um dos lados não resolver, `Bc250ActiveEscapeCompositeBltViaVa`:
   - pega a raiz da tabela de páginas do **processo dono do escape** (hContext, senão hDevice), com a mesma regra do DX.93: só raiz no segmento 1, nunca a de outro processo;
   - caminha a tabela, uma vez por tabela folha (2 MB de VA), e lê a PTE de cada página;
   - exige VALID e a permissão certa: leitura na origem, escrita no destino;
   - copia a PTE para a **janela da GART da VMID 0** sem os bits de fragmento (11:7) e sem nada acima do MTYPE: origem nas entradas 256..383, destino nas 384..511, 512 KB cada;
   - para cada pedaço (até ~50 linhas de 10.240 bytes), faz `mfence` e emite no anel do SDMA0 a **invalidação da VMID 0** (`Bc250SdmaEmitGartInvalidate`, engine 1, 15 dwords), depois a cópia (linear ou retângulo), o GCR e a fence. Em seguida espera RPTR e fence, como o blt de sempre;
   - no fim, a janela inteira volta para a dummy.
3. O lado que resolveu por MC continua por MC; só o lado fora da VRAM passa pela janela.

Salvaguardas:
- **Primeira utilização:** as entradas 256..511 precisam ser todas iguais à dummy e sem o bit SYSTEM. Se não forem, o caminho é recusado, porque alguém está usando a janela.
- **Janela única:** se houver outra composição em curso, a chamada é recusada e o quadro é perdido, com log.
- **Sem a GART da D2 ligada**, recusa nomeada.
- O emissor público `Bc250SdmaEmitVmInvalidate` continua recusando a VMID 0. A VMID 0 só pode ser invalidada pelo emissor próprio da GART.

Mapa da GART da VMID 0 (512 entradas): MEC 0..31, anel da D2 32..35, P7 64.., IH 128..191, SDMA1 224..225, **COMP.VA 256..511** (estavam livres).

## 3. Arquivos

- `driver/core/bc250_sdma_packet_builder.c/.h`: `Bc250SdmaEmitGartInvalidate`. Teste com a sequência dourada de 15 dwords (REQ `0x00F80001`, ACK bit 0). A recusa da VMID 0 no emissor comum continua testada.
- `driver/wdk/bc250_sdma_present.c/.h`: `Bc250SdmaPresentBltViaGartWindow`. Os núcleos do blt linear e do retângulo ganharam `FlushGartFirst` (FALSE nos caminhos antigos). A cópia pela janela só **apaga** a marca de "scanout integral".
- `driver/wdk/bc250_active_miniport.c`: `Bc250ActiveWalkLeafTable`, `Bc250ActiveEscapeOwnerRoot`, `Bc250ActiveCompVaMapWindow`/`UnmapWindow` e `Bc250ActiveEscapeCompositeBltViaVa`, ligados no `COMPOSITE_BLT`.
- `tools/audit-bc250mcdm-safety.py`: a regra DX.32 reconhece o quarto escritor da marca e exige que ele só a apague.
- `build-contract` EXIT=0; auditoria do pacote PASS; auditoria de segurança PASS.

## 4. Pacote lab 3.441.1.0

- SYS assinado `C83F70F6…` (fonte `230BD274…`), INF `C65DFBA4…`, CAT `36B04FD9…`.
- UMD `B73F9BDA` e RADV `B4AE2E14` iguais.
- `DebugTrace=1`, `DcnFlip=1`, `DcnExt=125`, `ApuUnifiedMemory=1`.
- Contém a APU.2b, a APU.2c e a COMP.VA.
- Roteiro: `tools/EXECUTAR_CORRIDA291_APU2C_300S.cmd` (preflight `tools/prepare-corrida291-apu2c.ps1`, atualizado para este pacote).

## 5. Critério da Corrida 291 (substitui o do doc 742)

| Desfecho | Assinatura no log | Implicação |
|---|---|---|
| **A** | `COMPOSITE_BLT OK` + `COMPOSITE_BLT OK VIA_VA` > 1.000 somados, `VIA_VA FALHOU` < 1%, zero TDR, desktop visível (operador) | composição independente do segmento; medir a fluidez contra a 288 |
| **A2** | igual a A, mas com zero `VIA_VA` (tudo por MC) | a APU.2c bastou; a COMP.VA não foi exercitada (fica NÃO VALIDADA) |
| **B** | `VIA_VA FALHOU … SDMA nao concluiu` ou TDR/0x116 logo depois do primeiro `VIA_VA` | a invalidação da VMID 0 no anel ou a janela quebram o SDMA0; desligar a COMP.VA e minerar a fence/RPTR do pedaço |
| **C** | `VIA_VA RECUSADO`/`FALHOU` por PTE ou raiz | o dono ou a tabela não são os esperados; minerar o VA que falhou (está no log) |
| **D** | tela com lixo ou imagem de outro programa com `OK VIA_VA` | PTE errada na janela (bits ou endereço); comparar com o walker verboso |
| **E** | algo fora do mapa | descrever antes de julgar |

Antes: desligar e ligar. Depois que o desktop subir, rodar `tools\RODAR_PROCESSO_NOVO.cmd` como administrador.

## 6. Riscos declarados

- **Invalidação da VMID 0 em regime:** é a primeira vez fora do boot. Ela só descarta traduções em cache, e o MEC, o anel e o IH são recaminhados com o mesmo valor (H com base no amdgpu, que invalida a GART a cada bind).
- **Custo:** a CPU lê ~2.700 PTEs da VRAM por quadro de tela cheia que passe pela janela, e há ~22 pedaços síncronos por cópia. Isso pesa no FPS. É a correção funcional, não a otimizada.
- **Residência:** a página pode ser movida entre a caminhada e a cópia. É o mesmo risco que o caminho por MC já tinha.

**NÃO VALIDADO EM HARDWARE.**
