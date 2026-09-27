# Critério da Corrida 55 — o latch do fault e a janela de publicação

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.216.1.0**,
> SYS `BE9C7687...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT
> SUBMISSION. **Somente leitura — nenhum registrador novo é escrito.**
> **NÃO VALIDADO EM HARDWARE.**

---

## 0. Por que esta corrida existe

A Corrida 54 mostrou um fault em que **a PTE existe e é totalmente permissiva**
(`flags=0x71` = `VALID|EXECUTABLE|READABLE|WRITEABLE`) e ainda assim o acesso de
escrita falhou. Antes de atacar isso, a mineração encontrou duas razões para
duvidar da própria medição:

1. **Nunca lemos `GCVM_L2_PROTECTION_FAULT_CNTL` (`0x0A120`).** O bit 1,
   `ALLOW_SUBSEQUENT_PROTECTION_FAULT_STATUS_ADDR_UPDATES` (sh_mask 12092),
   decide se o par `STATUS`/`ADDR` acompanha o fault **mais recente** ou fica
   preso no **primeiro** até alguém escrever o bit 0. O header do projeto já
   registrava essa dúvida desde 07/09 e ela nunca foi respondida.
2. **A caminhada é post-mortem.** Ela roda no TDR, muito depois do fault. Se a
   tabela foi publicada nesse intervalo, a PTE válida lida agora pode ter
   nascido **depois** do fault.

Enquanto as duas estiverem abertas, atacar o caso "PTE existe" é atacar um
número cuja procedência não está estabelecida.

---

## 1. A pergunta única

**`AceitaAtualizacoes` é 1 ou 0?**

---

## 2. Linhas novas

```
[TDR_GCVM_FAULT] LATCH_CNTL=0x........ -> Clear=u AceitaAtualizacoes=u | enables: ... -> <veredito>
[TDR_GCVM_FAULT] JANELA_DE_PUBLICACAO: EpocaNaUltimaSubmissao=u EpocaAgora=u Geracao=l Maps=l Unmaps=l -> <veredito>
[PT_WALK FAULT_VMID_POOL] ...        (o rótulo antes dizia FAULT_VMID7 caminhando VMID 1/2/3)
```

---

## 3. Desfechos

### A — O latch acompanha o fault recente

**Assinatura:** `LATCH_CNTL` com `AceitaAtualizacoes=1`.

**Implicação:** as leituras de fault das Corridas 45–54 **valem como estão**. O
`Status` decodificado é do fault que causou aquele TDR. O caso "PTE existe"
continua de pé e passa a ser decidido pela `JANELA_DE_PUBLICACAO` — ver A1/A2.

- **A1 — `TABELA INALTERADA na janela`:** a PTE lida já valia no instante do
  fault. O defeito **não** é corrida de publicação. Sobram TLB obsoleto e a
  semântica de `PERMISSION_FAULTS`, e o próximo passo é a invalidação.
- **A2 — `TABELA MUDOU na janela`:** a PTE pode ter nascido depois do fault. A
  conclusão "a PTE existe, logo o defeito não é PTE ausente" **cai**, e o alvo
  passa a ser a ordem entre publicação e submissão.

### B — O latch está preso

**Assinatura:** `LATCH_CNTL` com `AceitaAtualizacoes=0`.

**Implicação:** **dez corridas leram um latch velho.** Todo `Status`, `Vmid`,
`WalkerError` e `FaultAddr` desde a Corrida 45 passa a carregar a ressalva de
que pode ser do primeiro fault do bind, não do que causou o TDR. Isso não apaga
o que foi medido por outros meios (a caminhada, o mapa do pool, a instalação),
mas **reabre** as conclusões que se apoiaram no endereço do fault — entre elas
a granularidade `<<20` da Corrida 47.

O conserto passa a ser escrever `CLEAR_PROTECTION_FAULT_STATUS_ADDR` depois de
cada leitura, o que exige entrar na allowlist de **escrita** e tem critério
próprio. **Não é feito nesta corrida**, de propósito.

### C — O CNTL não é lido

**Assinatura:** `LATCH_CNTL NAO LIDO`.

**Implicação:** a allowlist de leitura ou o transporte recusaram. Defeito nosso,
localizável por leitura, sem gastar outra corrida.

### D — Não há fault

**Assinatura:** nenhum `DECODIFICADO`, com `IB EXECUTADO` em volume comparável
(2.134 na Corrida 54).

**Implicação:** a pergunta não chegou a ser feita. **Não é sucesso** — é corrida
sem o evento. Nada muda de status.

### E — Algo fora do mapa

**Instrução:** descrever o observado antes de julgá-lo. A observar: os
`enables` do `CNTL` divergirem do que o POST deixou; `MoreFaults=1` deixar de
aparecer; o rótulo `FAULT_VMID_POOL` sair em fault que nomeia VMID 7 (seria
defeito meu); BugCheck.

---

## 4. Controles

| Controle | Corrida 54 |
|---|---|
| BugCheck | zero em 10 corridas |
| IBs executados | 2.134 |
| Instalações OK | 28 de 28 |
| Recusas de VMID | 0 |
| Rollback | `RECOVERY_OK_ONLINE` |

**Risco desta faixa: baixo.** Só leitura; nenhum registrador novo é escrito, e
um portão reprova se `0xA120` entrar na allowlist de escrita.

**Tela preta continua correta.**

---

## 5. Procedimento

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

---

## 6. Proveniência

| Artefato | SHA-256 (início) |
|---|---|
| SYS assinado | `BE9C768798698909...` |
| PDB | `641EDE9F6FE2E9FA...` |
| UMD | `934DBD84E04F1002...` (inalterada) |
| INF | `4302E9CC9F914ACD...` |
| CAT | `417CE5C4AD64D6C7...` |

`Lab package audit: PASS`. Strings conferidas **em ANSI**: `LATCH_CNTL=`,
`LATCH PRESO`, `ACEITA ATUALIZACOES`, `JANELA_DE_PUBLICACAO:`,
`TABELA INALTERADA na janela`, `TABELA MUDOU na janela`, `FAULT_VMID_POOL`.

---

## 7. Estado offline

`build-contract.bat` **EXIT=0**, três auditorias **PASS**, `git diff --check`
limpo.

**Nove mutações verificadas, todas reprovando:** CNTL deixando de ser lido;
veredito do latch perdendo o lado "preso"; falha de leitura virando muda;
`0xA120` entrando na allowlist de **escrita**; `0xA120` saindo da de leitura;
janela virando carimbo sem comparar as épocas; janela perdendo o desfecho
"tabela mudou"; carimbo voltando à raiz do VMID 7; rótulo da caminhada voltando
a ser fixo.

**Um portão meu quase passou por fatia vazia:** escrevi
`Bc250MmioTopologyReadOffsetAllowed` e a função chama-se
`Bc250MmioTopologyOffsetAllowed`. Agora ele reprova se não localizar a função —
falha não-atribuível é portão fraco.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
