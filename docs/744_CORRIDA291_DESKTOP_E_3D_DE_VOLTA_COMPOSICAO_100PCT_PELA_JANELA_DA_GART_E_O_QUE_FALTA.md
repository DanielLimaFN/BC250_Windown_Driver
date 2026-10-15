# Doc 744 — Corrida 291: desktop e teste 3D de volta, composição 100% pela janela da GART, e o que falta

> **Data:** 13/10/2026, bind às 23:17:20; watchdog de 300 s; rollback online (`RECOVERY_OK_ONLINE`, rodada 1, sem reboot).
> **Candidato:** lab **3.441.1.0** (SYS `C83F70F6`) = APU.2b + APU.2c + COMP.VA ([doc 743](743_COMPVA_COMPOSICAO_PELO_VA_DO_PROCESSO_PELA_JANELA_DA_GART_PACOTE_3441_E_CRITERIO_CORRIDA291.md)).
> **Evidência:** `docs/evidence/2026-10-13_23-17-20-DESKTOP-HQAG6T4-lab-binding/` (`display-log-this-bind-FINAL.txt`, 423.784 linhas; logs da UMD e de AppContainer).
> **Observado pelo Daniel:** desktop certinho e teste 3D funcionando. Menu e Pesquisa ainda lentos, piscadas rápidas às vezes, falhas na Pesquisa do menu. Arrastar janelas ficou bem mais rápido.

## 1. Medido (P0)

| Sinal | 290 | 291 |
|---|---|---|
| Composição | 0 OK, 160 recusadas | **≥ 6.144 `OK VIA_VA`**, 0 por MC, 0 `VIA_VA FALHOU` |
| `VIA_VA RECUSADO` | — | **5**, todos "janela ocupada por outra composição" |
| Escapes de composição (ENTER) | — | 6.385 |
| Forma da cópia de tela cheia | — | 22 pedaços de 50 linhas, **2.700 páginas de sistema** lidas e mapeadas por quadro |
| Aquecimento da DX.43 | 209 | **8.006 `Emitido=1`**, 0 ilegível |
| TDR / 0x116 / fault | 0 | **0 / 0 / 0**; 24 `PREEMPT_COMMAND` e 24 `PREEMPT_DONE` |
| Flips MPO3 | 0 | **6.219**, 6.205 de 6.214 latches pela IRQ do flip, OTG 60,001 Hz |
| VSYNC perdidos | 12% | 1.056 de 17.361 (6,1%) |
| Latch em 3+ quadros | — | 357 |
| Journal `NO_SPACE` | 1.243 | 186 |
| T3 (processo 3D novo) | não rodou | **21 ciclos com código 0** (187 quadros cada); o 22º foi cortado pelo rollback |
| Monitor (última linha) | — | VRAM dedicada 214,7 MB, compartilhada 131,7 MB, Tctl 74,1 °C |

## 2. Por que ainda está lento e pisca

1. **Toda composição passa pela janela.** A superfície que o DWM compõe é memória da RADV (a UMD D3D11 é traduzida para Vulkan). A APU.2c só manda para a VRAM as classes da UMD, então a RADV continua preferindo o segmento 2. Resultado: em todo quadro de tela cheia a CPU lê 2.700 PTEs da tabela do DWM e o SDMA faz 22 cópias síncronas, ~22 quadros/s. Funciona, mas custa caro. O ganho ao arrastar janelas vem de as cópias pararem de ser recusadas e de a APU.2 ter reduzido a paginação.
2. **Piscadas e falhas da Pesquisa (P0 por correlação 1:1).** As 5 recusas por "janela ocupada" coincidem com os `DXGI Present RECUSADO … E_FAIL` do SearchApp (`0x2427C0000 → 0x2417A0000 800x640` etc.). Duas composições simultâneas, do DWM e da Pesquisa, disputaram a janela única; a segunda foi recusada e aquele quadro se perdeu. Defeito meu: a janela devia **esperar**, não recusar.
3. **`Upload LOCK falhou: hr=0x80070057`.** 227 ocorrências (172 no menu Iniciar), depois de `UploadImageData SUCESSO`. Não é novo: foram 1.107 na 288 e 457 na 290. Efeito na lentidão do menu: H.
4. **6% de VSYNC perdidos e 357 latches em 3+ quadros** são candidatos às piscadas restantes (H).

## 3. Veredito

Pelo critério do doc 743 o desfecho é **A**: ≥ 6.144 composições OK, `VIA_VA FALHOU` = 0, zero TDR, desktop visível pelo operador. A ressalva é que todas as composições foram pela janela; nenhuma usou o caminho por MC. **A COMP.VA está validada em hardware** (P0: engine + operador). A prova de dados por amostragem não existe nesse caminho (`Prova=SOMENTE_ENGINE`); a imagem correta na tela, vista pelo operador, é a evidência visual.

## 4. Próximos passos (ordem)

1. **COMP.VA.2:** a janela passa a esperar (mutex), em vez de recusar. Fecha as 5 perdas, as piscadas e as falhas da Pesquisa ligadas a elas.
2. **COMP.VA.3 (desempenho):** cache das PTEs por origem e GART maior, para a tela inteira caber num pedaço só (o amdgpu usa GART de 512 MB; a nossa tem 2 MB). Isso troca 22 cópias e 2.700 leituras por 1 cópia.
3. **RT na VRAM:** a UMD marca os render targets da RADV que o DWM compõe, e o KMD os põe primeiro na VRAM. A composição volta a usar o caminho por MC, de 1 cópia.
4. **Zero cópia (destino final):** o DWM renderiza direto na primária do flip (MPO3/DirectFlip), sem `COMPOSITE_BLT`.
5. `Upload LOCK falhou` (`E_INVALIDARG`) no menu e na Pesquisa.
6. Journal `NO_SPACE` (186).
7. VSYNC perdidos (6%) e latches lentos.

**NÃO VALIDADO EM HARDWARE:** DCN.4 (formato 28 no Present) e LOG.2 (sem marcas no log).
