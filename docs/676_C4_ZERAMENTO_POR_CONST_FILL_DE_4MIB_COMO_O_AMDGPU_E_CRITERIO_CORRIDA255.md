# 676 — C4: zeramento de memória nova por CONST_FILL de 4 MiB, como o `amdgpu`. Critério da Corrida 255

**Data:** 09/10/2026. **Natureza:** offline, sem corrida. Pedido do Daniel: "faça o C4" (doc 671).

## 1. A diferença (P1 e P0 do nosso código)

**Linux (`amdgpu_fill_buffer`, `inc/linux_ref/sdma_v5_0.c:1863-1883`, P1):**
- Pacote `SDMA_OP_CONST_FILL` com `count = byte_count - 1`.
- Fatias de até `fill_max_bytes = 0x400000` (4 MiB).

**Nós até a 254 (`bc250_mcdm_callbacks.c`):**
- Padrão 0 (zeramento de alocação nova): **X2b**, uma **cópia** de uma região de zeros de **32 KiB** na frente do paging buffer.
  - São 8.192 DW de zeros mais uma `COPY_LINEAR` por 32 KiB.
  - A primária de 11.075.584 B virava 338 cópias, com o dobro do tráfego de memória (lê e escreve).
- Padrão ≠ 0: CONST_FILL em fatias de 256 KiB (**X2a**).
- A X2a e a X2b eram ensaios nossos (docs 523 e 561). A 203 já mostrou que a cópia não cura a trava (doc 562).

## 2. C4 (o que mudou)

**Chave:** `BC250_C4_ZERAMENTO_COMO_O_AMDGPU 1` (`bc250_mcdm_callbacks.c`).
- `BC250_X2B_FILL_BY_COPY` passa a 0. A cópia de zeros fica compilada fora, dormente.
- `BC250_SDMA_PAGING_FILL_MAX_BYTES = BC250_SDMA_CONST_FILL_PACKET_MAX_BYTES = 0x400000`.
- Todo FILL de paginação (padrão 0 ou não) sai em CONST_FILL de até 4 MiB. A primária medida vira 3 pacotes (4 MiB, 4 MiB e 2.686.976 B): 15 DW, contra 10.560.
- Com a chave em 0, X2a e X2b voltam.

**Encoder (`bc250_sdma_packet_builder.h/.c`):**
- O pacote aceita até 0x400000 (`count = 0x3FFFFF`, cabe nos 22 bits), como o `amdgpu`.
- O teto da Mesa (`0x3FFFFC`, `BC250_SDMA_FILL_MAX_BYTES`) continua nos chamadores do present, que não mudam.
- Isso desfaz o SDMA.4M (28/09). A ligação daquela regra com 4 MiB exatos era H fraca: 333–379 fills de 4 MiB por corrida passaram sem trava.

**Prova no log:**
- Uma linha, no primeiro FILL: `[C4_FILL] chave=1 X2b=0 fatia_max=0x400000: ...`.
- Cada FILL segue em `[DX91_PAGING_MOVE] op=FILL ... Pattern=0x00000000 DW=n`, linha fora da amostragem.

**Testes:**
- `mcdm_callbacks_test`: 13 fatias de 4 MiB num buffer de 64 DW (12 + 1 por multipasse).
- `mcdm_callbacks_test`: a primária medida em 3 pacotes, decodificada pela prova como `OP_FILL`, padrão 0.
- `mcdm_callbacks_test`: buffer pequeno demais pede multipasse sem progresso.
- `sdma_packet_builder_test`: 0x400000 aceito com `count=0x3FFFFF`; 0x400004 recusado.

**Portão:** `audit_c4_zeramento_como_o_amdgpu`, com 6 mutações, todas reprovadas:
- chave em 0;
- X2b religada sob a C4;
- fatia de volta a 256 KiB;
- teto do pacote diferente do `amdgpu`;
- encoder de volta ao teto da Mesa;
- teste afrouxado.

As auditorias X2a e X2b ficam dormentes sob a C4.

**Build e auditorias:**
- `build-contract` EXIT=0 e build de laboratório EXIT=0.
- Auditorias de segurança, matriz de DDI e no-op: PASS.
- Auditoria do pacote lab: PASS. A primeira passada do empacotador deu IOException no `Copy-Item` e a auditoria reprovou; repetida, passou.

**Diferença que sobra, declarada:**
- O cabeçalho do `amdgpu` deixa `fillsize` em 0; o nosso usa 2 (DWORD, formato da Mesa).
- Com padrão 0 o resultado em memória é o mesmo (H). Não mexi para não trocar duas coisas na mesma corrida.

## 3. Pacote lab 3.401.1.0

| | |
|---|---|
| SYS assinado | `A022A815F2C2362C7EDADFE569E780F8FF8E4BCEABA8BE6E3ADD99486C7BE26E` |
| SYS fonte | `09AE46D9…` |
| INF | `E26E959D…` |
| CAT | `9039A7BD…` |
| UMD | `B73F9BDA` (igual) |
| RADV | `B4AE2E14` (igual) |
| Backup | `driver/wdk/package/output_lab_3.401.1.0_255` |
| Lançador | `tools/EXECUTAR_CORRIDA255_C4_CONST_FILL_MENU_600S.cmd` |

- Strings conferidas no SYS assinado: `[C4_FILL] chave=`, `[C3_VRAM_WC] chave=` e `[C1_GFX_DOORBELL] ATIVA` presentes; `FILL_POR_COPIA` ausente.
- C3, C1 e MON.1 seguem ativos.

## 4. Critério da Corrida 255 (escrito antes)

**Antes de rodar:** `tools\BC250_RECOVERY.cmd` elevado, `tools\inspect-boot-fuse.ps1 -Reset`, desligar (boot frio). Roteiro: só o menu.

**Pré-condição:**
- `[C4_FILL] chave=1 X2b=0 fatia_max=0x400000`.
- Zero `op=FILL_POR_COPIA` e zero `[X2B_FILL]`.
- FILLs com `Pattern=0x00000000` em `op=FILL`.
- `[C3_VRAM_WC] chave=1 P1b=1 ... =N` com N ≥ 4.
- `[C1_GFX_DOORBELL] ATIVA`.
- Desktop visível.
- Zero `GFXHUB_FAULT`.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Pré-condição completa e **600 s sem trava** | O zeramento por cópia era o mecanismo provável. Repetição obrigatória |
| **B** | Pré-condição completa e trava dura de sempre (GPU ociosa) | O zeramento sai das causas. A C4 fica (paridade, menos tráfego). A lista C1–C4 se esgota: o próximo passo é um novo inventário de paridade |
| **C** | Falta `[C4_FILL]`, ou aparece `FILL_POR_COPIA` | A C4 não armou. A corrida não responde |
| **D** | Fault ou timeout num IB de FILL, desktop com lixo ou preto em área recém-alocada, ou TDR do SDMA0 | O pacote de 4 MiB tem defeito no nosso caminho. Voltar a chave para 0 |
| **E** | Fora do mapa | Descrever antes de julgar |

**Teto:** 2 corridas.

**Nota de honestidade:**
- Antes da X2a (29/09) o zeramento já era CONST_FILL de ~4 MiB, e a máquina travava do mesmo jeito (com outra VRAM).
- A C4 entra por paridade e performance. A expectativa honesta é o desfecho B.

**NÃO VALIDADO EM HARDWARE:** a C4.
