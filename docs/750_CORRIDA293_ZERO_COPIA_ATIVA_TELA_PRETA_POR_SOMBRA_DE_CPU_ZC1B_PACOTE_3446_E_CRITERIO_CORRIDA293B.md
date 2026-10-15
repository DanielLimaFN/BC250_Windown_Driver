# Doc 750 — Corrida 293: zero cópia ativa, tela preta pela sombra de CPU; ZC.1b, pacote 3.446.1.0 e critério da Corrida 293b

> **Data:** 14/10/2026, bind às 09:41:49, rollback online (`RECOVERY_OK_ONLINE`).
> **Candidato:** lab **3.445.1.0** (SYS `64D0E959`, UMD `9F65C18A`), [doc 749](749_PISC1_LATCH_MEDIDO_NO_REGISTRADOR_ZC1_PRESENT_SEM_COPIA_PACOTE_3445_E_CRITERIO_CORRIDA293.md).
> **Evidência:** `docs/evidence/2026-10-14_09-41-49-DESKTOP-HQAG6T4-lab-binding/`.
> **Observado pelo Daniel:** tela preta em quase tudo; aparecem certinho as regiões mexidas (por exemplo, a janela arrastada). Menu mais rápido.

## 1. O que a corrida mediu

| Medida | Valor | Nível |
|---|---|---|
| `COMPOSITE_BLT OK VIA_VA` (a cópia de cada quadro pelo KMD) | **0**, contra 5.376 na 292c | P0 |
| Primárias do DWM (#4–#6) | somem das linhas `FRENTE_B NAO`; o Present deixou de chamar a cópia antiga | P2 |
| Linhas `SIM_ZC1` e `[ZC1_PRESENT]` | não aparecem no log | — |
| PISC.1 | `contador_atrasado=115`, `tardios_reais=1` (só o 1º flip, 40,7 ms) | P0 |
| Estabilidade | zero TDR, fault ou BugCheck; GART de 32 MB de pé; rollback online | P0 |

- O zero cópia rodou, por P0 no KMD e P2 na UMD.
- As linhas `SIM_ZC1` e `[ZC1_PRESENT]` faltam por defeito meu: o LOGMIN.1 só deixa passar linhas com palavras-chave, e elas não tinham nenhuma.
- O balde "3+" era mesmo erro de contagem. As piscadas da 292c **não** vêm de latch atrasado.
- A T3 não rodou nesta janela.

Pelo critério do doc 749, o desfecho é **B**: regiões pretas fora do que é redesenhado, a Corrida 190 de volta.

## 2. Causa (P1 pelo código; explica também a 190)

O DWM monta cada quadro copiando do buffer anterior as regiões que não mudaram (`ResourceCopyRegion` com caixa). Na UMD essa DDI fazia duas coisas:
1. a cópia de GPU da caixa pedida (`Bc250UmdVkCopyImageInternal`), que está certa;
2. em seguida, a cópia da **sombra de CPU** e o upload da sombra **inteira** (`TotalByteSize`) para a alocação de destino.

Num render target a sombra de CPU é só zeros, porque quem desenha é a GPU.

- **Sem alias:** os zeros caíam só na alocação WDDM. O Present sobrescrevia a alocação inteira a partir da imagem, e o defeito ficava invisível.
- **Com alias:** a alocação **é** a imagem que o DCN mostra. Os zeros apagavam tudo, e só sobrevivia o que era desenhado depois, que é exatamente o observado.

O `ResourceCopy` tinha o mesmo padrão.

## 3. Correção: ZC.1b (pacote 3.446.1.0)

- **Copy e CopyRegion:** quando a cópia de GPU entre as duas imagens deu certo e o destino tem alias, a sombra de CPU **não** é copiada nem sobe (`Bc250UmdCopySkipsShadowUpload`, linha `[ZC1B]`). Sem alias, o comportamento não muda.
- **LOGMIN:** passa a deixar sair as linhas com `ZC1`, `FRENTE_B` e `PISC1`.
- **Auditoria:** dois portões novos.

**Build e pacote:**
- `build-umd` EXIT=0; build de laboratório OK; auditoria de segurança e auditoria do pacote PASS; `[ZC1B]` conferido na UMD.
- **Lab 3.446.1.0:** SYS `F3ED4592…`, UMD `36A7D0DC…`, INF `F321C3FE…`, CAT `23892FC5…`.
- **Roteiro:** `tools/EXECUTAR_CORRIDA293B_ZC1B_SEM_SOMBRA_300S.cmd`. Antes: desligar e ligar.

**Dívida observada e não tocada:** para destino sem alias, o upload da sombra inteira continua acontecendo a cada cópia. Sobre um render target de 11 MB, isso é escrita de CPU desperdiçada. Fica para depois que o alias for provado.

## 4. Critério da Corrida 293b

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | desktop inteiro visível e estável (Daniel); `[ZC1B]` > 0; `[ZC1_PRESENT]` > 0; `COMPOSITE_BLT OK VIA_VA` ≈ 0 no DWM; zero TDR | zero cópia validado; avaliar menu e piscadas |
| **B** | ainda há regiões pretas | existe outro escritor da alocação: procurar `Upload` e `Unmap` em render target com alias |
| **C** | piscadas iguais às da 292c com o desktop inteiro | latch descartado (PISC.1); próximos suspeitos são os VSYNC perdidos (D06) |
| **E** | algo fora do mapa | descrever antes de julgar |

**NÃO VALIDADO EM HARDWARE.**
