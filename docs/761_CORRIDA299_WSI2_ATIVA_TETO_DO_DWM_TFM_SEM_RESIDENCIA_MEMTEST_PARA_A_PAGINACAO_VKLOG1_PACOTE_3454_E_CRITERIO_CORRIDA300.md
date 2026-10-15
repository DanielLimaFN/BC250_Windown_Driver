# Doc 761 — Corrida 299: WSI.2 ativa e o teto do DWM, TFM sem residência, memtest para a paginação; VKLOG.1, pacote 3454 e critério da Corrida 300

> **Corrida 299:** 14/10/2026 21:49:49, lab **3.453.1.0** (SYS `E04FF5AD`, UMD `B1AAFF12`, RADV `12D48A75`), [doc 760](760_CORRIDA298_WSI2_RECUSADA_NA_IMPORTACAO_DA_FENCE_GRAVITYMARK_SEM_MEMORIA_WSI2B_HEAP1_PACOTE_3453_E_CRITERIO_CORRIDA299.md).
> **Evidência:** `docs/evidence/2026-10-14_21-49-49-DESKTOP-HQAG6T4-lab-binding/` (logs `-FINAL`: vkcube 8292, vkcubepp 2612, TFM 1300 e 12348, dwm 1284).
> **Relato do Daniel:** o vkcube abre, mas ele não sabe se funciona; o GravityMark não abriu; o TFM (Tyler's Frame Machine, Vulkan) também não; tela preta por volta das 21:57 com o memtest_vulkan, que voltou sozinha. Pergunta: precisamos de log para saber se há função não mapeada?

## 1. O que a 299 mediu (P0)

| Item | Medida |
|---|---|
| Bind, 600 s, rollback | Código 0 até o fim; `RECOVERY_OK_ONLINE` sem reboot |
| TDR do GFX / BugCheck | nenhum → GFXRST.2 segue **não exercitada** |
| HEAP.1 | `HEAP1 ... Budget=7625194496` → GTT anunciado **7,6 GB** (era 12 GB); `AvailableForReservation=3,9 GB` |
| WSI.2b | tentativa 0 (a do upstream) = **`0xC000000D` STATUS_INVALID_PARAMETER** em todas; tentativa 1 (sem `Shared`, com acesso da GPU) = OK |
| WSI.2 | **`WSI2: ATIVO`** no vkcube (4 swapchains, com redimensionamento), no vkcubepp e no TFM; `[WSI2] Present #N` com espera e sinal pela GPU `hr=0` até o #1280 |
| vkcube | 1.373 Presents em 87 s, **~16 quadros/s** estáveis (14–20/s por segundo) |
| vkcubepp | 1.711 Presents em 94 s, ~18 quadros/s |
| TFM (pid 1300) | abriu: Vulkan, swapchain 2560×1080 com WSI.2, 2.044 submissões, 1.012 Presents S_OK na UMD, fechou limpo às 21:53:38 |
| TFM no KMD | **1.102 `[Present] REJECT source contract`**, todos com `SlotWord=0x2001C0000` (o back buffer do TFM) e `SlotSeg=0` (não residente) → nada foi para a tela |
| Page fault | 1 só, ~21:53:24: VMID 13 escreveu em `0x2001C0000` sem mapeamento (`Walker=6 Mapping=1 Rw=1`); a GPU seguiu |
| TFM (pid 12348) | 2ª abertura às 21:53:48; o log para 4 s depois, no meio de um Present (H: fechado pelo operador) |
| GravityMark | **nenhum log** (nem da UMD nem da RADV) |
| memtest_vulkan (21:57) | **nenhum log nosso**; o DWM ficou **12 s sem apresentar** (21:56:47–21:56:59): `WaitPaging TIMEOUT (5000ms)` com a fence de paginação parada em 8308 (alvo 8310), `CreateResource RECUSADO hr=0x80070102`, e o DWM se refez quando a paginação voltou |

## 2. O que isso quer dizer

**A. WSI.2 funciona e o vkcube não acelerou: o teto agora é o DWM (P0).** O `pfnPresentCb` de cada quadro leva 30–45 ms dentro do Windows, enquanto o nosso `DxgkDdiPresent` custa 0,1–0,2 ms (`DX74_PRESENT_KMD_TEMPO`). Em janela, o Windows segura o app até o DWM compor os quadros anteriores. Com o vkcube aberto o DWM compunha ~33 quadros/s (parado, 50–60), e o vkcube anda a metade disso. Tirar as duas esperas da CPU era necessário e não foi suficiente: o ganho só aparece com o DWM mais rápido ou com apresentação sem composição (flip direto em tela cheia, D-DirectFlip, aberto).

**B. TFM "não abriu" porque o KMD recusou a imagem (P0 na recusa, H no mecanismo).** O TFM desenhou e apresentou do lado do app, mas em tela cheia o Present passa pelo nosso caminho de Blt, que exige a alocação de origem residente num segmento. O back buffer de 2560×1080 da WSI.2 estava sem segmento (`SlotSeg=0`) em todos os 1.102 Presents. O page fault no mesmo endereço indica que, ao menos uma vez, a página nem estava mapeada quando o blit da RADV escreveu. Mecanismo provável (H): a RADV escreve nesse back buffer pelo endereço virtual importado da UMD, sem o pôr na sua lista de residência, e o VidMm pode despejá-lo entre um Present e outro.

**C. memtest: a paginação para ~10 s com o heap quase cheio (P0 no efeito, H na causa).** O memtest pede quase todo o heap. O VidMm passa segundos despejando e zerando, e a fila de paginação de todos os processos espera. O DWM tem uma espera nossa de 5 s (`WaitPaging TIMEOUT`, fail-closed) e desiste da criação de recurso. Um driver da AMD espera sem prazo. A causa da lentidão da paginação nesse cenário não foi medida.

**D. A pergunta do Daniel — sim, faltava log.** Hoje o log da RADV só vai para arquivo quando a UMD D3D11 é carregada (I.1, redirecionamento do stderr). Apps Vulkan puros (GravityMark antes da janela, memtest) não carregam a UMD, então qualquer erro deles ia só para um console invisível. Por isso não há como dizer por que o GravityMark não abriu. Função "não mapeada" seria vista como `vkGet*ProcAddr` devolvendo NULL, e isso também não era registrado.

## 3. VKLOG.1 (offline)

Na runtime Vulkan da Mesa (`src/vulkan/runtime/vk_log.c`, `vk_instance.c`, `vk_device.c`): log próprio em `C:\Windows\Temp\BC250_UMD_LOG_<pid>-VULKAN.txt` (ou `%TEMP%`). O nome cai na coleta do binder. A escrita é com handle compartilhado e **sem tocar no stderr** (o redirecionamento do stderr derrubou processos AppContainer na Corrida 188).

Registra:
- `vkCreateInstance OK`: app, engine, versão de API, extensões de instância e camadas pedidas;
- cada adaptador DX visto, aceito ou recusado pela RADV, com o VkResult, e o total do `vkEnumeratePhysicalDevices`;
- `vkCreateDevice OK` e as extensões de device pedidas;
- **todo `vk_error`/`vk_errorf`** devolvido ao app: código, arquivo:linha e mensagem (`VK_ERROR_EXTENSION_NOT_PRESENT` com o nome da extensão, `FEATURE_NOT_PRESENT`, NTSTATUS desconhecido etc.);
- **toda função pedida e não exposta** (`vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` devolvendo NULL), uma linha por nome.

Teste offline: `vulkaninfo` com a RADV nova gerou `BC250_UMD_LOG_8440-VULKAN.txt` com a instância, as 9 extensões, 25 funções não expostas (as de display direto, por exemplo) e os 3 adaptadores recusados. Achado colateral: forçada contra o adaptador do BasicDisplay, a RADV cai com divisão por zero (`0xC0000094`) em vez de recusar. Não acontece com o nosso driver instalado (o ICD só existe no INF dele), mas fica registrado.

**Pacote lab 3.454.1.0:** SYS `6E61F032` (mesma fonte), UMD `B1AAFF12` (igual), RADV `C6C9E7F3` no build e em `C:\BC250_LIB` (anterior guardada como `vulkan_radeon.dll.pre-vklog1-12D48A75`), RADV no pacote `296F3F8F`. Auditoria do pacote PASS. **NÃO VALIDADO EM HARDWARE.**

## 4. Critério da Corrida 300 (escrito antes)

**Roteiro:** `tools\EXECUTAR_CORRIDA300_VKLOG1_600S.cmd`, elevado; antes: fusível, desligar e ligar. GravityMark, TFM em janela se der, vkcube; **sem memtest**; desktop até o fim.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **L1** | existe `BC250_UMD_LOG_<pid>-VULKAN.txt` do GravityMark com o último `[VKLOG] ERRO ...` ou `FUNCAO NAO EXPOSTA` antes de ele sair | a causa do GravityMark tem nome e vira a próxima correção |
| **L2** | log do GravityMark só com `vkCreateInstance OK` e nada depois | ele sai fora da RADV (loader, DXGI, outra API); próximo passo é o ETW/WER do processo |
| **L3** | nenhum `-VULKAN.txt` do GravityMark | ele não carregou a RADV; olhar qual API ele escolheu |
| **T1** | TFM em janela com imagem (operador) e sem `REJECT source contract` | o problema do TFM é só o caminho de tela cheia (Blt + residência) |
| **T2** | `REJECT ... SlotSeg=0` de novo | residência do back buffer da WSI.2 confirmada como causa; próxima correção |
| **E** | fora do mapa: descrever antes de julgar | — |

## 5. Próximas correções propostas (não feitas nesta faixa)

1. **WSI.2 com residência:** a RADV recebe o handle da alocação do back buffer e o põe no `MakeResident` e na lista da submissão do blit, ou a UMD o fixa residente enquanto a swapchain viver. Fecha o TFM em tela cheia e o page fault.
2. **Espera de paginação sem prazo** no DWM, como os drivers da AMD (o fail-closed de 5 s derruba a composição sob pressão de memória), com a lentidão da paginação medida no KMD.
3. **Velocidade do DWM** (~33 quadros/s com um app aberto), que é o teto atual de todo app em janela.

## 6. Tabela viva do DCN

Sem mudança. Prontos: D01–D05, D08, D10, D15, D32–D33. Abertos: D06/D07, D11, D12–D14, D18–D23, D25–D29, D31.
