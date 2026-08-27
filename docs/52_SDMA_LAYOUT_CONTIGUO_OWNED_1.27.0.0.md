# Layout SDMA contiguo owned - pacote 1.27.0.0

Data: 2026-08-26.

Status: **RING E FENCE PROVADOS; COMPARACAO DE DADOS BLOQUEADA PELO GATE; ACELERACAO AINDA NAO VALIDADA**.

O 1.27 tenta primeiro o bloco legacy e registra a colisao. Em seguida reserva
um unico bloco CPU-visible owned de 20 KiB, escolhido top-down pelo allocator:

```text
base + 0x0000  ring
base + 0x1000  RPTR writeback
base + 0x2000  WPTR shadow
base + 0x3000  fence
base + 0x4000  destino WRITE_LINEAR
```

O schema 35 registra o status da reserva fixa, modo de alocacao, enderecos MC
e todos os diagnosticos SDMA/UTCL do 1.26. O auditor exige modo dinamico owned,
contiguidade, `RPTR == WPTR == 68`, fence fisico, quatro dwords corretos e
rollback integral.

Build completo, contratos offline, assinatura e auditoria: PASS.

- source SYS: `676E0C74786AB3989B8E39B315FEFE8902483B8B8AA8B5F106920AD0F640214D`;
- signed SYS: `7F0A61C08613BD583C3BCA5B86608C501F084B719D3E2D4768005CA04156B1E1`;
- INF: `38A83D359ADE7B79805A026E6C0AA84D4124F8818196831495A93D02246D4E41`;
- CAT: `A59F8EAE67A139568583D4F57E85BC745F410FB51D6C7B7004AAADB4D4AD2DF7`.

**NAO VALIDADO EM HARDWARE**

## Resultado fisico

Evidencia: `docs/evidence/2026-08-26_18-12-58-DESKTOP-HQAG6T4-mcdm-replace`.

- reserva fixa colidiu (`5`) e o bloco contiguo dinamico owned foi usado (`2`);
- ring/work MC: `0xF40FBB9000/0xF40FBBA000`;
- `WPTR=68`, `RPTR MMIO=68` e RPTR writeback `52`;
- fence GPU: poison `0x43DAFFFE` para `0xBC250123`;
- F32: 13 transicoes; rollback registradores/memoria: `1/1`;
- a comparacao de dados nao ocorreu porque o gate antigo exigia que o
  writeback de RPTR, atrasado em um pacote, tambem chegasse a 68.

O mesmo atraso `RPTR writeback=52` com `RPTR MMIO=68` aparece na captura
legacy funcional. O 1.28 usa o RPTR MMIO para conclusao e conserva o writeback
como diagnostico, sempre junto da fence fisica e da comparacao dos dados.

**NAO VALIDADO EM HARDWARE**
