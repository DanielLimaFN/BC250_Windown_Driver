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
#define BC250_NODE1_TRIPOD_REG_ARMED     "Node1TripodArmed"
#define BC250_NODE1_TRIPOD_REG_TOKEN     "Node1TripodToken"
#define BC250_NODE1_TRIPOD_ENV_TOKEN     "BC250_UMD_NODE1_TRIPOD_TOKEN"
#define BC250_NODE1_TRIPOD_HARNESS_EXE   "bc250-umd-node1-tripod.exe"
#define BC250_NODE1_TRIPOD_TOKEN_CHARS   32u

BOOL Bc250UmdD3D11LabPromotionEnabled(void);

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

#ifdef __cplusplus
}
#endif

#endif /* BC250_UMD_CAPS_PROBE_H */
