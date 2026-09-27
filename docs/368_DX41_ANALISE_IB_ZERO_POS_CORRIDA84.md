# DX.41 — análise offline dos TDRs da Corrida 84 e preâmbulo zerado

> 23/09/2026. Fonte física: [Corrida 84](367_CORRIDA84_DX40_RESULTADO.md),
> [`evidence/2026-09-23_13-08-48-DESKTOP-HQAG6T4-lab-binding/`](evidence/2026-09-23_13-08-48-DESKTOP-HQAG6T4-lab-binding/).
> A edição DX.41, o build e as auditorias abaixo são **offline**. Nenhum bind ou
> nova corrida; a DX.41 **NÃO FOI VALIDADA EM HARDWARE**.

## Prova recuperada e limite da coleta

O `FINAL` KMD estava completo, mas a primeira cópia do staging RADV terminou
enquanto PID 12816 ainda submetia. Nove pares `.bin`/`.meta` das submissões
posteriores à fence 98 continuavam em `C:\Windows\Temp\BC250_GFX_IB_DUMP`.
Copiei só esses 18 arquivos para a pasta da Corrida 84 e comparei SHA-256 de
origem/destino. Os fluxos de PID 12816 e 6620 também tinham crescido: a versão
original foi preservada como `.snapshot`, e a versão integral substituiu o `.log`
ativo. [`ib-staging-rescue.json`](evidence/2026-09-23_13-08-48-DESKTOP-HQAG6T4-lab-binding/ib-staging-rescue.json)
guarda nomes, tamanhos e hashes. O índice foi regenerado. Há agora 65 dumps com
`.meta`, 16 fluxos, e a correlação KMD dá **54 únicos, 8 ambíguos, 42 ausentes**.
Isto corrige a contagem inicial 45/8/51; `complete_at_capture=true` se refere
somente à **cauda do KMD**, não à completude do staging RADV.

O indexador passou a agrupar fluxo por `(arquivo, contexto RADV, submit_id)`.
No PID 12816 o `submit_id=1` apareceu em dois contextos, e o agrupamento antigo
os confundia. Passou também a destacar `IB_PREFIXO_ZERO` com o tamanho do
prefixo, `accepted`, contexto e ID entre os primeiros alertas. Os **90 alertas**
do índice revisado não são 90 falhas independentes.

## TDRs 1–3: pista de origem das ondas

Os três têm 4/4 ondas no VMID anterior ao da última submissão e fault de busca
de instrução (`Cid=9`) na página de PC. O `CONTEXT_CONTROL` foi emitido em cada
troca. A [referência Linux `amdgpu_ib_schedule`](https://github.com/torvalds/linux/blob/v6.6/drivers/gpu/drm/amd/amdgpu/amdgpu_ib.c)
avalia `ring->current_ctx != fence_ctx`, faz o flush de VM antes do IB, emite
`CONTEXT_CONTROL` quando há job e depois o IB e a fence. O
[emissor GFX10](https://github.com/torvalds/linux/blob/v6.6/drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c)
carrega os registradores de shader no `CONTEXT_CONTROL` quando marca troca de
contexto. No nosso KMD, a decisão é `LastSubmitVmid != Vmid`; a corrida mediu
**49** trocas com pacote, mas o resultado prova que a sequência atual não basta.
Há **quatro** submissões em que o contexto KMD mudou com VMID igual e o pacote
foi omitido (fences 26, 36, 38, 49); não são os três TDRs medidos, pois neles
também mudou o VMID. Essa divergência é dívida concreta para outra hipótese,
não explicação pronta desta corrida.

No primeiro TDR a correlação ficou mais forte:

| Fence | VMID | Dump único | Shader programado no IB | Estado |
|---:|---:|---|---|---|
| 18 | 1 | PID 1216, 280 DW | `COMPUTE_PGM_LO=0x0201600D` → `0x201600D00` | Fence física 18 observada |
| 19 | 4 | PID 1452, 648 DW | `COMPUTE_PGM_LO=0x0200040C` → **`0x200040C00`**, seguido de `DISPATCH_DIRECT` | Fence 19 em voo; onda VMID **1**, PC **`0x200040C00`** |

O shader do IB imediatamente anterior é diferente. Entre os 65 dumps, o valor
`0x0200040C` ocorre **apenas no dump da fence 19**. No `GFX_WAVE_BOUNDARY` da
fence 19, `SQ_before=0`, `SQ_after=0`, `Fence_before=18` e `Fence_after=18` na
janela CPU; no TDR a onda aparece com PC da 19 e VMID1. Isso torna **forte** a
hipótese de que o dispatch novo executou com estado VMID antigo. Ainda não é
prova absoluta: dump de trabalhos anteriores ausentes e a falta de carimbo
imutável de fence na onda impedem excluir todo reuso possível desse PC. Uma
fence escrita também não informa, por si, a história de cada onda fatal.

No TDR2 o dump único da fence 32 programa `0x200040800`, o PC observado, mas
o dump da fence 31 falta. No TDR3 a fence 47 programa `0x2003C0B00`, o PC
observado, mas há outros IBs do mesmo processo com esse shader e falta a 46.
O bit 23 `VALID` em todos os IBs GFX continua diferente do emissor GFX Linux,
que o omite; como dezenas de IBs com esse bit aposentaram, ainda não há
discriminante causal para removê-lo neste candidato.

## TDR4: bytes malformados antes da submissão

A fence **107** (PID 12816, contexto RADV `0x40002f80`, VMID3, VA
`0x241D88000`, 560 DW) agora tem dump **único** por VA/DW/FNV64. Seus **180
primeiros DW são zero**, e 313/560 DW são não nulos. O primeiro DW `0x00000000`
não é o header PM4 tipo 3 `0xC0012800` que inicia todos os outros IBs aceitos
desse processo. O decodificador estrito de IB GFX recusa o stream no DW0;
o inspetor KMD genérico também consegue percorrer zeros como pacotes tipo 0.
Portanto, o DW0 zerado viola o contrato do preâmbulo observado, mas **não
prova isoladamente** qual comando fez o CP parar. A `.meta` RADV diz
`accepted=1`, e o `KMD_GFX_SUBMIT` da 107 registra o **mesmo FNV64 completo**:
o KMD leu os bytes malformados antes de publicar o WPTR. Não é uma alteração
atribuível à GPU após o submit.

O único outro dump com exatamente **180 DW zero no começo** é o `submit_id=10`
do harness PID 11804, 288 DW, `accepted=0` por `STATUS_DEVICE_REMOVED` após
o terceiro TDR. Os outros **63/65** dumps começam sem prefixo zero. Em ambos,
o conteúdo posterior ao DW179 ainda existe. O padrão é compatível com perda
do CS de preâmbulo usado na concatenação (`pre=1, main=1`), mas a Corrida 84
não registrou o buffer de origem no momento do `memcpy`; não sabemos se o
preâmbulo já veio zerado ou se a cópia/BO de destino perdeu esses bytes.

No mesmo contexto do PID 12816, as submissões 1–26 usaram a VA do mesmo
slot de concatenação (`0x241D88000`) e as amostras KMD anteriores à 107
mostram o header esperado. As submissões 27 e 28 tinham `cs=0` e apenas
sinalizaram fences; a 29 voltou a `pre=1, main=1` e produziu o prefixo zero.
O código do pool só libera slot marcado com fence conhecida e valor observado
maior ou igual ao esperado; se todos estiverem presos, espera a fence mais
antiga. A Corrida 84 **não logou essa decisão de reaproveitamento**, então
o reuso da mesma VA não prova liberação prematura nem exclui escrita
concorrente. O novo log da origem e do destino é o primeiro discriminante.

No quarto TDR, a última submissão era a fence 108/VMID1, mas o registrador
`CP_IB1_BASE=0x241D88000` aponta à VA da **107**, com 24 DW restantes. A
fence física observada era 106; 107 e 108 ainda estavam em voo. O CP estava
ocupado (`PfpParsing=1`) e a amostra `SQ_DEBUG_STS_GLOBAL=0` não mostrou
ondas. Isto liga o stall à vizinhança temporal do IB malformado, sem provar
que seu prefixo zero foi o pacote exato que parou o CP. O `ME_HEADER_DUMP`
mostrava `INDIRECT_BUFFER`; não deve ser rebatizado como falha de shader.

## Mudança DX.41 e medição necessária

No `radv_wddm2_cs.c` externo, antes e depois de copiar o CS de preâmbulo para
o BO de concatenação, `RADV_CONCAT_PREAMBLE` agora registra PID, `submit_id`,
contexto, IP, primeiro DW da origem e destino, tamanho de ambos os prefixos
zero, tamanho copiado e VA do slot. Se o primeiro DW copiado for zero, o fluxo
recebe `layout=REJECT_ZERO_PREAMBLE`, gera dump e `.meta` com `accepted=0`,
emite `RADV_CONCAT_REJECT` e retorna `VK_ERROR_DEVICE_LOST` **antes** de
`D3DKMTSubmitCommand`. Assim uma nova observação separa:

| Leitura futura | Interpretação permitida |
|---|---|
| `source_zero_prefix=180`, `copied_zero_prefix=180` | O CS de preâmbulo já estava zerado na leitura CPU; investigar vida/residência/recuperação desse CS. |
| `source_zero_prefix=0`, `copied_zero_prefix=180` | A origem parecia íntegra, mas o slot copiado não; investigar mapeamento/BO, coerência ou escrita concorrente. |
| Ambos zero | O defeito não ocorreu nessa submissão; não prova correção geral. |

Esse veto protege apenas o caso **medido** (primeiro DW zero). Não valida o
restante do stream nem corrige automaticamente os TDRs 1–3. A causa do prefixo
segue em aberto até o novo log; não se deve repetir a corrida como teste de
desktop sem critério separado para estabilidade/ondas.

## Verificação offline e estado

- `build_radv.bat`: EXIT 0, recompilou `radv_wddm2_cs.c` e vinculou
  `vulkan_radeon.dll` SHA-256 `5C4E770140DB3A0C2DF36A3F7CF11B2448789285CFFF54F2E0E677BCE0891DEE`.
- `audit-radv-wddm-safety.py`: PASS, incluindo portão DX.41 que exige sonda,
  guarda e recusa antes de D3DKMT. `index-bc250-telemetry.py`: compila e
  indexou a evidência completa com 90 alertas, destacando os dois prefixos.
- Patch RADV espelhado em [`external/radv-wddm/bc250-radv-wddm.patch`](../external/radv-wddm/bc250-radv-wddm.patch),
  SHA-256 `B608DCB60F7A201922425256D975C41C6D14701D09BDD85F4C9694CAE8814F03`.
  DLL implantada da Corrida 84: `BF6A1EDA...`. **Build e implantação divergem**;
  não houve cópia do novo binário para `C:\BC250_LIB`.

Na revisão offline, o pool só marcou slot livre após fence conhecida e
observada ou espera explícita, mas a Corrida 84 não registrou a decisão
de reaproveitamento. A coleta tardia do staging foi corrigida no wrapper.
Desktop íntegro, 60/60 e ausência de TDR continuam **NÃO VALIDADOS EM HARDWARE**.

O ensaio controlado foi especificado em [Corrida 85](369_CRITERIO_CORRIDA85_DX41_PREAMBULO.md):
mesmo KMD/UMD da 84, RADV DX.41 fixada por hash e coleta RADV novamente após
o rollback. Foi apenas preparado; o marcador ainda exige boot frio.
