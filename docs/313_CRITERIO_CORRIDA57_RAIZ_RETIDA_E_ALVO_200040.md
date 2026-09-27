# Critério da Corrida 57 — raiz retida e alvo `0x200040`

> Escrito antes do ensaio. Candidato lab **3.218.1.0**, SYS assinado
> `05731245B6000F196D1F7D815A3181E77B21A39CEF0169CCE4A9F16B00A056CF`.
> Esta corrida mede DX.10/DX.11 no silício; não promete corrigir a tradução nem
> produzir pixel. Pacote auditado e **NÃO INSTALADO**.

## Perguntas

1. Um fault em VMID cujo contexto já morreu ainda consegue caminhar a tabela correta?
2. Qual alocação viva contém `0x200040 << 12 = 0x200040000`?

## Controles mínimos

- Volume de `IB EXECUTADO` comparável aos 1.611 da Corrida 56.
- `TDR_RESTART PROVADO` para cada TDR observado, zero falso sucesso por rollback.
- O candidato, SYS assinado, horário e pasta de evidência devem ser arquivados.

## Desfechos

### A — medição completa

Assinatura: fault em VMID do pool com `ORIGEM_DA_RAIZ ... RETIDA_POS_MORTE`,
seguido de `WALK ... RESOLVEU` ou `NAO RESOLVE`, e `ALVO 0x200040` publicando
`Nome`, `Token`, `Alloc`, `Base`, `Size` e `Offset` (ou ausência explícita).

Implicação: DX.10/DX.11 estão exercitados no silício. O conteúdo da PTE e o
nome da alocação decidem a próxima correção; nenhum dos dois desfechos, sozinho,
fecha DX.5.

### B — só há raiz viva

Assinatura: todos os faults usam `VIVA_READY`; nenhum contexto faltante exige a
retenção.

Implicação: a corrida não exerceu DX.10. O resultado é inconclusivo para a
retenção, mesmo que a caminhada funcione.

### C — retenção ausente ou incoerente

Assinatura: `AUSENTE` para VMID previamente instalado, ou contexto/geração não
coincidem com a última instalação registrada daquele VMID.

Implicação: defeito nosso de ciclo de vida ou snapshot. Não interpretar a
caminhada e não avançar para correção de tradução.

### D — alvo sem nome

Assinatura: `ALVO 0x200040` traz `SEM_TOKEN`, `CLASSE_DESCONHECIDA`, ou nenhuma
alocação viva.

Implicação: é informação, não falha automática. Correlacionar handle/base com
`CreateAllocation` e `UpdatePageTable`; se o objeto morreu, decidir se o token
precisa sobreviver também ao teardown da faixa.

### E — inesperado

Descrever antes de julgar: novo endereço/CID/RW, fault fora dos VMIDs 1–5,
BugCheck, TDR sem restart, inconsistência entre o alvo e a caminhada, ou volume
insuficiente para reproduzir a condição.

## Veredito permitido

Sucesso somente para a medição DX.10/DX.11 quando o desfecho A ocorrer. A
tradução DX.5 continua aberta até submissões GFX comparáveis completarem sem
fault e com efeito do IB medido.

## Procedimento

Boot frio obrigatório. O bind é o mesmo perfil `DISPLAY_RENDER_LAB` da Corrida
56, com RenderNode, ShaderDispatch, PersistentSubmission, GpuvaLabPromotion,
gate por processo, D3D11LabPromotion, tradutor e captura de crash habilitados.
O comando deve ser obtido do dry-run do binder; não reconstruí-lo de memória.

Hashes do pacote: PDB `491B9951D62E5F7B...`, UMD `934DBD84E04F1002...`, INF
`003E14F3CDC09B66...`, CAT `D4700F95B49E2136...`.

## Resultado — 21/09/2026 17:11:51

**Desfecho E; veredito INCONCLUSIVO para DX.10/DX.11 e FALHA funcional.** Evidência:
`docs/evidence/2026-09-21_17-11-51-DESKTOP-HQAG6T4-lab-binding/`.

- O controle de volume passou: 2.173 `IB EXECUTADO (INDIRECT)`, contra 1.611 na Corrida 56.
- Houve cinco TDRs e cinco `TDR_RESTART PROVADO`, sem BugCheck. Porém os cinco snapshots
  disseram `SEM FAULT LATCHADO (FaultValid=0x7 Status=0)`. Logo não houve `DRENO #`,
  `ORIGEM_DA_RAIZ`, caminhada nem `ALVO 0x200040`; a retenção pós-morte e o token não foram
  exercitados no silício.
- O stall gráfico persistiu: nos cinco TDRs o `CP_RB0` ficou ativo com `RPTR != WPTR`,
  `GUI_ACTIVE=1`, `CP_BUSY=1` e ondas vivas (6–18 em SA0 e 6–14 em SA1). As 107 linhas de
  tripé tiveram fence escrita pela GPU, mas todas mantiveram a perna 3 aberta; 90 convergiram
  pelo RPTR e 17 não convergiram. O maior contador observado foi `Submetidas=131,
  Aposentadas=130, EmVoo=1`.
- O pool continuou operacional: 31 instalações físicas, distribuídas em VMID01=7,
  VMID02=13, VMID03=6, VMID04=4 e VMID05=1; zero exaustão.
- O harness D3D11 falhou honestamente no primeiro quadro: `Present=0x887A0005`,
  `DeviceRemovedReason=0x887A0020`, 0/600 quadros. Houve 10 Presents KMD e dois
  `BYTE_PROOF INCONCLUSIVO`, ambos zero contra zero.
- Correlação nova, sem alegar causalidade: o KMD mapeou 64 páginas a partir de
  `0x200040000`; o RADV identifica a operação como uma alocação de 262.144 bytes. O PC da
  onda medido na Corrida 45, `0x20004114C`, fica exatamente no offset `+0x114C` dessa faixa.
  Isso torna o alvo candidato forte a arena de código/instruções, mas a permissão e a PTE
  não foram arquivadas nesta corrida.
- Rollback online sem reboot: `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em cinco amostras.
  Isso é recuperação, não sucesso do desktop BC-250.
