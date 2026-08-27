# Layout SDMA legacy e diagnostico UTCL - pacote 1.26.0.0

Data: 2026-08-26.

Status: **BLOQUEADO NO PREFLIGHT DE ALOCACAO; NENHUMA ESCRITA DE HARDWARE; ACELERACAO NAO VALIDADA**.

O 1.26 elimina a principal diferenca de memoria restante em relacao ao ensaio
legacy comprovado. O allocator precisa reservar, sem colisao, este bloco:

```text
0x0FF00000  ring de 4 KiB
0x0FF01000  RPTR writeback
0x0FF02000  WPTR shadow
0x0FF03000  fence
0x0FF04000  destino WRITE_LINEAR
```

O pacote e o valor de `WPTR=68` permanecem iguais ao 1.25. Se o tripe fisico
ainda falhar, o schema 34 registra `STATUS1/2/3`, `ERROR_LOG`, estados UTCL1,
XNACKs de leitura/escrita e timeout. Todos esses offsets sao P2 catalogados.

Gate de sucesso: `RPTR == WPTR == 68`, fence GPU fora do poison, quatro
dwords corretos e rollback integral. Uma colisao no allocator bloqueia o teste
antes de qualquer escrita.

Build completo, testes offline, assinatura e auditoria de integridade: PASS.

- source SYS: `030BDAAD903A65471BF5123D2AABCA83527464FB8FF2260FF7FF4B9AFBC81031`;
- signed SYS: `F1A35B36DC456D41AFF7775AF02FBDB7D0D6AD18D466769D4484448066C6FF4F`;
- INF: `F8391019BBDEA285DF2526A5A6B0308C7AEE9B7E837E7DFD5689FC747D186245`;
- CAT: `8C95F4962C1BFA41E7C0F6B3B0CD64A68E30D783E02DE2C7C56611D3998BDF93`.

**NAO VALIDADO EM HARDWARE**

## Resultado fisico

Evidencia: `docs/evidence/2026-08-26_18-09-01-DESKTOP-HQAG6T4-mcdm-replace`.

- firmware PSP e F32 permaneceram aprovados, com 15 transicoes de PC;
- `SdmaRingWorkStatus=2`: reserva fixa falhou antes do snapshot/apply;
- ring, WPTR, RPTR, fence e diagnosticos ficaram zero porque o ensaio nao foi
  iniciado;
- nenhuma escrita SDMA ocorreu e nenhum rollback foi necessario.

Conclusao: o bloco legacy exato colide com uma reserva owned existente. O
1.27 mantem o mesmo layout relativo em um unico bloco contiguo livre.

**NAO VALIDADO EM HARDWARE**
