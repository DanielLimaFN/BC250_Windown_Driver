# Doc 745 — COMP.VA.2 (a janela espera) e COMP.VA.3 (GART de 32 MB e janela persistente), pacote 3.442.1.0 e critério da Corrida 292

> **Data:** 13/10/2026, offline. **Pedido do Daniel:** passos 1 e 2 do [doc 744](744_CORRIDA291_DESKTOP_E_3D_DE_VOLTA_COMPOSICAO_100PCT_PELA_JANELA_DA_GART_E_O_QUE_FALTA.md).
> **Base:** lab 3.441.1.0 (Corrida 291: desktop e 3D de volta, composição 100% pela janela da GART, com 22 cópias e 2.700 PTEs por quadro, e 5 recusas por "janela ocupada").

## 1. COMP.VA.2: a janela espera em vez de recusar

Na 291 as 5 recusas por "janela ocupada" foram exatamente os 5 `DXGI Present RECUSADO … E_FAIL` do SearchApp, o que deu as piscadas e as falhas da Pesquisa. A janela agora é protegida por um `KGUARDED_MUTEX`; o escape roda em PASSIVE_LEVEL. Quem chega com ela ocupada espera a vez. Primeiro tenta `KeTryToAcquire`; se precisar esperar, conta em `esperas=`.

## 2. COMP.VA.3: a tela inteira numa cópia só

- **GART do GFXHUB de 2 MB para 32 MB** (`BC250_GCVM_GART_ENTRIES` 512 → 8.192; tabela de 4 KB → 64 KB; `gart_end >> 12` = 0x1FFF). O amdgpu usa 512 MB a partir de MC 0 neste chip (coleta L-B). As entradas 0..511 mantêm o mapa antigo: MEC 0..31, D2 32..35, P7 64.., IH 128..191, SDMA1 224..225.
- **O MMHUB continua com 2 MB** (`BC250_MMHUB_GART_BYTES`). Ele só precisa do IH, e uma faixa maior cobriria endereços físicos baixos de outros clientes dele (a lição do PSP na Corrida 230). A tabela é a mesma; as entradas 0..511 não mudaram.
- **A tabela ganhou reserva própria no alocador** (papel GART, 64 KB, alinhada a 64 KB), de preferência em `0x0FF10000`, que estava livre no mapa DX.67 da 291 (0x0FF05000..0x0FF80000). Se o offset estiver ocupado, o alocador escolhe (`TOP_DOWN`). O offset real fica em `Context->D2GartTableOffset` e é o único que a D2, o MEC (V0.1) e o MMHUB usam. Sem reserva, a D2 não é tentada (fail-closed, o anel fica na VRAM) e a linha `[D2_GART] NAO TENTADO: reserva…` diz isso.
- **Janela da composição:** entradas 512..8191, com 3.840 páginas por lado. A tela de 2560×1080 (2.700 páginas) cabe inteira, então a cópia é feita em **1 pedaço**, contra 22 na 291.
- **Janela persistente:** com a tela inteira mapeada, a janela não volta para a dummy ao fim da cópia. Na composição seguinte, ela é reutilizada sem caminhar a tabela, sem regravar PTE e sem invalidar a TLB, desde que três condições valham:
  - mesma origem (VA, raiz, dono) e mesmo número de páginas;
  - a geração da tabela do dono não mudou (o `UpdatePageTable` do dono, ou sem dono, incrementa `g_Bc250CompVaPtGeneration`);
  - as PTEs da primeira, da do meio e da última página na tabela real são iguais às da janela.
  
  Se qualquer uma falhar, a tabela é caminhada de novo, as PTEs regravadas e a VMID 0 invalidada no anel (`FlushGart=TRUE`). O mapeamento só é marcado reutilizável se a geração não mudou durante a caminhada.
- O caso em pedaços (superfície maior que a janela) continua existindo e não reutiliza nada.

## 3. Testes e pacote

- `gcvm_global_mmu_test`: tabela de 8.192 entradas e `PT_END = 0x1FFF`. `mmhub_gart_test` continua com `0x1FF` (o MMHUB não mudou).
- Stubs de `KGUARDED_MUTEX` no `active_miniport_test`.
- `build-contract` EXIT=0; auditoria de segurança PASS; auditoria do pacote PASS.
- **Pacote lab 3.442.1.0:** SYS assinado `D6D57307…` (fonte `247D22DF…`), INF `12E2F4A2…`, CAT `A33A59CC…`. UMD `B73F9BDA` e RADV `B4AE2E14` iguais. As strings `TABELA RESERVADA`, `total reuso=` e `COMPOSITE_BLT OK VIA_VA` estão no SYS.
- **Roteiro:** `tools/EXECUTAR_CORRIDA292_COMPVA23_300S.cmd` (preflight `tools/prepare-corrida292-compva23.ps1`).

## 4. Critério da Corrida 292 (escrito antes)

| Desfecho | Assinatura no log | Implicação |
|---|---|---|
| **A** | `[D2_GART] TABELA RESERVADA` e `CONTEXTO0 … END=0x1FFF`; `OK VIA_VA` de tela cheia com `pedacos=1`; `total reuso` > `remapeios`; zero `VIA_VA RECUSADO`/`FALHOU`; zero `DXGI Present RECUSADO` no SearchApp; zero TDR; desktop visível e mais fluido (operador) | passos 1 e 2 fechados; seguir para o render target da RADV na VRAM ou zero cópia |
| **B** | igual a A, mas `reuso` ≈ 0 | a tabela do DWM muda todo quadro (ou a conferência das 3 PTEs falha); medir quem incrementa a geração; a cópia única já é ganho |
| **C** | `[D2_GART] NAO TENTADO: reserva…` ou `CONTEXTO0` com falha | a GART não subiu: sem COMP.VA a tela fica preta como na 290; minerar o mapa DX.67 e o status do alocador |
| **D** | TDR, 0x116, fault ou trava logo depois de a D2 ligar | a GART de 32 MB afetou o MEC, o anel ou o IH; voltar a 512 entradas e minerar |
| **E** | algo fora do mapa | descrever antes de julgar |

Antes: desligar e ligar. Depois que o desktop subir, rodar `tools\RODAR_PROCESSO_NOVO.cmd` como administrador. Testar menu, Pesquisa (digitar) e arrastar janelas.

**NÃO VALIDADO EM HARDWARE.**
