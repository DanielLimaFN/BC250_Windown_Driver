#ifndef BC250_UMD_CAPS_PROBE_H
#define BC250_UMD_CAPS_PROBE_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * P7_D3D11_CAPS_PROBE -- logica pura de decisao, num TU proprio.
 *
 * Vive separada de bc250_d3d_umd.c por dois motivos:
 *
 *  1. bc250_d3d_umd.c inclui d3d12umddi.h, que define constantes com ligacao
 *     externa. Incluir esse header em dois TUs do mesmo executavel produz
 *     LNK2005 em massa. Isolando a decisao aqui, o teste offline pode linkar
 *     esta unidade sem arrastar o header do D3D12.
 *  2. A regra de decisao passa a ter UMA fonte, exercitada por teste -- a
 *     licao do A1, onde dois produtores da mesma decisao divergiram por meses.
 *
 * Nada aqui toca hardware, registro ou runtime. E' aritmetica de politica.
 */

#define BC250_D3D11_CAPS_PROBE_REG_KEY  "SOFTWARE\\BC250\\Umd"
#define BC250_D3D11_CAPS_PROBE_REG_NAME "D3D11CapsProbe"
#define BC250_D3D11_LAB_PROMOTION_REG_NAME "D3D11LabPromotion"

/*
 * F2.2 (13/09) -- OPT-OUT DO MODELO FLIP, ARMAVEL SEM REBUILD.
 *
 * `DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT` (dxgiddi.h:218) e' o mecanismo
 * DOCUMENTADO para a UMD dizer ao runtime que apresenta por COPIA, e nao por
 * flip. Isso importa porque este driver NAO PODE fazer flip: o scanout e' o
 * framebuffer fixo herdado do UEFI GOP e escrita em registrador de DCN esta
 * proibida no escopo atual. Aceitar a cadeia de flip calados e' cap sem backend.
 *
 * DESARMADO POR PADRAO, e o motivo e' de metodo, nao de risco: armado, o DXGI
 * muda o caminho inteiro para Blt e nao seria possivel saber se as correcoes do
 * caminho de Flip (DescribeAllocation, SetDisplayMode) resolveram alguma coisa.
 * Uma corrida mede o Flip corrigido; se o E_FAIL persistir, esta chave e' armada
 * pelo registro e a segunda corrida mede o opt-out -- sem recompilar nada.
 */
#define BC250_D3D11_NO_SCANOUT_OPTOUT_REG_NAME "D3D11NoScanoutOptOut"
#define BC250_D3D11_DRAW_BLT_SUBST_REG_NAME "D3D11DrawBltSubstitution"
#define BC250_NODE1_TRIPOD_REG_ARMED     "Node1TripodArmed"
#define BC250_NODE1_TRIPOD_REG_TOKEN     "Node1TripodToken"
#define BC250_NODE1_TRIPOD_ENV_TOKEN     "BC250_UMD_NODE1_TRIPOD_TOKEN"
#define BC250_NODE1_TRIPOD_HARNESS_EXE   "bc250-umd-node1-tripod.exe"
#define BC250_NODE1_TRIPOD_TOKEN_CHARS   32u

/*
 * C3 (12/09) -- tradutor DXBC->SPIR-V, FECHADO POR DEFAULT.
 *
 * Exige DUAS coisas, e as duas sao decisao explicita do dono do projeto:
 *   D3D11ShaderTranslator     (REG_DWORD) tem de existir e valer 1;
 *   D3D11ShaderTranslatorPath (REG_SZ)    caminho ABSOLUTO da DLL.
 *
 * O motivo de nao haver caminho default: a unica implementacao ensaiada e'
 * libvkd3d-shader-1.dll construida com MinGW, que arrasta libgcc_s_seh-1.dll e
 * libwinpthread-1.dll. A bc250umd.dll carrega em TODO processo D3D, incluindo o
 * dwm.exe -- injetar tres DLLs MinGW no compositor do Windows e' decisao de
 * risco, nao efeito colateral de um default. Sem as duas chaves o tradutor nao
 * e' sequer procurado e nenhuma DLL externa e' carregada.
 */
#define BC250_D3D11_SHADER_TRANSLATOR_REG_NAME      "D3D11ShaderTranslator"
#define BC250_D3D11_SHADER_TRANSLATOR_PATH_REG_NAME "D3D11ShaderTranslatorPath"

BOOL Bc250UmdD3D11LabPromotionEnabled(void);
BOOL Bc250UmdDrawBltSubstitutionArmed(void);

/* F2.2: opt-out do modelo Flip. Ver o bloco do define acima. */
BOOL Bc250UmdNoScanoutOptOutArmed(void);

/*
 * F5.2 (14/09) -- Emissao experimental de PM4 para Draw 3D (GFX10).
 *
 * DESARMADO POR PADRAO (fail-closed, default 0).
 *
 * Motivo: pacotes graficos 3D (CB_COLOR0, Scissor, DRAW_INDEX_AUTO) emitidos
 * sem anel grafico dedicado (ME/PFP), sem shaders compilados (DXBC->SPIR-V->ACO)
 * e sem geometria de entrada vinculada (vertex buffer / layout) NAO produzem pixel.
 * Aceitar o Draw silenciosamente viola a Lei 4 (sucesso sem backend).
 * O default de fabrica DEVE recusar honestamente com Bc250UmdRejectUnbackedDraw ("RECUSADO").
 * Este tunable permite habilitar a emissao experimental apenas para ensaios controlados
 * ou testes sem vazar para a producao.
 */
#define BC250_D3D11_EXPERIMENTAL_PM4_DRAW_REG_NAME "D3D11ExperimentalPm4Draw"
#define BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME "BC250_EXPERIMENTAL_PM4_DRAW"

BOOL Bc250UmdExperimentalPm4DrawArmed(void);

/*
 * O driver DECLARA ter pipeline 3D? Fonte unica das duas respostas que
 * dependiam disso: pfnCreateDevice devolver device, e GetCaps Type=130
 * (D3D11DDICAPS_3DPIPELINESUPPORT) publicar niveis.
 *
 *   NativeReady=0, LabPromotion=0 -> FALSE (E_NOTIMPL, default de fabrica)
 *   NativeReady=0, LabPromotion=1 -> TRUE  (promocao de laboratorio)
 *   NativeReady=1, qualquer       -> TRUE  (producao)
 *
 * Extraida em 10/09 porque a decisao vivia inline no CreateDevice lendo o
 * registro, e por isso o teste de contrato offline dependia do estado da
 * MAQUINA: com D3D11LabPromotion=1 deixado armado por um bind anterior, a
 * suite reprovava sem nenhuma mudanca de codigo. Politica pura aqui, leitura
 * de registro no chamador.
 */
BOOL Bc250Umd3DPipelineIsDeclared(BOOL NativeReady, BOOL LabPromotionArmed);

/*
 * C1 (10/09) -- um Draw pode ser SUBSTITUIDO por copia 1:1 de superficie?
 *
 * DEFEITO MEDIDO POR LEITURA DE CODIGO, e da MESMA classe da v1 do probe:
 * Bc250UmdCompositeDraw atendia pfnDraw/pfnDrawIndexed com um memcpy de CPU
 * entre os shadows de origem e destino, marcava handled=TRUE ANTES de tentar
 * qualquer caminho de GPU e devolvia sucesso silencioso. Isso e' fallback de
 * software disfarcado de renderizacao -- proibicao nominal da Lei 4 -- e
 * tornava INFALSIFICAVEL qualquer desktop que aparecesse na tela: nao haveria
 * como distinguir composicao por GPU de memcpy. O teste de contrato ainda
 * EXIGIA esse memcpy, congelando a mentira (mesma armadilha da A15 e do
 * mcdm_callbacks_test).
 *
 * O memcpy foi REMOVIDO. Sobra a substituicao por blt 1:1 no SDMA, que ao
 * menos e' trabalho de GPU -- mas continua sendo SUBSTITUICAO, nao
 * rasterizacao: shader, vertex buffer, index buffer, topologia e blend sao
 * ignorados. Nao sabemos, sem compilador, se o PS ligado e' um passe
 * identidade; afirmar equivalencia seria H apresentada como P0. Por isso:
 *
 *   - default de fabrica DESARMADO (E_NOTIMPL honesto, como antes da C1);
 *   - quando armado, so' e' licito na janela onde a substituicao e' plausivel:
 *     origem e destino residentes, dimensoes compativeis com copia 1:1, mesmo formato
 *     e viewport cobrindo o retangulo de copia (blend e' ignorado pelo SDMA);
 *   - o log declara SUBSTITUIDO, e a contagem e' separada de DrawCount.
 *
 * TRUE  -> a substituicao e' licita nesta configuracao
 * FALSE -> recusar fechado
 */
BOOL Bc250UmdDrawBltSubstitutionIsLawful(
    BOOL SubstitutionArmed,
    BOOL WinsysReady,
    BOOL SourceResident,
    BOOL DestResident,
    BOOL SameDimensions,
    BOOL SameFormat,
    BOOL ViewportCoversFullSurface,
    BOOL BlendEnabled);

/*
 * OpenAdapter deve publicar a tabela de adapter?
 *
 *   NativeReady=0, ProbeArmed=0 -> FALSE  (E_NOTIMPL, default de fabrica)
 *   NativeReady=0, ProbeArmed=1 -> TRUE   (probe: publica caps; device falha fechado)
 *   NativeReady=1, qualquer     -> TRUE   (producao, quando existir pipeline 3D)
 *
 * Publicar caps e' INSTRUMENTACAO. Ter device e' FUNCIONALIDADE. O probe
 * concede a primeira e nunca a segunda.
 */
BOOL Bc250UmdShouldPublishAdapter(BOOL NativeReady, BOOL ProbeArmed);

/* Pure authorization policy for the destructive proof path. All four facts
 * are mandatory; the token is exactly 128 bits encoded as 32 hex chars. */
BOOL Bc250UmdNode1TripodAuthorize(
    BOOL Armed,
    const char* ProcessImageName,
    const char* RegistryToken,
    const char* EnvironmentToken);

/*
 * Versoes de DDI que o probe aceita. Vem da corrida 2026-09-06 23:45.
 *
 * DEFEITO MEDIDO NAQUELA CORRIDA: a v1 do probe aceitava QUALQUER Interface.
 * O runtime pediu 0x000B0011 (minor 17), respondemos S_OK, e a partir dai ele
 * NAO chamou mais nada -- zero GetSupportedVersions, zero GetCaps, zero
 * CreateDevice -- e ficou carregando e descarregando a DLL 66 vezes dentro do
 * dwm.exe. O OpenAdapter10 nunca foi chamado: por termos aceito o degrau mais
 * alto, o runtime NUNCA desceu a escada ate o D3D10_1.
 *
 * Duas licoes, e a segunda e' uma violacao da Lei 4 cometida por mim:
 *
 *   1. o step-down do runtime so' acontece se a UMD RECUSAR o que nao
 *      implementa. Aceitar bloqueia a negociacao.
 *   2. devolver S_OK para um DDI que nao implementamos e' declaracao de
 *      capacidade falsa -- exatamente o que a Lei 4 proibe. O hardware pegou
 *      isso em uma corrida.
 *
 * Por isso o probe passa a ter allowlist: aceita SOMENTE a versao cujo alvo
 * esta dimensionado (D3D10_1, 0x000A0002, tabela de 105 entradas do doc 173) e
 * recusa o resto com E_NOTIMPL, para o runtime poder descer a escada.
 */
#define BC250_UMD_PROBE_IFACE_D3D10_1   0x000A0002u  /* D3D10_1_DDI_INTERFACE_VERSION */
#define BC250_UMD_PROBE_IFACE_D3D11_0_7 0x000B000Bu  /* D3D11_0_7_DDI_INTERFACE_VERSION */
#define BC250_UMD_PROBE_IFACE_MINOR17   0x000B0011u  /* sem nome no WDK 26100 */

/* TRUE somente para versao de DDI que o probe pretende de fato responder. */
BOOL Bc250UmdProbeAcceptsInterface(UINT Interface);

/*
 * v4 (07/09) -- allowlist POR ENTRY POINT, e o motivo e' geometrico.
 *
 * MEDIDO na corrida 07/09 09:40: o handshake UMDRIVERPRIVATE passou a funcionar
 * (112 publicacoes no KMD, 32 validacoes na UMD, zero NOT SUPPORTED) e MESMO
 * ASSIM nenhuma entrada do adapter table foi chamada. Isso REFUTOU a hipotese
 * de que a recusa do Type=0 era a causa do descarte.
 *
 * O que sobrou e' a forma das tabelas:
 *
 *   OpenAdapter10   -> D3D10DDI_ADAPTERFUNCS   = 3 entradas, SEM pfnGetCaps
 *   OpenAdapter10_2 -> D3D10_2DDI_ADAPTERFUNCS = 5 entradas, COM pfnGetCaps
 *                                                 e pfnGetSupportedVersions
 *
 * O runtime so' oferece 0x000A0002 pelo OpenAdapter10 -- ou seja, pela tabela
 * SEM caps. Sem pfnGetCaps ele nao tem por onde perguntar o feature level, e
 * descarta o adapter sem chamar nada. E ele NUNCA pede 0x000A0002 pelo
 * OpenAdapter10_2.
 *
 * Logo o unico caminho para o pfnGetCaps ser alcancado e' aceitar um DDI de
 * D3D11 no OpenAdapter10_2 e ai declarar a verdade em pfnGetSupportedVersions.
 *
 * ISTO NAO E' O ERRO DA v1. Na v1 devolvemos S_OK para 0x000B0011 e nao havia
 * como qualificar a resposta -- era cap falso puro. Aqui aceitamos a ABERTURA
 * em 0x000B000B e imediatamente declaramos, pelo mecanismo que a Microsoft
 * desenhou para isso, que as versoes suportadas sao D3D10_0 e D3D10_1. Abrir
 * nao e' prometer; pfnGetSupportedVersions existe exatamente para essa
 * distincao. E pfnCreateDevice continua falhando fechado.
 *
 * 0x000B0011 (minor 17) segue FORA: nao temos nome nem header contemporaneo
 * para ele, e declarar suporte ao que nao se sabe nomear e' chute.
 */
BOOL Bc250UmdProbeAcceptsInterface10_2(UINT Interface);
BOOL Bc250UmdProbeAcceptsInterface10(UINT Interface);

/*
 * T4.2 (29/09): TRUE somente quando D3D10DDIARG_OPENRESOURCE traz o array
 * D3DDDI_OPENALLOCATIONINFO2 (interface WDDM 2.0, minor 17). Na 0x000B000B
 * negociada o array e' o v1, sem GpuVirtualAddress. Ver bc250_d3d_umd.h.
 */
BOOL Bc250UmdOpenAllocationArrayIsInfo2(UINT Interface);

/*
 * T4.4 (30/09): sequencia de token retomada entre cargas da DLL no mesmo
 * processo, guardada na variavel de ambiente BC250_UMD_TOKEN_SEQUENCE como
 * hexadecimal de 8 digitos. Parse estrito: 1 a 8 digitos hex, nada mais.
 * Format grava sempre 8 digitos e o terminador (Size >= 9).
 */
BOOL Bc250UmdTokenSequenceParse(const char* Text, LONG* Value);
BOOL Bc250UmdTokenSequenceFormat(LONG Value, char* Text, size_t Size);

/*
 * Zerar um caps e' resposta HONESTA para alguns tipos e MENTIRA para outros.
 *
 * D3D11DDICAPS_3DPIPELINESUPPORT carrega um D3D11DDI_3DPIPELINELEVEL, e nesse
 * enum o valor 0 significa "3DPIPELINELEVEL_10_0" -- ou seja, zerar declara um
 * pipeline 3D nivel 10_0 que NAO existe neste driver. Para esse tipo, zerar e'
 * declaracao falsa e o probe precisa devolver falha.
 *
 * Para tipos onde o buffer e' um conjunto de flags de recursos opcionais
 * (THREADING, SHADER, OPTIONS...), tudo zero significa "nada suportado", que e'
 * exatamente a verdade.
 *
 * TRUE  -> zerar e' verdade, responder sucesso
 * FALSE -> zerar seria mentira, responder falha
 */
BOOL Bc250UmdCapsZeroIsTruthful(UINT CapsType);

/* Nome legivel de um D3D10_2DDICAPS_TYPE, para o log da corrida. */
const char* Bc250UmdCapsTypeName(UINT Type);

/*
 * Frente B (doc 547): politica pura do alias VkImage LINEAR sobre a alocacao
 * WDDM do recurso. Devolvem NULL quando o alias e' licito, ou o motivo da
 * recusa. A primeira roda antes de criar a imagem; a segunda confere o layout
 * que a RADV declarou contra a alocacao.
 */
/*
 * ZC.1 (doc 749): render target APRESENTAVEL -- primaria do swapchain do DWM
 * (o MPO3 do DCN mostra a propria alocacao) ou superficie compartilhada que o
 * DWM compoe -- volta a ter alias sobre a propria alocacao WDDM, que mora na
 * VRAM (APU.2c). O Present deixa de copiar imagem -> buffer linear -> alocacao
 * (duas copias de 11 MB e o buffer linear no segmento 2, Corrida 292c). Os
 * outros render targets seguem com memoria propria (Corrida 190). Cada chave
 * desliga a sua metade sozinha se a 190 voltar.
 */
#define BC250_UMD_ZC1_PRIMARY_RT_ALIAS 1
#define BC250_UMD_ZC1_SHARED_RT_ALIAS  1

const char* Bc250UmdFrenteBPreCheck(BOOL AllocCreated, UINT64 GpuVa, UINT64 AllocBytes,
    UINT MipLevels, UINT ArraySize, UINT SampleCount, BOOL DepthStencil, BOOL RenderTarget, BOOL OpenedSurface,
    BOOL PresentableTarget, UINT Width, UINT Height, UINT RowPitch);
const char* Bc250UmdFrenteBLayoutCheck(UINT64 GpuVa, UINT64 AllocBytes, UINT RowPitch,
    UINT64 LayoutOffset, UINT64 LayoutRowPitch, UINT64 ReqSize, UINT64 ReqAlignment);

#ifdef __cplusplus
}
#endif

#endif /* BC250_UMD_CAPS_PROBE_H */
