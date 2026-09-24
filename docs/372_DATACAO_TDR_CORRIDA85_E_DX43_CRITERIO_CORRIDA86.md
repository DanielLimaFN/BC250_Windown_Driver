# Datação dos TDRs da Corrida 85, DX.43 e critério da Corrida 86

> 23/09/2026. Análise e faixa **offline**, sem bind. Fonte: [Corrida 85](371_CORRIDA85_DX42_RESULTADO.md),
> `docs/evidence/2026-09-23_14-15-11-DESKTOP-HQAG6T4-lab-binding/`.
> Teto: **uma** corrida, depois de desligamento completo.

## 1. Datação: os 4 TDRs são a mesma classe (P0)

**Correção do docs/371.** Lá eu classifiquei os TDRs 2 e 4 como "VMID certo,
falta de mapeamento". O erro foi comparar o VMID das ondas com o **último
submit**, que ainda não tinha começado a executar. O IB que estava executando é
o apontado por `CP_IB1_BASE` no TDR.

| TDR | IB em `CP_IB1` (fence, VMID) | IB anterior (fence, VMID) | Ondas (VMID, PC) | Quem programa esse PC |
|---|---|---|---|---|
| 1 | 29, VMID 2, 824 DW | 28, VMID 1 | VMID **1**, `0x200040C00` | o IB **29** (`COMPUTE_PGM_LO=0x0200040C` no dump); o 28 não tem `COMPUTE_PGM` |
| 2 | 50, VMID 1, 280 DW | 49, VMID 2 | VMID **2**, `0x201800D00` | o IB **50** (`0x0201800D`); o 49 não tem dump |
| 3 | 62, VMID 2, 280 DW | 61, VMID 4 | VMID **4**, `0x2003C0B00` | o IB **62** (`0x02003C0B`); o 61 (3 dumps) não tem `COMPUTE_PGM` |
| 4 | 78, VMID 2, 280 DW | 77, VMID 1 | VMID **1**, `0x201600D00` | o IB **78** (`0x0201600D`); o 77 não tem dump |

- **Em 4 de 4 (P0):** as ondas presas executam o shader programado pelo IB em
  `CP_IB1` e carregam o VMID do **IB imediatamente anterior**.
- Nos dois casos em que o IB anterior tem dump (28 e 61), ele **não programa
  nenhum compute shader**. As ondas não podem ter vindo dele.
- **Conclusão (P0 nos casos com dump):** o dispatch do IB novo nasce com o VMID
  atrasado em um IB.
- **Leitura das faltas:** a falta de permissão (TDRs 1 e 3) ou de mapeamento
  (TDRs 2 e 4) depende só de como a página do shader do processo novo está na
  tabela do processo **anterior**. Não são duas classes.
- **Janela temporal (P0, `GFX_WAVE_BOUNDARY`):** nas trocas que precedem cada
  TDR, `SQ_before=0` e `SQ_after≠0`. As ondas surgem logo depois da publicação
  do WPTR do IB novo, não antes.

**Hipótese (H):** o VMID que o pipe gráfico aplica ao dispatch só é atualizado
numa fronteira de `INDIRECT_BUFFER`. Por isso o primeiro IB depois da troca
roda com o VMID anterior.

- O Linux emite o par `SWITCH_BUFFER` depois do flush de VM, o que pode ser o
  equivalente dessa fronteira.
- No nosso anel, o `SWITCH_BUFFER` e o `SET_BASE(CE_PARTITION)` travaram o ME à
  espera do CE (Corridas 31 e 81; `ME_HAS_ACTIVE_CE_BUFFER_FLAG=1` também nos
  TDRs da 85).
- Portanto, esse caminho exige antes pôr o CE em funcionamento. **Não é esta
  corrida.**

## 2. DX.43 — a variável única

Em `Bc250ActiveGfxEngineSubmit` (`bc250_active_miniport.c`), **só na troca de
VMID**, depois da transição de 34 DW e antes do IB real, sai:

```
INDIRECT_BUFFER(VA = VA do próprio IB, IB_SIZE = 3, VMID = novo)
```

- **O que esse IB executa:** os 3 primeiros DW de todo IB da RADV são um
  `CONTEXT_CONTROL` completo (`C0012800 80000000 80000000`, PKT3 0x28,
  `nvd.h:94`), que a RADV já executa no início de cada IB. O aquecimento cria
  uma fronteira de IB sob o VMID novo sem nenhum comando novo.
- **Guardas:**
  - o KMD **lê** o DW0 do IB pela mesma janela e raiz da inspeção
    (`Bc250ActiveGfxIbReadDword`);
  - o aquecimento só sai se `DW0 == 0xC0012800` e o IB tiver mais de 3 DW;
  - senão pula, com o motivo: `CABECA_ILEGIVEL`, `CABECA_NAO_E_CONTEXT_CONTROL`
    ou `IB_CURTO`.
- **Anel:** reserva de anel própria (+4 DW), só quando o aquecimento sai.
- **Log:** toda troca emite `[GfxEngine] DX43_VMID_WARMUP_IB: Fence VMID a->b
  Emitido Motivo Head VA`, isenta da amostragem e fora do `GfxWaveProbe`.
- **O que não muda:** o IB real, a transição DX.37, a UMD e a RADV DX.42.

**Verificação offline:**
- `build-contract` EXIT=0;
- build lab EXIT=0;
- string `DX43_VMID_WARMUP_IB` conferida no SYS de laboratório e no SYS
  empacotado;
- portão novo `audit_dx43_vmid_warmup_ib` na auditoria de segurança, com
  `tools/test-dx43-vmid-warmup-mutations.py` dando **7 de 7 mutações
  reprovadas** (sem guarda de cabeça, sem reserva, aquecimento depois do IB
  real, cabeça não lida, linha removida, cabeça trocada, linha amostrada fora)
  e os arquivos restaurados;
- auditorias de segurança, matriz DDI, callbacks, RADV e DX.31: PASS.

Não há teste de unidade do anel para este ramo: a lógica vive no miniport. O
portão cobre a ordem e as guardas.

## 3. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.247.1.0`, `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`. SYS `9D28E368346B5B465ED972AED249D806A6B248749175663AF9F91B9198BA3EEB`, INF `BB3D6189…3B81`, CAT `418DDAFA…A006`. Auditoria do pacote PASS. |
| UMD | inalterada, `45BB28C3…1396B9` |
| RADV | DX.42, `FCAA11FE…DD1008C`, a mesma da 85 (já em `C:\BC250_LIB`) |
| Pré-voo | `tools/prepare-corrida86-dx43.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA86_DX43.cmd`, elevado |

O marcador `C:\BC250_REQUIRE_CLEAN_BOOT.json` está armado para o boot de
14:12:45. **Desligamento completo antes da corrida.** Sem `-Force`, sem apagar o
marcador.

## 4. Critério, escrito antes

**Método para cada TDR:**
- ler `TDR_GFX_PACKET IB1 Base` e `RestamDW`;
- achar o último `KMD_GFX_SUBMIT` com essa VA antes do TDR (o IB executando) e
  o anterior;
- ler a linha `DX43_VMID_WARMUP_IB` da troca desse IB;
- comparar o VMID das ondas (`KMD_TDR_WAVE vmid`) com o do IB executando e o
  do anterior;
- conferir no dump quem programa o PC.

`IB1` pode apontar para o aquecimento (mesma VA, 3 DW): isso aparece com
`RestamDW` ≤ 3 e ondas zero.

| | Assinatura | Implicação |
|---|---|---|
| **A: aquecimento resolve** | Todos os sinais: ≥ 30 trocas com `Emitido=1`; **zero** TDR com ondas no VMID do IB anterior; ondas, se houver TDR, no VMID do IB em `CP_IB1` | O VMID do dispatch atualiza numa fronteira de IB. Próximo passo: tratar como correção (manter, medir 60/60), procurar o mecanismo upstream (`SWITCH_BUFFER`/CE) e voltar à frente de render target. Uma corrida só **não prova** eliminação: as 84/85 tiveram 4 TDRs em ~50 trocas. |
| **B: aquecimento não resolve** | ≥ 1 TDR com ondas no VMID do IB anterior **e** a troca do IB em `CP_IB1` com `Emitido=1` | A defasagem não é por pacote de IB. Suspeitos seguintes: latch por EOP/contexto ou buffer CE/DE, que exige inicializar o CE antes de qualquer `SWITCH_BUFFER`. Reverter a DX.43. |
| **C: VMID certo, TDR segue** | TDRs com ondas no VMID do IB em `CP_IB1` e `Emitido=1` na troca | O VMID foi corrigido e resta outra falta (mapeamento real). Ler a PTE da página do PC na tabela do próprio VMID. |
| **D: reversão** | TDRs com `CP_IB1` = VA do IB, `RestamDW` ≤ 3, zero ondas, logo depois de trocas com `Emitido=1`, repetidos; ou ME/PFP parados no `INDIRECT_BUFFER` do aquecimento; ou BugCheck 0x116 | O próprio aquecimento trava o CP. Reverter imediatamente. |
| **E: fora do mapa** | Qualquer outra coisa: `DX43_VMID_WARMUP_IB` ausente, maioria `Emitido=0`, classe nova | Descrever antes de julgar. Maioria de pulos = a variável não foi exercitada (**inconclusivo**). |

- TDR cujo IB em `CP_IB1` teve `Emitido=0` **não conta** para A nem para B:
  registrar com o motivo do pulo.
- Desfecho parcial não é desfecho. Exemplo: zero TDR com menos de 30 trocas
  emitidas não é A.

**Controles** (sem regredir frente à 85):
- restart GFX/SDMA provado a cada TDR;
- sem BugCheck;
- coleta pós-rollback completa;
- veto DX.41 ainda ativo (`RADV_PREAMBLE_IDENTITY` presente).

**Função:** o desfecho funcional sai do harness (60/60), dos pixels, do
device-lost e da observação visual. Qualquer TDR, device-lost ou menos de 60/60
é falha funcional, mesmo com A.
