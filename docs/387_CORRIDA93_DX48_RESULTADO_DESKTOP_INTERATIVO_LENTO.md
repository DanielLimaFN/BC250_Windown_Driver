# Corrida 93 — DX.48: sem travamento, desktop visível e interativo a ~0,6 FPS

> 23/09/2026, bind 21:24:38, janela de 300 s. Candidato lab **3.253.1.0**, SYS
> `48CFDEFD…`, UMD `45BB28C3…`, RADV DX.42 `FCAA11FE…`.
> Evidência: `docs/evidence/2026-09-23_21-24-38-DESKTOP-HQAG6T4-lab-binding/`.
> Critério: [doc 386](386_DX48_CICLO_DE_VIDA_FALANTE_E_CRITERIO_CORRIDA93.md).

## 1. Relato visual do Daniel

- **O que apareceu:** o quadrado azul com o triângulo colorido do harness; depois,
  o desktop.
- **Interação:** o mouse mexia, mas a tela parecia atualizar cerca de 1 quadro
  por segundo.
- **Reinício:** não houve reinício manual.

## 2. Desfecho do critério: **D**

**Controle (P0):**
- `AddDevice ENTER #1` → `LEAVE status=0` → `StartDevice LEAVE status=0x00000000`;
- `CntlPlanned=0x50000B0D`, readback idêntico.

**Assinatura de D, cumprida:**
- sem travamento;
- harness **60/60**, EXIT 0;
- zero `TDR_QUIESCE`.

**Ciclo de vida:**
- existe um único `AddDevice` (#1);
- o único QAI `Type=29` saiu no início, com `hAdapter` = o contexto recém-criado
  e antes do StartDevice (normal);
- **não houve segunda instância**.

**Fim (P0):**
- `RemoveDevice` chegou com o contexto ativo e fez o stop internamente
  (`Reason=2`). É por isso que não há `[DX48_LIFECYCLE] StopDevice`: é o caminho
  previsto no código, não uma lacuna.

**A causa dos travamentos das 88 e 92 continua aberta:** foram 2 travamentos
em 7 corridas desde a 87. A instrumentação DX.48 fica, pronta para o próximo.

## 3. Secundárias

**Preempção (P0):** 2 `PREEMPT_COMMAND` `REGISTRADO` e 2 `PREEMPT_DONE`, com
`LastCompletedFenceId` 820 e 2307.

**Outras métricas (P0):**
- DX.43: 449 warm-ups, zero TDR de VMID antigo;
- zero `CP_RESET_DETECTED`, zero `GFXHUB_FAULT`, zero `DEVICE_REMOVED`;
- zero `TIMEOUT`.

**Composição (P0), inédita:**
- 216 `COMPOSITE_BLT OK`, e **216/216 `COMPOSITE_DATA_PROOF SAMPLED_PASS`**
  (Match=5/5, SourceNonZero=5/5);
- 216 `[Present #]` com Flip=1;
- `SdmaPresent Submitted=649 Completed=649 Fallbacks=0 HostCpuCopies=0`.

Nas corridas anteriores ao DX.3x, a composição movia zeros. Aqui moveu pixels
não nulos, e o Daniel viu o desktop.

**Dirty rects:** 17 `DIRTY_RECTS_RECUSADOS`, ainda abertos.

**Guardião:**
- o primeiro rollback online ficou incompleto (exit 5) e o guardião pediu reboot
  sozinho;
- no boot seguinte fechou com `RECOVERY_OK_ONLINE` e BasicDisplay em Código 0
  (5 amostras);
- ao final da análise, BasicDisplay estava em Código 0 e o serviço bc250mcdm
  estava ausente.

## 4. Por que o desktop está a ~0,6 FPS (mineração do log do dwm.exe, pid 1212)

**Taxa (P0):**
- `DXGI Present LEAVE hr=0`: 149 no total, zero falhas;
- 35 a 38 por minuto, cerca de 0,6 FPS, o que casa com o relato.

**Orçamento do minuto 21:28** (60 s de parede, tempo atribuído à linha que
precede cada intervalo):

| Custo | Medida | Classe |
|---|---|---|
| Depois de `UploadImageData SUCESSO` | **24,0 s** (60 uploads de textura; os 15 de 1307×681 = 3,6 MB custam ~1,1 s cada) | P0 medido |
| Espera de fence do `FlushCommands` | 9,6 s em 425 flushes (p50 20 ms, p90 40 ms, máx 128 ms) | P0 |
| Resto (Map/Upload de VB/IB por draw, D3D10 Flush, trace) | ~26 s | P0 total; divisão não medida |

**O intervalo de ~1,1 s** fica entre o trace `SUCESSO`, que é emitido depois da
fence da cópia e da contagem de não-zeros, e o trace do chamador. No código
(`bc250_umd_resource_vk.c:967-970`), entre os dois só existem
`vkFreeMemory` + `vkDestroyBuffer` do **staging criado e destruído a cada
upload**. Atribuir o custo à liberação do staging é P2: vem da leitura do código
mais o intervalo medido. Por que liberar 3,6 MB leva 1,1 s é **H**: caminho de
paginação/unmap no KMD com walk, prova e log por operação, ou espera de fence de
paginação no winsys do RADV.

**A mesma textura de 1307×681** é reenviada a cada ~4 s com conteúdo idêntico
(`NonZeroSrc=66834/70405` em todas). É o DWM chamando `UpdateSubresourceUP` de
novo, não um defeito nosso.

**Outros custos:**
- **Um submit mais uma espera de fence por draw:** 190 draws/min, e cada ciclo de
  draw leva ~100 ms de parede.
- **Trace síncrono:** cerca de 10 mil linhas/min no dwm.exe, cada uma com
  `CreateFile` + `WRITE_THROUGH` + `FlushFileBuffers` + `Close`, em até dois
  arquivos (`bc250_wddm_winsys.c:64-99`). Custo por linha não medido (H).

## 5. Veredito

- **Critério:** D. A corrida é **sucesso** frente ao critério escrito.
- **Avanço funcional:** é o primeiro desktop visível e interativo relatado desde
  as corridas 86 e 87, quando o desktop ficava travado.
- **Composição:** a composição por GPU chega com dados amostrados não nulos.
- **Desempenho:** ~0,6 FPS. É o próximo bloqueador, e agora tem orçamento
  medido.

## 6. Próximo passo proposto: DX.49, staging persistente no upload de textura

- **Mudança:** uma variável. `Bc250UmdVkUploadImageData` passa a reusar um
  buffer de staging por device, que cresce quando precisa, em vez de criar e
  destruir a cada upload. A fence da cópia continua sendo esperada antes do
  reuso.
- **Medição junto:** trace com µs de `vkAllocateMemory` e `vkFreeMemory`, para
  que a atribuição de §4 deixe de ser P2.
- **Esperado:** some o intervalo de ~1,1 s depois de `SUCESSO`, e os presents
  por minuto sobem. Se os ~24 s/min forem mesmo do staging, isso fica perto
  de 2×.
- **Depois:**
  - agrupar draws até o Present, em vez de um flush por draw;
  - trace com handle aberto e sem `WRITE_THROUGH` por linha.
