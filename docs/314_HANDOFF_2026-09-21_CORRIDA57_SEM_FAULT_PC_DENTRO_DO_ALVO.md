# Handoff — Corrida 57: sem fault latchado, PC dentro do alvo

## Resultado físico

Corrida de 21/09/2026 17:11:51, candidato lab 3.218.1.0, SYS
`05731245B6000F196D1F7D815A3181E77B21A39CEF0169CCE4A9F16B00A056CF`.
Evidência: `docs/evidence/2026-09-21_17-11-51-DESKTOP-HQAG6T4-lab-binding/`.

O desfecho foi E do critério 313. A carga foi suficiente (2.173 IBs SDMA), porém os cinco
TDRs trouxeram `FaultStatus=0`; a raiz retida, a caminhada e o nome do alvo não foram
exercitados. Os cinco restarts foram provados, não houve BugCheck observado e o rollback
terminou online com BasicDisplay Código 0.

O produto continuou falhando: o harness D3D11 completou 0/600 quadros, com Present
`0x887A0005` e remoção `0x887A0020`; dois BYTE_PROOF foram inconclusivos, zero contra zero.

## Achado que muda a próxima pergunta

Nesta corrida, o KMD registrou:

```text
[UpdatePT] Lvl=0, FirstPteVA=0x200040000, Num=64, ... Repeat=1
```

O RADV atribuiu a mesma VA a uma alocação de 262.144 bytes. Na Corrida 45, a onda parada
tinha `PC=0x20004114C`: offset `+0x114C` dentro dessa faixa. Logo `0x200040000` é candidato
forte a arena de código/instruções. Ainda não sabemos se o DXGK entregou `NoExecute`, qual
foi a PTE codificada, nem qual token nomeia a alocação; não transformar a correlação em causa.

## Faixa offline concluída

`Bc250ActiveUpdatePageTableHandler` agora reconhece toda atualização que cobre
`0x200040000` e emite `[DX_TARGET_200040] MAP` com:

- processo, alocação, token, classe e tamanho;
- base, número de páginas e índice do alvo;
- `Valid`, `Zero`, segmento, PFN, `ReadOnly`, `NoExecute`, `CacheCoherent` e flags brutas;
- PTE codificada e status da codificação.

A linha foi isenta da amostragem. O caminho apenas descreve os dados e a codificação que já
seria feita; não acrescenta escrita MMIO nem muda a PTE.

Validação offline: `build-contract.bat` EXIT=0 e safety audit PASS. Três mutações fizeram o
portão falhar ao remover: permissões, isenção do amostrador e marcador dedicado.

## Próximo ensaio

Repetir o perfil DISPLAY_RENDER_LAB com o novo candidato. O sucesso diagnóstico mínimo não
depende de reproduzir o fault: basta uma linha `[DX_TARGET_200040] MAP` coerente. Se
`NoExecute=1` ou a PTE sair sem bit 4, a próxima correção é de permissão/codificação. Se
`NoExecute=0` e `EncodedPte` tiver `EXECUTABLE`, o alvo está executável e a investigação deve
seguir para conteúdo/publicação da arena e estado da onda, sem rearmar `GfxWaveProbe`: a
Corrida 45 mediu `selector_stuck`, e a decisão vigente é mantê-la desarmada.

## Candidato pronto

Lab 3.219.1.0, assinado, auditado e não instalado:

- SYS `5E53951F2A1F89D91C3B3815C9615F6870DC519B053D292617832F72C46FBC8C`;
- PDB `8D3B95D491A0AB258663BC3256F30D2E1B937A5E1A60CD4221CC6B117DD092E5`;
- UMD `934DBD84E04F10024F6B7CAB28FBE014CAA41D35E103600F160BBF395AB5236A`;
- INF `6EE2DE01ED51978B81853ABB58F6CFA2668764687302E38C9258D533995F4D95`;
- CAT `863179B8CB0D64934EECFAC415D64262899DADA466A6A0A998C7BD24A286688B`.

O dry-run do binder passou e emitiu o comando da Corrida 58. Critério congelado antes do
ensaio em `docs/315_CRITERIO_CORRIDA58_PTE_DO_ALVO_200040.md`.
