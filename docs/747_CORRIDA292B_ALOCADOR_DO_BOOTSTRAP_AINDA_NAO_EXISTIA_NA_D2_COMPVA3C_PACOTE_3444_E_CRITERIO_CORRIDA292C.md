# Doc 747 — Corrida 292b: na D2 o alocador do bootstrap ainda não existia; COMP.VA.3c, pacote 3.444.1.0 e critério da Corrida 292c

> **Data:** 14/10/2026, bind às 00:18:28, rollback online (`RECOVERY_OK_ONLINE`, sem reboot).
> **Candidato:** lab **3.443.1.0** (SYS `ABAFF3ED`), [doc 746](746_CORRIDA292_GART_NAO_SUBIU_RESERVA_DA_TABELA_NO_SPACE_COMPVA3B_PACOTE_3443_E_CRITERIO_CORRIDA292B.md).
> **Evidência:** `docs/evidence/2026-10-14_00-18-28-DESKTOP-HQAG6T4-lab-binding/`.
> **Observado pelo Daniel:** tela preta com o cursor vivo e, de vez em quando, poucos artefatos.

## 1. O que aconteceu (P0)

- `[D2_GART] NAO TENTADO: reserva de 65536 bytes … falhou (status=3, 320 tentativas de 0xF3F0000 ate 0xE000000)`. O status 3 é `BC250_VRAM_OUT_OF_RANGE`, e veio em **todas** as 320 posições.
- **A cascata foi a mesma da 292:**
  - `P4_SDMA1`, `P3B_MMHUB`, `P3_IH` e `P7_GFX_GTT` ficaram `NAO TENTADO`;
  - `COMPOSITE_BLT VIA_VA RECUSADO … GART da VMID 0 desligada` (16 linhas, amostrado) e zero `COMPOSITE_BLT OK`;
  - resultado: tela preta.
- **O que continuou bem:**
  - zero TDR, fault ou BugCheck;
  - 62 flips por `SET_ADDRESS`;
  - rollback online sem reboot.
- O log da T3 arquivado é da 291 (sessão de 23:17), então a T3 não rodou nesta corrida.

Pelo critério do doc 746 o desfecho é **C**. A implicação que eu tinha escrito para C ("a VRAM abaixo também está ocupada") **está errada**; ver a causa abaixo.

## 2. Causa: defeito meu, e o diagnóstico do doc 746 estava errado

`OUT_OF_RANGE` em todas as posições quer dizer que `end > Allocator->TotalBytes` em todas elas. O alocador que eu usei, `Owned.Bootstrap.Allocator`, só é criado no **segundo preflight** (`Bc250StartDevicePrepareOwnership` → `Bc250BuildOwnedAdapterBootstrap`), e o log mostra a ordem (P0):

| Linha | Evento |
|---|---|
| 85 | MMU global programada |
| 87 | D2 tenta a reserva |
| 149 | segundo preflight |

Quando a D2 rodou, o alocador ainda tinha `TotalBytes=0`. A mesma causa explica a 292: o offset fixo deu `OUT_OF_RANGE` e o `TOP_DOWN` deu `NO_SPACE`, porque não havia espaço nenhum. O "topo da VRAM cheio" do doc 746 nunca foi o problema.

Ressalva: mesmo que a reserva tivesse passado, ela seria apagada, porque `Bc250BuildOwnedAdapterBootstrap` zera o alocador com `memset` antes de montá-lo.

## 3. Correção: COMP.VA.3c (pacote 3.444.1.0)

- **Onde a tabela é reservada:** agora no alocador da **MMU global** (`GlobalMmuAllocator`). Ele já existe na D2, porque é criado pela própria MMU global e já guarda o scanout e a dummy. A varredura é a mesma da 3b; a primeira posição, `0x0F3F0000`, deve passar.
- **Espelho no bootstrap:** logo depois do segundo preflight, `Bc250ActiveD2GartMirrorReservation` reserva a mesma faixa no alocador do bootstrap, antes do TMR (linha 153) e de qualquer alocação `TOP_DOWN` dele. Isso é necessário porque os dois alocadores não se conferem entre si. O resultado sai no log: `ESPELHO OK`, `ESPELHO FALHOU` (alto, porque a faixa poderia ganhar outro dono) ou `ESPELHO ADIADO`.
- **Segmento do VidMm:** o corte DX.67 já percorre os dois alocadores, então o segmento termina em `0x0F3F0000`. O VidMm perde 64 KB.
- **StopDevice:** o estado da reserva é zerado junto com o alocador da MMU global.
- **Log de falha:** passa a mostrar o tamanho do alocador (`total=`, `cpu=`), para que um alocador vazio apareça na hora.

**Build e pacote:**
- `build-contract` EXIT=0; build de laboratório OK; auditoria do pacote PASS; string `ESPELHO OK` conferida no SYS.
- **Lab 3.444.1.0:** SYS `218CFF90…`, INF `BB82F1E8…`, CAT `B593EBE3…`.
- **Roteiro:** `tools/EXECUTAR_CORRIDA292C_GART_ALOCADOR_MMU_300S.cmd` (preflight `tools/prepare-corrida292c-gart-alocador-mmu.ps1`). Antes: desligar e ligar.

## 4. Critério da Corrida 292c

| Desfecho | Assinatura no log | Implicação |
|---|---|---|
| **A** | `TABELA RESERVADA (VARREDURA no alocador da MMU global, 1 tentativas)`, `ESPELHO OK`, `CONTEXTO0 … END=0x1FFF`; `COMPOSITE_BLT OK VIA_VA … pedacos=1`; `reuso` > 0; zero `VIA_VA RECUSADO`; zero TDR; desktop visível | COMP.VA.2 e COMP.VA.3 validados (passos 1 e 2) |
| **B** | tabela reservada e GART ligada, mas o desktop cai (TDR, fault ou trava) | a GART de 32 MB afetou o MEC, o anel ou o IH; voltar à tabela de 4 KB |
| **C** | `NAO TENTADO: reserva…` de novo | ler `total=`/`cpu=` na mesma linha: o alocador da MMU global também não tem tamanho nessa hora |
| **D** | `ESPELHO FALHOU` | outra região do bootstrap já está nessa faixa; mudar o início da varredura |
| **E** | algo fora do mapa | descrever antes de julgar |

**Dívida declarada:** se a reserva falhar, ainda não existe a volta para a tabela antiga de 4 KB, e a tela fica preta.

**NÃO VALIDADO EM HARDWARE.**
