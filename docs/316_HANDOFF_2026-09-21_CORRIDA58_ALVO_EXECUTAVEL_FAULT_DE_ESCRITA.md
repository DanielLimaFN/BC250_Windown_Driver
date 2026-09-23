# Handoff — Corrida 58: alvo executável; fault real é outra GPUVA

## Identidade e evidência

- Data/hora: 21/09/2026 17:55:20.
- Candidato: lab 3.219.1.0.
- SYS: `5E53951F2A1F89D91C3B3815C9615F6870DC519B053D292617832F72C46FBC8C`.
- Evidência: `docs/evidence/2026-09-21_17-55-20-DESKTOP-HQAG6T4-lab-binding/`.
- Critério congelado antes do ensaio: `docs/315_CRITERIO_CORRIDA58_PTE_DO_ALVO_200040.md`.

## Veredito

**Houve avanço diagnóstico, não funcional.** A corrida é sucesso para a pergunta de
permissão da arena (desfecho B), com a ressalva de identidade D, e falha para o produto.

O endereço `0x200040000` não está perdendo a permissão de execução quando tem a geometria da
arena de shader. Em 25/25 mapeamentos válidos de 64 páginas e 256 KiB:

```text
ReadOnly=1 NoExecute=0 RawFlags=0x29 EncodeStatus=0 EncodedPte.low=0x31
```

Logo, o bit 4 `EXECUTABLE` está presente. A classe sai `CLASSE_DESCONHECIDA`; a correlação
com a arena/PC continua forte, mas o token RADV ainda não a nomeia semanticamente. Duas
outras linhas válidas sobre o mesmo endereço pertenciam a `RESOURCE` e `PRIMARY` grandes,
com `NoExecute=1`; são reuso/sobreposição de VA, não a arena de 64 páginas.

## Bloqueador observado

Seis TDRs ocorreram. Três não tinham fault latchado; três repetiram:

```text
GpuVa=0x2469A6000 Cid=8 Rw=1 WalkerError=6 MappingError=1 PermissionFaults=0x5
```

A GPUVA cai dentro da mesma faixa viva (`Base=0x246400000`, `Size=0x5C0000`,
`Offset=0x5A6000`, segmento 1). Nas duas primeiras ocorrências a tabela mudou depois da
submissão, então a caminhada posterior não prova o instante do fault. Na terceira:

- fault em VMID 2;
- raiz `VIVA_READY` do contexto correto;
- época de publicação idêntica antes/depois (`TABELA INALTERADA`);
- PDE válida e PTE válida `0x0000000458F72071`;
- resolução `0x2469A6000 -> 0x458F72000`.

Os bits baixos `0x71` contêm `VALID|EXECUTABLE|READABLE|WRITEABLE` pelo contrato local.
Portanto, para essa ocorrência, não faltava a folha nem permissão R/W/X na imagem
CPU-visível. Isso desloca a investigação para visibilidade/invalidação da publicação,
semântica da hierarquia ou conteúdo/uso dessa alocação. Não escolher uma delas sem nova
medição.

## Controles e falhas

- 1.900 `IB EXECUTADO (INDIRECT)` no SDMA.
- 6/6 `TDR_RESTART PROVADO`.
- 26 instalações do pool e zero exaustão.
- GFX máximo `Submetidas=83 Aposentadas=81 EmVoo=2`.
- 107 vereditos: 86 RPTR convergentes, 21 não convergentes, 107 fences escritas pela GPU e
  107 pernas 3 abertas.
- D3D11: dispositivo e pipeline criados, mas primeiro Present falhou com `0x887A0005` e
  `DeviceRemovedReason=0x887A0007`; 0/600 quadros.
- Dois `BYTE_PROOF INCONCLUSIVO`; nenhum pixel não-nulo provado.
- Nenhum BugCheck ou dump novo observado no material coletado.

## Recuperação e estado

Rollback online sem reboot. `safe-bind-final.json` registra `RECOVERY_OK_ONLINE`, e o
BasicDisplay ficou em Código 0 por cinco amostras. Isso é recuperação, não sucesso do nosso
desktop. DX.5, D0.7 e todas as entregas de pixel continuam abertas.

## Próxima decisão — ainda não executada

Não alterar a permissão de execução de `0x200040000`: a medição a refutou como causa
suficiente. A próxima faixa deve discriminar por que uma escrita em `0x2469A6000` falha
apesar de uma folha R/W/X válida e contemporânea na visão da CPU. Nenhuma mudança de código
foi iniciada após esta análise.

Auditoria dirigida posterior encontrou o primeiro candidato concreto: o FlushTlb do VidMm
invalida apenas os VMIDs fixos 8/7/9, embora GFX agora submeta nos VMIDs dinâmicos do pool.
Uma atualização de PTE posterior à instalação pode, portanto, ficar CPU-visível sem invalidar
o VMID 1–6/10–15 usado pela GPU. Isto ainda é hipótese offline, não causa provada. Passagem
de bastão e critério de implementação em `docs/317_PASSAGEM_DE_BASTAO_POS_CORRIDA58_FLUSH_DOS_VMIDS_DO_POOL.md`.
