# Handoff para a próxima IA — estado após o candidato 3.39

Data local: 30/08/2026
Último pacote ensaiado: **3.39.0.0 / SchemaVersion 110**
Veredito: **NÃO VALIDADO EM HARDWARE**

Este documento é autocontido. Para detalhes ciclo a ciclo, leia depois os docs
112, 114, 116, 117 e 118.

---

## 1. Objetivo e critério inegociável

Precisamos executar um shader real nos CUs e fechar, no mesmo ciclo físico:

1. `RPTR == WPTR == ShaderDispatchDwords`;
2. fence posterior ao `DISPATCH_DIRECT` saindo de poison por escrita da GPU;
3. resultado saindo do poison e conferido byte a byte;
4. memória e registradores restaurados;
5. nenhuma simulação por CPU.

Sem qualquer item: **NÃO VALIDADO EM HARDWARE**.

## 2. Estado exato do bloqueio

O ring e sua tradução estão corretos, `WPTR=40` é publicado pelo hardware e o
silício reconhece a fila como não vazia. O RPTR, porém, permanece zero e os
poisons não mudam.

O 3.39 capturou `CP_CPF_BUSY_STAT` em três pontos:

```text
antes da doorbell   0x68460000
depois da doorbell  0x68460000
polling final       0x68460000
```

Bits ativos:

```text
0x40000000  HQD_PQ_BUSY
0x20000000  HQD_WPTR_POLL_BUSY
0x08000000  HQD_ROQ_PQ_BUSY
0x00400000  HQD_CONSUMED_RPTR_BUSY
0x00040000  HQD_PQ_FETCHER_BUSY
0x00020000  HQD_MESSAGE_BUSY
```

Conclusão forte: o estado ocupado já existe **antes da doorbell**. A causa
primária está no protocolo disparado por `CP_HQD_ACTIVE` e/ou na publicação
inicial do WPTR, não no kick BAR2.

## 3. O que os candidatos 3.35–3.39 provaram

| Candidato | Mudança | Resultado físico |
|---|---|---|
| 3.35 / 106 | ME2 header dump + ring DW0..7 | RPTR ficou 0; dump não mostrou header |
| 3.36 / 107 | FIFO ME2 antes/depois + CPC pré/pós-kick | Doorbell acende MEC2 e stalls causalmente |
| 3.37 / 108 | bit compute somente no primeiro `SET_SH_REG` | `DECODING_PACKET` desapareceu; RPTR 0 |
| 3.38 / 109 | bit compute nos seis `SET_SH_REG` | espera RCIU deixou de persistir; RPTR 0 |
| 3.39 / 110 | CPF busy antes/depois/final | CPF já ocupado antes do kick, invariável |

Há variabilidade entre boots: `DECODING_PACKET`, transições do PC e stalls
mudam. Não transforme uma única amostra em regra obrigatória. Compare sempre
os pontos dentro da mesma transação.

## 4. Estado do PM4 atual

O construtor em `driver/core/bc250_compute_dispatch_contract.c` marca todos os
seis `SET_SH_REG` com `BC250_PM4_SHADER_TYPE_COMPUTE`. Os primeiros dwords
relidos no 3.38 confirmaram:

```text
DW0  C0027602
DW4  C0027602
```

Não reverta isso sem nova evidência. Não altere PM4 no próximo ciclo: o objetivo
é localizar a transição CPF dentro da ativação.

## 5. Próximo candidato mínimo: 3.40 / Schema 111

Adicionar somente observações de `CP_CPF_BUSY_STAT` (`BAR5 0x08220`, read-only):

1. imediatamente antes de escrever `CP_HQD_ACTIVE=1`;
2. imediatamente depois do readback de `ACTIVE=1`;
3. antes da publicação/observação inicial do WPTR;
4. depois dessa publicação e antes da doorbell.

Mantenha as três amostras do 3.39. Não adicione escrita, atraso, reset ou abort.
A etapa nova entra como observação até o silício provar que é requisito.

Predição falsificável:

- se CPF passa `0 -> 0x68460000` no `ACTIVE`, o protocolo de ativação é o
  limite causal;
- se permanece 0 após `ACTIVE` e muda após WPTR, a publicação do cursor é o
  limite;
- se já entra ocupado antes de `ACTIVE`, existe estado residual anterior e o
  próximo ciclo deve recuar a instrumentação, não mudar PM4.

## 6. Rollback e segurança

O rollback interno varia:

- 3.37/3.38: primeira falha em `HQD_PLAN`, `CP_HQD_PERSISTENT_STATE`;
- 3.39: duas falhas, primeira em `ACTIVE_DEACTIVATE`, `CP_HQD_ACTIVE`
  esperado 0, observado 1.

O rollback externo do 3.39 concluiu e restaurou `BasicDisplay`, Código 0.
Isso não transforma o rollback interno em aprovado.

Uma única submissão física por boot. Fluxo obrigatório:

```text
rollback externo -> reboot manual -> quarentena -> prebinding PASS
-> uma submissão -> confirmação factual do display -> acceptance read-only
-> documentação -> rollback externo
```

## 7. Estado atual da máquina

Após o 3.39:

- dispositivo: `BasicDisplay`, Código 0;
- serviço/chave live do `bc250mcdm`: ausentes após rollback;
- pacote 3.39/Schema 110 já foi ensaiado;
- houve submissão neste boot: **reboot manual obrigatório antes do 3.40**.

O arquivo residual `C:\Windows\System32\drivers\bc250mcdm.sys` costuma ficar
após rollback. Use somente `tools/quarantine-bc250mcdm-lab-residue.ps1` com a
aceitação exata; nunca apague alvos amplos.

## 8. Artefatos do último pacote

```text
DriverVer  08/30/2026,3.39.0.0
Schema     110
SYS SHA256 0A7CED569A5F9BEA5ADE852A058FE6F4DF837ACB812EC7AFDCDAE117AEBBD70B
INF SHA256 253FC4655EB41EB868AEA31A950196B94AAC5592547D333E19DACE840EE170CD
CAT SHA256 76A88174BDED6B4DBD0E10B1C4F8E6733DBC4D554A6A80C45A7DE54F17F5DE8A
```

Evidência canônica do 3.39:
`docs/evidence/2026-08-30_13-45-57-DESKTOP-HQAG6T4-lab-binding/`.

## 9. Armadilhas operacionais

- O `bind-bc250mcdm-lab.ps1` abre uma janela limitada pelo watchdog. Inicie-o
  sem bloquear a conversa, leia `_last_lab_binding.json`, confirme `Code 0` e
  peça imediatamente ao operador se a tela está visível.
- Use o token exato no `confirm-bc250mcdm-lab.ps1`; confirmação tardia não
  desarma watchdog já encerrado.
- O acceptance exige telemetria live. Rode-o antes do rollback externo.
- `CP_MEC_ME2_HEADER_DUMP` é FIFO: cada leitura avança; não leia no polling.
- Não confunda build/`STATUS_SUCCESS` com prova física.
- Preserve mudanças existentes: o worktree está deliberadamente sujo.

## 10. Arquivos para ler primeiro

1. `CLAUDE.md`
2. `AGENTS.md`
3. `BC250_HARDWARE_RULES.md`
4. este documento
5. `docs/118_RESULTADO_FISICO_3.39.md`
6. `driver/core/bc250_compute_mqd_contract.c/.h`
7. `driver/core/bc250_compute_dispatch_contract.c`

**NÃO VALIDADO EM HARDWARE.**
