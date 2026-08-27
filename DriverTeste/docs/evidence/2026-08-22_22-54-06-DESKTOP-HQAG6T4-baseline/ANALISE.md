# Análise do primeiro baseline sem o WDDM legado

Captura: 2026-08-22 22:54:10, 331 segundos depois do boot. A BC-250 estava
vinculada a `BasicDisplay`/`display.inf`; `bc250v4` não existia como serviço e
o coletor registrou `legacy_kmd_absent=true`. O único driver adicional ativo
era o laboratório `bc250dbg`, ABI 2, em `readonly`.

Comparação principal: baseline contaminado de 19:16:29, feito com `bc250v4`,
contra este baseline após remoção offline e novo boot.

## Estados atribuíveis ao driver legado

### PSP

- Antes: ring GPCOM externo em `0x000000044FE01000`, tamanho `0x1000`, WPTR
  `0x80` e `C2PMSG_64=0x80020000`.
- Agora: endereço, tamanho e WPTR zerados; `C2PMSG_64=0x80000000` mantém apenas
  o estado TOS/ACK pronto.

O ring PSP observado anteriormente não é estado de POST desta inicialização;
ele havia sido criado por software na sessão contaminada.

### SDMA0

- Antes: `RB_CNTL=0x00001001`, base `GPU 0xF40FD00000`, WPTR `0xDC`, RPTR
  writeback e WPTR poll em `GPU 0xF40FD02000`, poll habilitado e IB habilitado.
- Agora: ring desabilitado (`RB_CNTL=0x80840000`), base e ponteiros zerados,
  endereços de writeback/poll zerados e `STATUS2_REG=0`.
- `CLK_CTRL` mudou de `0xFF0C0100` para `0x000C0100`.

Isso confirma que o ring em `0xF40FD00000` e sua configuração eram resíduo do
driver legado, não configuração de POST reutilizável. O valor limpo de
`RB_CNTL` não autoriza escrever uma constante de reset: qualquer bring-up deve
usar snapshot, máscaras da versão IP correta, rollback e prova por
RPTR/WPTR/fence/resultado.

## Estados que não mudaram de forma material

- GCVM permaneceu com L2 `0x00080602`, contexto 0 desligado, page table zerada,
  abertura de sistema `GPU 0xF400000000..0xF420000000` e fault status zero.
- `CP_MEC_CNTL=0x50000000`, `CP_STAT=0` e `CP_ME_CNTL=0x15000000` foram iguais
  nas capturas contaminada e limpa. Esses valores não podem ser atribuídos ao
  `bc250v4` pela comparação e não provam firmware, ring ou execução.
- `GRBM_STATUS2` perdeu o bit `0x00200000` (`0x00200008 -> 0x00000008`). A
  diferença é real, mas o significado funcional desse bit ainda precisa ser
  reconciliado com o header/máscara exatos antes de atribuição causal.

## SMU Q0

O preflight encontrou CMD/ARG zerados e RSP `1`. As oito consultas permitidas
responderam status `1`, sem timeout: TestMessage `1`, versão `0x00580600`,
interface `8`, clocks brutos `1500`, ActiveWgp `0`, VID bruto `99` e features
`0xDD602C7D`.

O transporte de consultas SMU Q0 está confirmado também no ambiente limpo.
ActiveWgp zero, clock anunciado e a máscara de features não provam clock físico,
GFXOFF desativado, WGP utilizável ou execução.

## Correção de catálogo

O catálogo dizia que `CP_ME_CNTL` havia sido observado como zero. Ambas as
capturas comparáveis registram `0x15000000`; a documentação consolidada foi
corrigida. O offset `0x04C50` foi incluído como `GFX_RB_AQL_CNTL`, derivado do
header GC10.1 (`mmREG 0x00B4`, base idx 0). `0x04BD0` continua em quarentena:
não há símbolo correspondente em `0x0094` no header usado.

## Gate resultante

O primeiro baseline sem contaminação do WDDM legado foi obtido. O próximo gate
é um ensaio SDMA isolado, controlado e reversível; ele ainda não foi executado.
PSP/firmware, CP, compute, WDDM e qualquer execução GPU permanecem fora deste
marco.

**NÃO VALIDADO EM HARDWARE**
