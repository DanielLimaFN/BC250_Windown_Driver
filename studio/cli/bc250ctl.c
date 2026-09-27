/* bc250ctl.c — CLI do BC-250 Studio.
 *
 * A GUI vem depois; esta ferramenta existe para que o driver seja testavel
 * no minuto seguinte a ele carregar, e para que todo experimento tenha uma
 * forma scriptavel (util em captura de sessao e em relatorio).
 *
 * SPDX-License-Identifier: MIT
 */

#define _CRT_SECURE_NO_WARNINGS 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../lib/bc250lib.h"
#include "audit.h"

static BC250_CTX g_Ctx;

static void Usage(void)
{
    printf(
    "bc250ctl - instrumentacao da AMD BC-250 (bc250dbg.sys)\n"
    "\n"
    "  info                          baseline read-only: driver, PCI e MMIO\n"
    "  regs                          lista os registradores nomeados\n"
    "  reg r <reg|off>               le registrador do BAR5\n"
    "  reg w <reg|off> <valor>       escreve (exige arm write)\n"
    "  smn r <addr>                  le pela janela SMN\n"
    "  smn w <addr> <valor>          escreve pela janela SMN\n"
    "  smu <fila> <msg> [arg]        mensagem do mailbox do SMU\n"
    "  snap <bar|smn> <ini> <n> [f]  dump de registradores (para arquivo)\n"
    "  diff <a> <b>                  compara dois dumps\n"
    "  dumpall <arquivo>             le os 512 KB do BAR5 para arquivo\n"
    "  scan <valor> [mascara]        acha offsets que contem um valor\n"
    "  oracle [assinaturas.txt]      localiza os blocos pelo conteudo lido\n"
    "                                no Linux e decide a convencao de offset\n"
    "  run <script.bcs>              executa um experimento\n"
    "  log [-f] [-t]                 stream do log ring (-f segue, -t trace)\n"
    "  arm <readonly|write|unsafe>   nivel de armamento\n"
    "  thermal                       estado do sensor\n"
    "  thermal calib <src> <num> <den> <offmC> <limC>\n"
    "                                declara a escala do sensor e o limite\n"
    "  telemetry <sample [smu]|watch [n] [ms] [smu_n]>\n"
    "                                GFXOFF fisico e estado SDMA sincronizado\n"
    "  vbios [arquivo.bin]           extrai imagem VBIOS da tabela ACPI VFCT\n"
    "  sdma init [vram|sysram] [off] [dw] arma o ring da SDMA0 (default vram,\n"
    "                                1024 dwords; anel menor exercita wraparound)\n"
    "  sdma write [-nokick|-badfence|-badop] [-ignoresuspect]\n"
    "             [-raw|-rawaddr <va64>] [-verifyat <off>] <off> <dw..>\n"
    "                               -raw poe o endereco cru no pacote, sem somar a\n"
    "                               base de VRAM: e como submeter um VA que so a\n"
    "                               page table resolve. -verifyat diz onde o dado\n"
    "                               deve aparecer. WRITE sem dado conferido nao\n"
    "                               conclui: ponteiro e fence juntos ja mentiram.\n"
    "                                WRITE_LINEAR com fence e dado conferido no kernel.\n"
    "                                As opcoes sao CASOS NEGATIVOS: o resultado\n"
    "                                esperado passa a ser FALHA, nunca sucesso.\n"
    "  sdma <status|rollback|nop [n]|load> estado, restauracao, NOPs e microcodigo\n"
    "  vram <r|w|test|scan|fillpat|checkpat>  acesso e padroes na VRAM\n"
    "  psp <status|init|rollback|loadfw ...>  mailbox GPCOM e lifecycle seguro\n"
    "  sdma <f32|reset>              estado e reinicio do microcontrolador F32\n"
    "  sdma enginereset              SOFT RESET do bloco SDMA0 inteiro, na\n"
    "                                sequencia do upstream. Exige 'arm write' e\n"
    "                                'sdma init' depois: o anel e perdido.\n"
    "  sdma opgate                   bateria da fronteira fail-closed: toda\n"
    "                                entrada invalida tem de ser recusada antes\n"
    "                                de virar pacote, sem mexer no WPTR\n"
    "  sdma fault                    dump SOMENTE LEITURA do estado de falha:\n"
    "                                UTCL1/XNACK, ERROR_LOG, STATUS1..3, FREEZE\n"
    "                                e falha de protecao latchada na L2 do GC\n"
    "  sdma <fill|copy|verify|diag|parsetest|srbmtest>  DMA e diagnosticos\n"
    "  gcvm ptbuild [tab] [alvo] [n] constroi page table na VRAM e confere cada\n"
    "                                PTE. Defaults: tabela 0x0FF10000, alvo\n"
    "                                0x0FD00000, 512 paginas (2 MB cobertos)\n"
    "                                PTE por leitura da CPU. NAO liga traducao.\n"
    "  rlc <status|txn|stop|cgcg|pg|load|start|srm>\n"
    "                               Bring-up do RLC. Medido: EXEC_ROM_CODE e 1\n"
    "                               aqui e 0 no Linux com o CP vivo — o RLC deste\n"
    "                               lado roda o bootloader interno, sem ucode.\n"
    "                               O GATE e esse bit cair para 0.\n"
    "  cp <info|txn|stage|rollback|icinval|icbase|jt|clocks|status>\n"
    "                               Instrumentacao do ramo direct load. A\n"
    "                               recusa PSP historica (0x80000203) veio de\n"
    "                               sequencia incompleta e nao escolhe a rota.\n"
    "                               O Linux saudavel usa LOAD_IP_FW generico.\n"
    "                               NADA aqui tira engine do HALT.\n"
    "  gcvm program <estagio>       PASSO 3: programa valores novos nos\n"
    "                               registradores de traducao, na ordem de\n"
    "                               gfxhub_v2_0.c, com readback conferido em\n"
    "                               cada escrita. Estagios: gart system identity\n"
    "                               invrange (seguros) e tlb cache context0, que\n"
    "                               substituem o bypass do POST e exigem -danger.\n"
    "                               Exige 'gcvm txn snapshot' aberto: sem caminho\n"
    "                               de volta provado o driver recusa escrever.\n"
    "  gcvm invalidate [vmid] [tipo] PASSO 4: invalida a TLB pela engine 17 (a\n"
    "                               da GART) e confirma o ACK. Sem semaforo: o\n"
    "                               GFXHUB nao usa. ACK confirma aceite, nao\n"
    "                               prova traducao.\n"
    "  gcvm semrelease               libera o semaforo da engine de invalidacao\n"
    "                                (ler GCVM_INVALIDATE_ENG0_SEM ADQUIRE; so\n"
    "                                escrever 0 devolve)\n"
    "  gcvm txn <snapshot|restore|verify>  snapshot transacional dos 35\n"
    "                                registradores de traducao. 'restore'\n"
    "                                reescreve os MESMOS valores e confere cada\n"
    "                                readback; 'verify' so compara.\n"
    "  gcvm ptrollback               devolve a regiao da tabela ao conteudo\n"
    "                                original, conferindo cada dword\n"
    "  gcvm <status|dump|init>       MMU do GC. 'dump' e SOMENTE LEITURA e cobre\n"
    "                                aberturas, TLB, L2, contexto 0/1, falha de\n"
    "                                protecao e invalidacao\n"
    "  sdma stress                   BATERIA DE VALIDACAO da SDMA0: tamanhos,\n"
    "                                regioes, wraparound do anel, sequencias\n"
    "  audit                         LEGADO/INVASIVO: escreve scratch e VRAM;\n"
    "                                exige revisao antes de usar em hardware\n"
    "  fw [pasta]                    inspeciona blobs de microcodigo (.bin)\n"
    "\n"
    "Nivel de armamento: o driver carrega em readonly. Escrita exige 'write'.\n"
    "Mensagens de SMU capazes de mexer em clock/tensao exigem 'unsafe', que\n"
    "por sua vez exige sensor termico calibrado. Isso e deliberado: ja houve\n"
    "trava de maquina com ForceGfxFreq sem tabelas DPM.\n");
}

static const char *ArmName(uint32_t lvl)
{
    switch (lvl) {
    case BC250DBG_ARM_READONLY: return "readonly";
    case BC250DBG_ARM_WRITE:    return "write";
    case BC250DBG_ARM_UNSAFE:   return "unsafe";
    default:                    return "?";
    }
}

static int CmdInfo(void)
{
    BC250DBG_INFO in;
    BC250DBG_THERMAL th;
    int rc = Bc250Info(&g_Ctx, &in);
    int i;

    if (rc) {
        printf("GET_INFO falhou: %d\n", rc);
        return 1;
    }

    printf("driver          : bc250dbg v%u.%u  ABI %u  armamento=%s\n",
           (in.DriverVersion >> 16) & 0xFFFF, (in.DriverVersion >> 8) & 0xFF,
           in.AbiVersion, ArmName(in.ArmLevel));

    if (in.PciFound) {
        printf("pci             : %02X:%02X.%u  %04X:%04X rev %02X\n",
               in.PciBus, in.PciDev, in.PciFn, in.VendorId, in.DeviceId,
               in.RevisionId);
    } else {
        printf("pci             : NAO ENCONTRADO (MMIO desativado; sem fallback)\n");
    }

    for (i = 0; i < BC250DBG_MAX_BARS; i++) {
        if (in.Bar[i].Base || in.Bar[i].Raw) {
            printf("  BAR%d          : 0x%016llX  raw 0x%08X%s%s\n", i,
                   (unsigned long long)in.Bar[i].Base, in.Bar[i].Raw,
                   (in.Bar[i].Flags & 0x1) ? " io" : "",
                   (in.Bar[i].Flags & 0x4) ? " [mapeado]" : "");
        }
    }

    printf("mmio            : 0x%llX +%u KB %s\n",
           (unsigned long long)in.MmioPhysical, in.MmioLength / 1024,
           in.MmioMapped ? "" : "(NAO MAPEADO)");

    if (in.MmioMapped) {
        printf("  GPU_ID        : 0x%08X %s\n", in.GpuId,
               in.GpuId == 0x9FFF9700 ? "(confere com a BC-250)" :
               in.GpuId == 0xFFFFFFFF ? "<== leitura invalida: BAR nao responde" : "");

        /* Offsets SOC15 corrigidos em 2026-08-19. A coluna "legado" repete as
         * leituras dos offsets antigos, que estavam errados — serve so para
         * comparar com os registros historicos do projeto.
         * CC_GC_SHADER_ARRAY_CONFIG e o desempate: no Linux le 0xFFF80000. */
        printf("\n  offset          %-12s %-12s\n", "SOC15", "legado");
        printf("  SCRATCH_REG0  : 0x%08X   0x%08X\n",
               in.ScratchReg0, in.ScratchReg0B);
        printf("  GRBM_STATUS   : 0x%08X   0x%08X\n",
               in.GrbmStatus, in.GrbmStatusB);
        printf("  CC_GC_ARRAY   : 0x%08X   0x%08X   <== o desempate\n",
               in.CcShaderArray, in.CcShaderArrayB);

        if (in.CcShaderArray == 0xFFF80000u) {
            printf("\n  ** MAPA OBSERVADO: CC_GC_SHADER_ARRAY_CONFIG em 0x089BC bate com o\n"
                   "     valor bruto visto no Linux (0xFFF80000). Isto sustenta o offset\n"
                   "     SOC15, mas nao prova quantidade de CU utilizavel nem execucao.\n");
        } else {
            printf("\n  ** 0x089BC nao traz 0xFFF80000. Antes de ajustar qualquer offset,\n"
                   "     ache o bloco GC pelo conteudo, sem hipotese de endereco:\n"
                   "       bc250ctl oracle oracle\\linux-signatures.txt\n");
        }
        printf("\n");
    }

    printf("smu             : nao consultado pelo baseline; use 'smu' explicitamente\n");

    if (Bc250ThermalGet(&g_Ctx, &th) == 0) {
        printf("termico         : %s  limite %u C  %s\n",
               th.Trusted ? "calibrado" : "NAO CALIBRADO",
               th.LimitC, th.Tripped ? "*** TRIP ***" : "");
        printf("  sondas cruas  : edge 0x%08X  junc 0x%08X  mem 0x%08X\n",
               th.RawEdge, th.RawJunction, th.RawMem);
        if (th.Trusted) {
            printf("  temperatura   : %.2f C\n", th.MilliC / 1000.0);
        } else {
            printf("  ** sem calibracao o driver recusa armar 'unsafe'.\n"
                   "     Compare as sondas com o Linux e rode 'thermal calib'.\n");
        }
    }

    printf("log ring        : %u entradas, head %lld\n",
           in.LogEntryCount, (long long)in.LogHead);
    return 0;
}

static int CmdRegs(void)
{
    int i;
    printf("%-24s %-4s %-10s %s\n", "NOME", "ESP", "OFFSET", "NOTA");
    for (i = 0; i < g_Bc250RegCount; i++) {
        printf("%-24s %-4s 0x%08X %s\n",
               g_Bc250Regs[i].Name,
               g_Bc250Regs[i].Space == BC250DBG_SPACE_SMN ? "smn" : "bar",
               g_Bc250Regs[i].Offset,
               g_Bc250Regs[i].Note ? g_Bc250Regs[i].Note : "");
    }
    return 0;
}

static int CmdReg(int argc, char **argv, int smn)
{
    uint32_t space, off, value = 0;

    if (argc < 4) {
        Usage();
        return 1;
    }
    if (!Bc250ResolveReg(argv[3], &space, &off)) {
        printf("registrador desconhecido: %s\n", argv[3]);
        return 1;
    }
    if (smn) {
        space = BC250DBG_SPACE_SMN;
    }

    if (_stricmp(argv[2], "r") == 0) {
        int rc = (space == BC250DBG_SPACE_SMN)
               ? Bc250SmnRead(&g_Ctx, off, &value)
               : Bc250RegRead(&g_Ctx, off, &value);
        if (rc) {
            printf("leitura falhou: erro %d\n", rc);
            return 1;
        }
        printf("%s 0x%08X = 0x%08X\n",
               space == BC250DBG_SPACE_SMN ? "smn" : "bar", off, value);
        return 0;
    }

    if (_stricmp(argv[2], "w") == 0) {
        uint32_t val, readback = 0;
        int rc;

        if (argc < 5) {
            Usage();
            return 1;
        }
        val = (uint32_t)strtoul(argv[4], NULL, 0);
        rc = (space == BC250DBG_SPACE_SMN)
           ? Bc250SmnWrite(&g_Ctx, off, val, &readback)
           : Bc250RegWrite(&g_Ctx, off, val, &readback);
        if (rc == ERROR_ACCESS_DENIED) {
            printf("NEGADO: driver em readonly. Rode: bc250ctl arm write\n");
            return 1;
        }
        if (rc) {
            printf("escrita falhou: erro %d\n", rc);
            return 1;
        }
        printf("%s 0x%08X <- 0x%08X   readback 0x%08X   %s\n",
               space == BC250DBG_SPACE_SMN ? "smn" : "bar", off, val, readback,
               readback == val ? "guardou" : "*** NAO GUARDOU ***");
        return 0;
    }

    Usage();
    return 1;
}

static int CmdSmu(int argc, char **argv)
{
    uint32_t q, msg, arg = 0, status = 0, result = 0;
    int rc;

    if (argc < 4) {
        Usage();
        return 1;
    }
    q = (uint32_t)strtoul(argv[2], NULL, 0);
    msg = (uint32_t)strtoul(argv[3], NULL, 0);
    if (argc > 4) {
        arg = (uint32_t)strtoul(argv[4], NULL, 0);
    }

    rc = Bc250SmuMsg(&g_Ctx, q, msg, arg, 0, &status, &result);
    if (rc == ERROR_ACCESS_DENIED) {
        printf("NEGADO: mensagem classificada como de risco.\n"
               "Precisa de 'arm unsafe' (que exige sensor calibrado).\n");
        return 1;
    }
    if (rc) {
        printf("falhou: erro %d (status SMU 0x%02X)\n", rc, status);
        return 1;
    }
    printf("q%u msg 0x%02X arg 0x%X -> status 0x%02X (%s) resultado 0x%08X (%u)\n",
           q, msg, arg, status,
           status == 0x01 ? "OK" :
           status == 0xFF ? "FAILED" :
           status == 0xFE ? "UNKNOWN" :
           status == 0xFD ? "REJECTED" :
           status == 0xFC ? "BUSY" : "?",
           result, result);
    return 0;
}

static int CmdSnap(int argc, char **argv)
{
    uint32_t space, start, count, *buf;
    const char *path = (argc > 5) ? argv[5] : NULL;
    int rc;

    if (argc < 5) {
        Usage();
        return 1;
    }
    space = (_stricmp(argv[2], "smn") == 0) ? BC250DBG_SPACE_SMN
                                            : BC250DBG_SPACE_BAR;
    start = (uint32_t)strtoul(argv[3], NULL, 0);
    count = (uint32_t)strtoul(argv[4], NULL, 0);

    buf = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!buf) {
        return 1;
    }

    rc = Bc250Snapshot(&g_Ctx, space, start, count, buf);
    if (rc) {
        printf("snapshot falhou: erro %d\n", rc);
        free(buf);
        return 1;
    }

    if (path) {
        FILE *f = NULL;
        uint32_t i;
        if (fopen_s(&f, path, "w") != 0 || !f) {
            printf("nao consegui escrever %s\n", path);
            free(buf);
            return 1;
        }
        fprintf(f, "# bc250 snapshot space=%s start=0x%X count=%u\n",
                space == BC250DBG_SPACE_SMN ? "smn" : "bar", start, count);
        for (i = 0; i < count; i++) {
            fprintf(f, "0x%08X 0x%08X\n", start + i * 4, buf[i]);
        }
        fclose(f);
        printf("gravado: %s (%u dwords)\n", path, count);
    } else {
        uint32_t i;
        for (i = 0; i < count; i++) {
            const char *nm = Bc250RegName(space, start + i * 4);
            printf("0x%08X 0x%08X %s\n", start + i * 4, buf[i], nm ? nm : "");
        }
    }

    free(buf);
    return 0;
}

typedef struct { uint32_t off, val; } SnapLine;

static SnapLine *LoadSnap(const char *path, int *count)
{
    FILE *f = NULL;
    char line[256];
    SnapLine *out = NULL;
    int cap = 0, n = 0;

    if (fopen_s(&f, path, "r") != 0 || !f) {
        return NULL;
    }
    while (fgets(line, sizeof(line), f)) {
        unsigned int off, val;
        if (line[0] == '#') {
            continue;
        }
        if (sscanf(line, "%x %x", &off, &val) != 2) {
            continue;
        }
        if (n == cap) {
            cap = cap ? cap * 2 : 256;
            out = (SnapLine *)realloc(out, cap * sizeof(SnapLine));
            if (!out) {
                fclose(f);
                return NULL;
            }
        }
        out[n].off = off;
        out[n].val = val;
        n++;
    }
    fclose(f);
    *count = n;
    return out;
}

static int CmdDiff(int argc, char **argv)
{
    SnapLine *a, *b;
    int na = 0, nb = 0, i, j, diffs = 0;

    if (argc < 4) {
        Usage();
        return 1;
    }
    a = LoadSnap(argv[2], &na);
    b = LoadSnap(argv[3], &nb);
    if (!a || !b) {
        printf("nao consegui ler os dumps\n");
        free(a);
        free(b);
        return 1;
    }

    for (i = 0; i < na; i++) {
        for (j = 0; j < nb; j++) {
            if (a[i].off == b[j].off) {
                if (a[i].val != b[j].val) {
                    const char *nm = Bc250RegName(BC250DBG_SPACE_BAR, a[i].off);
                    printf("0x%08X  0x%08X -> 0x%08X  %s\n",
                           a[i].off, a[i].val, b[j].val, nm ? nm : "");
                    diffs++;
                }
                break;
            }
        }
    }

    printf("%d diferenca(s) em %d registradores\n", diffs, na);
    free(a);
    free(b);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Varredura do BAR5 por conteudo                                      */
/*                                                                     */
/* Serve para achar um bloco de registradores SEM depender de nenhuma  */
/* convencao de offset: procura no BAR5 os valores que o umr leu no    */
/* Linux e reporta onde eles estao. Ver docs/ORACULO-LINUX-*.md.       */
/* ------------------------------------------------------------------ */

#define BAR5_DWORDS (0x80000u / 4u)

/* AVISO: le TODO o BAR5. Registrador com efeito colateral de leitura
 * (read-to-clear, pop de FIFO) sera tocado. Nao rode isto com carga real
 * na placa. */
static uint32_t *DumpBar5(BC250_CTX *ctx)
{
    uint32_t *buf = (uint32_t *)malloc(BAR5_DWORDS * sizeof(uint32_t));
    uint32_t off = 0;

    if (!buf) {
        return NULL;
    }
    printf("lendo 512 KB do BAR5");
    while (off < BAR5_DWORDS) {
        uint32_t chunk = BAR5_DWORDS - off;
        if (chunk > BC250DBG_SNAP_MAX) {
            chunk = BC250DBG_SNAP_MAX;
        }
        if (Bc250Snapshot(ctx, BC250DBG_SPACE_BAR, off * 4, chunk, buf + off) != 0) {
            printf("\nfalha lendo em 0x%05X\n", off * 4);
            free(buf);
            return NULL;
        }
        off += chunk;
        putchar('.');
        fflush(stdout);
    }
    printf(" ok\n\n");
    return buf;
}

static int CmdDumpAll(int argc, char **argv)
{
    uint32_t *buf;
    FILE *f = NULL;
    uint32_t i;

    if (argc < 3) {
        printf("uso: dumpall <arquivo>\n");
        return 1;
    }
    buf = DumpBar5(&g_Ctx);
    if (!buf) {
        return 1;
    }
    if (fopen_s(&f, argv[2], "w") != 0 || !f) {
        printf("nao consegui escrever %s\n", argv[2]);
        free(buf);
        return 1;
    }
    fprintf(f, "# bc250 dump completo do BAR5 (512 KB)\n");
    for (i = 0; i < BAR5_DWORDS; i++) {
        fprintf(f, "0x%08X 0x%08X\n", i * 4, buf[i]);
    }
    fclose(f);
    printf("gravado: %s (%u dwords)\n", argv[2], BAR5_DWORDS);
    free(buf);
    return 0;
}

static int CmdScan(int argc, char **argv)
{
    uint32_t *buf, value, mask = 0xFFFFFFFFu;
    uint32_t i, hits = 0;

    if (argc < 3) {
        printf("uso: scan <valor> [mascara]\n");
        return 1;
    }
    value = (uint32_t)strtoul(argv[2], NULL, 0);
    if (argc > 3) {
        mask = (uint32_t)strtoul(argv[3], NULL, 0);
    }

    buf = DumpBar5(&g_Ctx);
    if (!buf) {
        return 1;
    }
    for (i = 0; i < BAR5_DWORDS; i++) {
        if ((buf[i] & mask) == (value & mask)) {
            const char *nm = Bc250RegName(BC250DBG_SPACE_BAR, i * 4);
            printf("0x%05X = 0x%08X  (dword 0x%05X) %s\n",
                   i * 4, buf[i], i, nm ? nm : "");
            if (++hits >= 64) {
                printf("... (parando em 64 ocorrencias)\n");
                break;
            }
        }
    }
    printf("\n%u ocorrencia(s)\n", hits);
    free(buf);
    return 0;
}

/* Le o arquivo de assinaturas do Linux e localiza cada uma no BAR5.
 * Formato por linha:  NOME  DWORD_OFFSET|-  VALOR  [MASCARA] */
static int CmdOracle(int argc, char **argv)
{
    const char *path = (argc > 2) ? argv[2] : "oracle\\linux-signatures.txt";
    FILE *f = NULL;
    char line[256];
    uint32_t *buf;
    int votosA = 0, votosB = 0, linhas = 0;

    if (fopen_s(&f, path, "r") != 0 || !f) {
        printf("nao consegui abrir %s\n", path);
        return 1;
    }
    buf = DumpBar5(&g_Ctx);
    if (!buf) {
        fclose(f);
        return 1;
    }

    printf("%-30s %-10s %s\n", "ASSINATURA", "VALOR", "ONDE APARECE NO BAR5");
    printf("--------------------------------------------------------------"
           "----------------\n");

    while (fgets(line, sizeof(line), f)) {
        char nome[64], sdw[32], sval[32], smask[32];
        uint32_t dw = 0xFFFFFFFFu, value, mask = 0xFFFFFFFFu;
        uint32_t i, hits = 0;
        int nf;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        smask[0] = '\0';
        nf = sscanf(line, "%63s %31s %31s %31s", nome, sdw, sval, smask);
        if (nf < 3) {
            continue;
        }
        if (sdw[0] != '-') {
            dw = (uint32_t)strtoul(sdw, NULL, 0);
        }
        value = (uint32_t)strtoul(sval, NULL, 0);
        if (nf >= 4 && smask[0]) {
            mask = (uint32_t)strtoul(smask, NULL, 0);
        }
        linhas++;

        printf("%-30s 0x%08X ", nome, value);

        for (i = 0; i < BAR5_DWORDS && hits < 6; i++) {
            if ((buf[i] & mask) != (value & mask)) {
                continue;
            }
            hits++;
            if (dw != 0xFFFFFFFFu) {
                /* base implicita em cada convencao:
                 *   A: byte  = base + reg      -> base = off - reg
                 *   B: byte  = (base + reg)*4  -> base = off/4 - reg      */
                uint32_t baseA = (i * 4) - dw;
                uint32_t baseB = i - dw;
                const char *veredito = "";

                if (baseB == 0x1260) { veredito = "  <== GC base B!"; votosB++; }
                else if (baseA == 0x1260) { veredito = "  <== GC base A!"; votosA++; }

                printf("%s0x%05X (baseA=0x%X baseB=0x%X)%s",
                       hits > 1 ? "\n                                          " : "",
                       i * 4, baseA, baseB, veredito);
            } else {
                printf("%s0x%05X", hits > 1 ? ", " : "", i * 4);
            }
        }
        if (hits == 0) {
            printf("NAO ENCONTRADO");
        }
        printf("\n");
    }

    fclose(f);
    free(buf);

    printf("\n--------------------------------------------------------------"
           "----------------\n");
    printf("%d assinatura(s) testada(s).  votos convencao A: %d   convencao B: %d\n",
           linhas, votosA, votosB);
    if (votosB > votosA) {
        printf("\nVEREDITO: convencao B — byte = (base_ip + reg_dword) * 4.\n"
               "Use os nomes B_* do mapa; os A_* estao errados por fator 4.\n");
    } else if (votosA > votosB) {
        printf("\nVEREDITO: convencao A — byte = base_ip + reg_dword.\n"
               "Inesperado: contraria o amdgpu. Confira as assinaturas.\n");
    } else {
        printf("\nSem veredito. Recolha as assinaturas de novo no Linux com a\n"
               "placa no MESMO estado (ociosa) e tente valores mais distintivos.\n");
    }
    return 0;
}

static int CmdRun(int argc, char **argv)
{
    BC250_SCRIPT s;
    BC250DBG_SCRIPT_OUT out;
    BC250DBG_OP_RESULT *res = NULL;
    char err[256];
    uint32_t i;
    int rc;

    if (argc < 3) {
        Usage();
        return 1;
    }
    if (!Bc250ScriptLoadFile(argv[2], &s, err, sizeof(err))) {
        printf("erro de montagem: %s\n", err);
        return 1;
    }

    printf("script '%s': %u ops\n", s.Name, s.OpCount);

    rc = Bc250ScriptRun(&g_Ctx, &s, &out, &res);
    if (rc) {
        printf("execucao falhou: erro %d\n", rc);
        Bc250ScriptFree(&s);
        return 1;
    }

    printf("\n%-5s %-6s %-10s %-14s %s\n",
           "LINHA", "OP", "STATUS", "VALOR", "DETALHE");
    for (i = 0; i < out.OpCount && res; i++) {
        const BC250DBG_OP *op = &s.Ops[i];
        char detail[96] = "";

        if (op->Op == BC250DBG_OP_REG_R || op->Op == BC250DBG_OP_REG_W ||
            op->Op == BC250DBG_OP_EXPECT_REG || op->Op == BC250DBG_OP_POLL_REG) {
            const char *nm = Bc250RegName(BC250DBG_SPACE_BAR, (uint32_t)op->A);
            _snprintf_s(detail, sizeof(detail), _TRUNCATE, "%s0x%05X",
                        nm ? nm : "", (uint32_t)op->A);
        } else if (op->Op == BC250DBG_OP_SMU) {
            _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                        "q%u msg 0x%02X -> smu status 0x%02X",
                        (uint32_t)op->A, (uint32_t)op->B,
                        (uint32_t)(res[i].Value >> 32));
        }

        printf("%-5u %-6s %-10s 0x%012llX %s\n",
               s.Lines ? s.Lines[i] : 0,
               Bc250OpName(op->Op),
               Bc250OpStatusName(res[i].Status),
               (unsigned long long)res[i].Value,
               detail);
    }

    printf("\nresultado: %u ops, %u falha(s), %llu us, status final %s\n",
           out.OpCount, out.Failures, (unsigned long long)out.ElapsedUs,
           Bc250OpStatusName(out.Status));

    free(res);
    Bc250ScriptFree(&s);
    return out.Failures ? 2 : 0;
}

static int CmdLog(int argc, char **argv)
{
    BC250DBG_LOG_ENTRY batch[512];
    char line[320];
    int follow = 0, trace = 0, i, n;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) follow = 1;
        if (strcmp(argv[i], "-t") == 0) trace = 1;
    }

    if (Bc250LogAttach(&g_Ctx) != 0) {
        printf("nao consegui mapear o log ring\n");
        return 1;
    }

    Bc250LogSetMask(&g_Ctx,
                    trace ? BC250DBG_LVL_ALL
                          : (BC250DBG_LVL_ERROR | BC250DBG_LVL_WARN |
                             BC250DBG_LVL_INFO),
                    BC250DBG_SUB_ALL);

    if (follow) {
        Bc250LogSeekEnd(&g_Ctx);
    }

    do {
        n = Bc250LogDrain(&g_Ctx, batch, (int)(sizeof(batch) / sizeof(batch[0])));
        for (i = 0; i < n; i++) {
            Bc250LogFormat(&g_Ctx, &batch[i], line, sizeof(line));
            puts(line);
        }
        if (follow && n == 0) {
            Sleep(50);
        }
    } while (follow);

    if (g_Ctx.LogDropped) {
        printf("[%lld entradas perdidas — consumidor ficou para tras]\n",
               (long long)g_Ctx.LogDropped);
    }
    return 0;
}

static int CmdArm(int argc, char **argv)
{
    uint32_t level;
    int rc;

    if (argc < 3) {
        Usage();
        return 1;
    }
    if (_stricmp(argv[2], "readonly") == 0)      level = BC250DBG_ARM_READONLY;
    else if (_stricmp(argv[2], "write") == 0)    level = BC250DBG_ARM_WRITE;
    else if (_stricmp(argv[2], "unsafe") == 0)   level = BC250DBG_ARM_UNSAFE;
    else { Usage(); return 1; }

    rc = Bc250Arm(&g_Ctx, level);
    if (rc) {
        printf("arm falhou: erro %d\n", rc);
        if (level == BC250DBG_ARM_UNSAFE) {
            printf("  'unsafe' exige sensor termico calibrado — rode\n"
                   "  'bc250ctl thermal calib ...' antes.\n");
        }
        return 1;
    }
    printf("armamento agora: %s\n", ArmName(level));
    return 0;
}

static int CmdThermal(int argc, char **argv)
{
    BC250DBG_THERMAL th;

    if (argc > 2 && _stricmp(argv[2], "calib") == 0) {
        BC250DBG_THERMAL_SET set;
        int rc;

        if (argc < 8) {
            printf("uso: thermal calib <src 0=edge 1=junc 2=mem> <num> <den> "
                   "<offset_mC> <limite_C>\n"
                   "exemplo (sonda em C inteiros, limite 85):\n"
                   "  bc250ctl thermal calib 1 1000 1 0 85\n");
            return 1;
        }
        memset(&set, 0, sizeof(set));
        set.Source       = (uint32_t)strtoul(argv[3], NULL, 0);
        set.ScaleNum     = (uint32_t)strtoul(argv[4], NULL, 0);
        set.ScaleDen     = (uint32_t)strtoul(argv[5], NULL, 0);
        set.OffsetMilliC = (int32_t)strtol(argv[6], NULL, 0);
        set.LimitC       = (uint32_t)strtoul(argv[7], NULL, 0);
        set.Trusted      = 1;

        rc = Bc250ThermalSet(&g_Ctx, &set);
        if (rc) {
            printf("calibracao recusada: erro %d\n", rc);
            return 1;
        }
        printf("calibracao aceita.\n");
    }

    if (Bc250ThermalGet(&g_Ctx, &th) != 0) {
        printf("leitura termica falhou\n");
        return 1;
    }

    printf("sondas: edge 0x%08X  junc 0x%08X  mem 0x%08X  fan %u rpm / pwm %u\n",
           th.RawEdge, th.RawJunction, th.RawMem, th.FanRpm, th.FanPwm);
    printf("escala: %u/%u offset %d mC  fonte %s\n",
           th.ScaleNum, th.ScaleDen, th.OffsetMilliC,
           th.Source == 0 ? "edge" : th.Source == 2 ? "mem" : "junction");
    printf("estado: %s  %.2f C  limite %u C%s\n",
           th.Trusted ? "calibrado" : "NAO CALIBRADO",
           th.MilliC / 1000.0, th.LimitC,
           th.Tripped ? "  *** TRIP ATIVO ***" : "");
    return 0;
}

static int CmdVbios(int argc, char **argv)
{
    const char *saveFile = (argc >= 3) ? argv[2] : NULL;
    DWORD tableSig = 0x54434656; /* 'VFCT' */
    DWORD tableProv = 0x41435049; /* 'ACPI' */
    DWORD cb = GetSystemFirmwareTable(tableProv, tableSig, NULL, 0);
    uint8_t *buf;
    uint32_t i;
    int foundVbios = 0;

    if (cb == 0) {
        printf("GetSystemFirmwareTable('ACPI', 'VFCT') retornou 0 (tabela VFCT nao encontrada).\n");
        return 1;
    }

    buf = (uint8_t *)malloc(cb);
    if (!buf) {
        printf("sem memoria para %u bytes da VFCT\n", cb);
        return 1;
    }

    if (GetSystemFirmwareTable(tableProv, tableSig, buf, cb) != cb) {
        printf("falha ao ler %u bytes da tabela ACPI VFCT\n", cb);
        free(buf);
        return 1;
    }

    printf("Tabela ACPI VFCT encontrada: %u bytes\n", cb);
    if (cb >= 36) {
        char oemId[7] = {0};
        char oemTable[9] = {0};
        memcpy(oemId, buf + 10, 6);
        memcpy(oemTable, buf + 16, 8);
        printf("  OEM ID        : %s\n", oemId);
        printf("  OEM Table ID  : %s\n", oemTable);
        printf("  Revisao       : %u\n", buf[8]);
    }

    for (i = 0; i + 64 < cb; i++) {
        if (buf[i] == 0x55 && buf[i + 1] == 0xAA) {
            uint32_t romSize = (uint32_t)buf[i + 2] * 512;
            uint32_t j;
            foundVbios = 1;

            printf("\nImagem VBIOS encontrada no offset 0x%X:\n", i);
            printf("  Tamanho ROM   : %u bytes (%u KB)\n", romSize, romSize / 1024);

            for (j = i; j < i + romSize && j + 16 < cb; j++) {
                if (memcmp(buf + j, "ATOMBIOS", 8) == 0 || memcmp(buf + j, "ATOM", 4) == 0) {
                    char str[128] = {0};
                    size_t k;
                    for (k = 0; k < sizeof(str) - 1 && j + k < cb && buf[j + k] >= 32 && buf[j + k] <= 126; k++) {
                        str[k] = (char)buf[j + k];
                    }
                    if (strlen(str) >= 4) {
                        printf("  Assinatura    : %s\n", str);
                        break;
                    }
                }
            }

            for (j = i; j < i + romSize && j + 32 < cb; j++) {
                if (buf[j] == '1' && buf[j+1] == '1' && buf[j+2] == '3' && buf[j+3] == '-') {
                    char str[64] = {0};
                    size_t k;
                    for (k = 0; k < sizeof(str) - 1 && j + k < cb && buf[j + k] >= 32 && buf[j + k] <= 126; k++) {
                        str[k] = (char)buf[j + k];
                    }
                    printf("  ATOM Part No  : %s\n", str);
                    break;
                }
            }

            if (saveFile) {
                FILE *f = fopen(saveFile, "wb");
                if (f) {
                    uint32_t writeLen = romSize;
                    if (i + writeLen > cb) writeLen = cb - i;
                    fwrite(buf + i, 1, writeLen, f);
                    fclose(f);
                    printf("  Salvo em      : %s (%u bytes)\n", saveFile, writeLen);
                } else {
                    printf("  Erro ao salvar em: %s\n", saveFile);
                }
            }

            if (romSize > 0 && i + romSize <= cb) {
                i += romSize - 1;
            }
        }
    }

    if (!foundVbios) {
        printf("Nenhuma assinatura de VBIOS padrao (55AA) encontrada dentro da tabela VFCT.\n");
    }

    free(buf);
    return 0;
}

static uint32_t PspFwTypeFromName(const char *s)
{
    if (_stricmp(s, "sdma0") == 0) return BC250_PSP_FW_SDMA0;
    if (_stricmp(s, "sdma1") == 0) return BC250_PSP_FW_SDMA1;
    if (_stricmp(s, "me")    == 0) return BC250_PSP_FW_CP_ME;
    if (_stricmp(s, "pfp")   == 0) return BC250_PSP_FW_CP_PFP;
    if (_stricmp(s, "ce")    == 0) return BC250_PSP_FW_CP_CE;
    if (_stricmp(s, "mec")   == 0) return BC250_PSP_FW_CP_MEC;
    if (_stricmp(s, "mec_jt1") == 0 || _stricmp(s, "mec2") == 0)
        return BC250_PSP_FW_CP_MEC_JT1;
    if (_stricmp(s, "rlc")   == 0) return BC250_PSP_FW_RLC_G;
    if (_stricmp(s, "smu")   == 0) return BC250_PSP_FW_SMU;
    return (uint32_t)strtoul(s, NULL, 0);
}

static int BcpLoadIpFwSilent(uint32_t fwType, const char *path, uint32_t *outStatus, uint32_t *outAddrLo)
{
    FILE *f;
    uint8_t *buf;
    long sz;
    uint32_t *hdr, ucodeSz, ucodeOff, dwordCount;
    BC250DBG_PSP_LOAD_FW_IO out;
    int rc;

    if (outStatus) *outStatus = 0xFFFFFFFFu;
    if (outAddrLo) *outAddrLo = 0;

    f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0x20) { fclose(f); return -2; }
    buf = (uint8_t *)malloc(sz);
    if (!buf) { fclose(f); return -3; }
    if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return -4; }
    fclose(f);

    hdr = (uint32_t *)buf;
    ucodeSz  = hdr[5];
    ucodeOff = hdr[6];
    if (ucodeSz == 0 || ucodeOff + ucodeSz > (uint32_t)sz) { free(buf); return -5; }
    dwordCount = ucodeSz / 4;

    memset(&out, 0, sizeof(out));
    rc = Bc250PspLoadIpFw(&g_Ctx, fwType, (const uint32_t *)(buf + ucodeOff), dwordCount, &out);
    free(buf);

    if (outStatus) *outStatus = out.RespStatus;
    if (outAddrLo) *outAddrLo = out.RespFwAddrLo;
    return (rc == 0 && out.FenceReached && out.RespStatus == 0) ? 0 : 1;
}

static int CmdPsp(int argc, char **argv)
{
    if (argc < 3) {
        printf("uso: bc250ctl psp <status | init | rollback | loadfw <tipo> [arquivo.bin]>\n"
               "\n"
               "  tipo: sdma0 sdma1 me pfp ce mec mec2 rlc smu (ou numero)\n"
               "  'init' cria o anel GPCOM; 'loadfw' exige 'init' antes.\n");
        return 1;
    }

    if (_stricmp(argv[2], "status") == 0) {
        BC250DBG_PSP_STATUS_IO st;
        int rc = Bc250PspStatus(&g_Ctx, &st);
        if (rc) { printf("falha ao ler status do PSP: erro %d\n", rc); return 1; }

        printf("Estado do PSP (MP0 em 0x%05X):\n", BC250_MP0_BASE);
        printf("  C2PMSG_64 : 0x%08X   %s\n", st.C2pmsg64,
               (st.C2pmsg64 & BC250_PSP_TOS_READY) ? "<== TOS PRONTO (bit31)"
                                                   : "<== TOS nao pronto");
        printf("  C2PMSG_81 : 0x%08X   status/heartbeat do SOS (valor variavel)\n", st.C2pmsg81);
        printf("  C2PMSG_67 : 0x%08X   WPTR do anel\n", st.C2pmsg67);
        printf("  anel      : lo 0x%08X  hi 0x%08X  tam 0x%08X\n",
               st.RingLo, st.RingHi, st.RingSizeReg);
        printf("  criado por nos: %s", st.RingCreated ? "sim" : "nao");
        if (st.RingCreated) printf("  (PA 0x%llX)", (unsigned long long)st.RingPhysical);
        printf("\n");
        return 0;
    }

    if (_stricmp(argv[2], "init") == 0) {
        BC250DBG_PSP_RING_INIT_IO out;
        int rc = Bc250PspRingInit(&g_Ctx, &out);

        printf("C2PMSG_64 antes : 0x%08X\n", out.C2pmsg64Before);
        printf("C2PMSG_64 depois: 0x%08X\n", out.C2pmsg64After);
        if (rc) {
            printf("falha ao criar o anel do PSP: erro %d\n", rc);
            if ((out.C2pmsg64Before & BC250_PSP_TOS_READY) == 0) {
                printf("  ** O TOS nao esta pronto. Sem isso o PSP nao atende nada.\n");
            } else {
                printf("  ** TOS estava pronto mas o anel nao foi confirmado.\n"
                       "     Rode 'bc250ctl arm write' se ainda nao rodou.\n");
            }
            return 1;
        }
        printf("Anel GPCOM do PSP criado: PA 0x%llX (%u bytes)\n",
               (unsigned long long)out.RingPhysical, BC250_PSP_RING_SIZE);
        return 0;
    }

    if (_stricmp(argv[2], "rollback") == 0 ||
        _stricmp(argv[2], "free") == 0) {
        BC250DBG_PSP_ROLLBACK_IO out;
        int rc;

        memset(&out, 0, sizeof(out));
        rc = Bc250PspRollback(&g_Ctx, &out);
        printf("Rollback PSP GPCOM:\n");
        printf("  snapshot presente : %s\n", out.SnapshotPresent ? "sim" : "nao");
        printf("  destroy ACK       : %s\n", out.DestroyAck ? "sim" : "nao");
        printf("  registradores     : %u restaurados, %u divergencias\n",
               out.RegistersRestored, out.RegisterMismatches);
        if (rc || out.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHA (erro %d, status %u)\n", rc, out.Status);
            return 1;
        }
        printf("  RESULTADO          : ring destruido antes de liberar buffers\n");
        return 0;
    }

    if (_stricmp(argv[2], "tmr") == 0) {
        printf("PSP SETUP_TMR em QUARENTENA.\n"
               "  Nao existe reserva dinamica exclusiva que comprove PA, MC/GPUVA e tamanho.\n"
               "  Nenhum IOCTL foi enviado e nenhuma escrita em hardware foi solicitada.\n");
        return 1;
    }

    if (_stricmp(argv[2], "loadfw") == 0 && argc >= 4) {
        uint32_t fwType = PspFwTypeFromName(argv[3]);
        const char *path = (argc >= 5) ? argv[4] : NULL;
        char defPath[MAX_PATH];
        FILE *f;
        uint8_t *buf;
        long sz;
        uint32_t *hdr, ucodeSz, ucodeOff, dwordCount, version;
        BC250DBG_PSP_LOAD_FW_IO out;
        int rc;

        if (fwType >= BC250_PSP_FW_CP_ME && fwType <= BC250_PSP_FW_CP_MEC_JT1) {
            printf("PSP LOAD_IP_FW para CP em QUARENTENA.\n"
                   "  A sequencia atual nao possui TMR reservada nem endereco MC comprovado.\n"
                   "  Nenhum arquivo foi aberto, nenhum IOCTL foi enviado e nenhuma escrita foi solicitada.\n");
            return 1;
        }

        if (path == NULL) {
            if (fwType == BC250_PSP_FW_SDMA0 || fwType == BC250_PSP_FW_SDMA1) {
                _snprintf_s(defPath, sizeof(defPath), _TRUNCATE,
                            "firmware/cyan_skillfish2_sdma%s.bin",
                            (fwType == BC250_PSP_FW_SDMA1) ? "1" : "");
            } else if (fwType == BC250_PSP_FW_CP_ME) {
                _snprintf_s(defPath, sizeof(defPath), _TRUNCATE, "firmware/cyan_skillfish2_me.bin");
            } else if (fwType == BC250_PSP_FW_CP_PFP) {
                _snprintf_s(defPath, sizeof(defPath), _TRUNCATE, "firmware/cyan_skillfish2_pfp.bin");
            } else if (fwType == BC250_PSP_FW_CP_CE) {
                _snprintf_s(defPath, sizeof(defPath), _TRUNCATE, "firmware/cyan_skillfish2_ce.bin");
            } else if (fwType == BC250_PSP_FW_CP_MEC) {
                _snprintf_s(defPath, sizeof(defPath), _TRUNCATE, "firmware/cyan_skillfish2_mec.bin");
            } else if (fwType == BC250_PSP_FW_CP_MEC_JT1) {
                _snprintf_s(defPath, sizeof(defPath), _TRUNCATE, "firmware/cyan_skillfish2_mec2.bin");
            } else if (fwType == BC250_PSP_FW_RLC_G) {
                _snprintf_s(defPath, sizeof(defPath), _TRUNCATE, "firmware/cyan_skillfish2_rlc.bin");
            } else {
                printf("informe o arquivo de firmware\n");
                return 1;
            }
            path = defPath;
        }

        f = fopen(path, "rb");
        if (!f) { printf("nao consegui abrir '%s'\n", path); return 1; }
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz < 0x20) { printf("arquivo pequeno demais: '%s'\n", path); fclose(f); return 1; }
        buf = (uint8_t *)malloc(sz);
        if (!buf) { fclose(f); return 1; }
        if (fread(buf, 1, sz, f) != (size_t)sz) {
            printf("leitura incompleta de '%s'\n", path);
            free(buf); fclose(f); return 1;
        }
        fclose(f);

        /* common_firmware_header: +0x10 ucode_version, +0x14 ucode_size_bytes,
         * +0x18 ucode_array_offset_bytes.
         *
         * Mandamos SO o payload, sem o header. O amdgpu faz o mesmo:
         * amdgpu_ucode_init_single_fw copia a partir de
         * ucode_array_offset_bytes com tamanho ucode_size_bytes. Mandar o
         * arquivo inteiro desloca tudo em ucode_array_offset_bytes. */
        hdr = (uint32_t *)buf;
        version  = hdr[4];
        ucodeSz  = hdr[5];
        ucodeOff = hdr[6];

        if (ucodeSz == 0 || ucodeOff + ucodeSz > (uint32_t)sz) {
            printf("formato de firmware invalido em '%s' "
                   "(offset 0x%X + tamanho 0x%X > arquivo 0x%lX)\n",
                   path, ucodeOff, ucodeSz, sz);
            free(buf);
            return 1;
        }
        dwordCount = ucodeSz / 4;

        printf("Carregando '%s' pelo PSP:\n", path);
        printf("  tipo         : %u\n", fwType);
        printf("  versao ucode : 0x%X\n", version);
        printf("  payload      : %u dwords (%u bytes) a partir do offset 0x%X\n",
               dwordCount, ucodeSz, ucodeOff);

        memset(&out, 0, sizeof(out));
        rc = Bc250PspLoadIpFw(&g_Ctx, fwType,
                              (const uint32_t *)(buf + ucodeOff), dwordCount, &out);
        free(buf);

        printf("  fence        : %s\n", out.FenceReached ? "alcancado" : "TIMEOUT");
        if (!out.FenceReached) {
            printf("  ** O PSP nao consumiu o frame. Rode 'psp init' e confira\n"
                   "     'psp status' (o anel precisa estar criado).\n");
            return 1;
        }
        printf("  RespStatus   : 0x%08X %s\n", out.RespStatus,
               (out.RespStatus == 0) ? "<== SUCESSO" :
               (out.RespStatus == BC250_PSP_ERR_UNKNOWN_CMD)
                   ? "<== comando desconhecido para este SOS" : "<== erro");
        printf("  RespFwAddr   : 0x%08X%08X   TmrSize 0x%08X\n",
               out.RespFwAddrHi, out.RespFwAddrLo, out.RespTmrSize);

        if (out.RespStatus == 0 &&
            (fwType == BC250_PSP_FW_SDMA0 || fwType == BC250_PSP_FW_SDMA1)) {
            printf("\n  Confira se o microcontrolador acordou:\n"
                   "    bc250ctl sdma f32\n");
        }
        return (out.RespStatus == 0 && rc == 0) ? 0 : 1;
    }

    printf("subcomando psp desconhecido: %s\n", argv[2]);
    return 1;
}

static int CmdVram(int argc, char **argv)
{
    if (argc < 3) {
        printf("uso: bc250ctl vram r <offset>\n"
               "     bc250ctl vram w <offset> <valor>\n"
               "     bc250ctl vram test [offset]\n"
               "\n"
               "Offsets sao dentro da abertura do BAR0 (256 MB). O endereco de\n"
               "GPU correspondente e 0x%llX + offset.\n",
               (unsigned long long)BC250_VRAM_GPU_BASE);
        return 1;
    }

    if (_stricmp(argv[2], "r") == 0 && argc >= 4) {
        uint32_t off = (uint32_t)strtoul(argv[3], NULL, 0);
        uint32_t v = 0;
        int rc = Bc250VramRead(&g_Ctx, off, &v);
        if (rc) { printf("falha ao ler VRAM: erro %d\n", rc); return 1; }
        printf("vram 0x%08X = 0x%08X   (GPU 0x%llX)\n", off, v,
               (unsigned long long)(BC250_VRAM_GPU_BASE + off));
        return 0;
    }

    if (_stricmp(argv[2], "w") == 0 && argc >= 5) {
        uint32_t off = (uint32_t)strtoul(argv[3], NULL, 0);
        uint32_t val = (uint32_t)strtoul(argv[4], NULL, 0);
        uint32_t rb = 0;
        int rc = Bc250VramWrite(&g_Ctx, off, val, &rb);
        if (rc) { printf("falha ao escrever na VRAM: erro %d (rode 'arm write')\n", rc); return 1; }
        printf("vram 0x%08X <- 0x%08X   readback 0x%08X   %s\n", off, val, rb,
               (rb == val) ? "guardou" : "*** NAO GUARDOU ***");
        return 0;
    }

    if ((_stricmp(argv[2], "fillpat") == 0 || _stricmp(argv[2], "checkpat") == 0) && argc >= 5) {
        /* Preenchimento/verificacao com padrao derivado do endereco, feitos no
         * kernel. Expostos no CLI para poder reproduzir passo a passo o que a
         * bateria faz — sem isso nao da para bissectar uma falha dela. */
        int verificar = (_stricmp(argv[2], "checkpat") == 0);
        BC250DBG_VRAM_PATTERN_IO io;
        uint32_t off = (uint32_t)strtoul(argv[3], NULL, 0);
        uint32_t len = (uint32_t)strtoul(argv[4], NULL, 0);
        uint32_t sem = (argc >= 6) ? (uint32_t)strtoul(argv[5], NULL, 0) : 0x5A000000u;
        uint32_t feito = 0;
        long ruins = 0;
        int rc;

        while (feito < len) {
            uint32_t pedaco = len - feito;
            if (pedaco > BC250DBG_VRAM_OP_MAX_BYTES) pedaco = BC250DBG_VRAM_OP_MAX_BYTES;
            memset(&io, 0, sizeof(io));
            io.Pattern = BC250DBG_PAT_ADDR;
            io.Value   = sem;
            io.Start   = off + feito;
            io.Length  = pedaco;
            rc = verificar ? Bc250VramVerifyPattern(&g_Ctx, &io)
                           : Bc250VramFillPattern(&g_Ctx, &io);
            if (rc) { printf("falha no IOCTL: erro %d\n", rc); return 1; }
            ruins += (long)io.BadCount;
            if (verificar && io.BadCount) {
                uint32_t k, n = io.BadCount;
                if (n > BC250DBG_VRAM_BAD_SAMPLES) n = BC250DBG_VRAM_BAD_SAMPLES;
                for (k = 0; k < n; k++) {
                    printf("  0x%08X: lido 0x%08X, esperado 0x%08X\n",
                           io.BadOffset[k], io.BadGot[k], io.BadWant[k]);
                }
            }
            feito += pedaco;
        }

        if (verificar) {
            printf("%s: %ld de %u dwords divergiram (semente 0x%08X)\n",
                   ruins ? "DIVERGIU" : "ok", ruins, len / 4, sem);
            return ruins ? 1 : 0;
        }
        printf("preenchido 0x%08X +%u bytes com padrao de endereco (semente 0x%08X)\n",
               off, len, sem);
        return 0;
    }

    if (_stricmp(argv[2], "scan") == 0 && argc >= 4) {
        /* Varredura completa da janela, fatiada: o driver limita cada chamada
         * a 32 MB para nao segurar o IOCTL por minutos. */
        uint32_t alvo = (uint32_t)strtoul(argv[3], NULL, 0);
        uint32_t mascara = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 0u;
        uint32_t off, total = 0;

        printf("Procurando 0x%08X nos 256 MB da janela do BAR0...\n", alvo);
        for (off = 0; off < 0x10000000u; off += 0x02000000u) {
            BC250DBG_VRAM_SCAN_IO scan;
            memset(&scan, 0, sizeof(scan));
            scan.Value  = alvo;
            scan.Mask   = mascara;
            scan.Start  = off;
            scan.Length = 0x02000000u;
            if (Bc250VramScan(&g_Ctx, &scan)) {
                printf("  falha na varredura em 0x%08X\n", off);
                return 1;
            }
            if (scan.HitCount) {
                uint32_t k, n = scan.HitCount;
                if (n > BC250DBG_VRAM_SCAN_MAX_HITS) n = BC250DBG_VRAM_SCAN_MAX_HITS;
                for (k = 0; k < n; k++) printf("  0x%08X\n", scan.Hits[k]);
                total += scan.HitCount;
            }
            printf("  ... %u MB varridos\r", (off + 0x02000000u) >> 20);
            fflush(stdout);
        }
        printf("\n%u ocorrencia(s).\n", total);
        return 0;
    }

    if (_stricmp(argv[2], "test") == 0) {
        uint32_t off = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0)
                                   : BC250_SDMA_RING_VRAM_DEFAULT;
        static const uint32_t pats[] = { 0x00000000u, 0xFFFFFFFFu, 0xCAFEBABEu,
                                         0x12345678u, 0xA5A5A5A5u };
        uint32_t saved = 0;
        int ok = 1, i;

        if (Bc250VramRead(&g_Ctx, off, &saved)) {
            printf("falha ao ler VRAM em 0x%08X\n", off);
            return 1;
        }
        printf("Teste de padroes na VRAM em 0x%08X (GPU 0x%llX):\n", off,
               (unsigned long long)(BC250_VRAM_GPU_BASE + off));
        printf("  valor original : 0x%08X\n", saved);

        for (i = 0; i < (int)(sizeof(pats) / sizeof(pats[0])); i++) {
            uint32_t rb = 0;
            if (Bc250VramWrite(&g_Ctx, off, pats[i], &rb)) {
                printf("  falha na escrita (rode 'bc250ctl arm write')\n");
                return 1;
            }
            printf("  0x%08X -> readback 0x%08X   %s\n", pats[i], rb,
                   (rb == pats[i]) ? "ok" : "DIVERGIU");
            if (rb != pats[i]) ok = 0;
        }

        Bc250VramWrite(&g_Ctx, off, saved, NULL);
        printf("  valor restaurado.\n");
        printf("\n  %s\n", ok
               ? "** A abertura do BAR0 e memoria real e gravavel."
               : "** Algum padrao divergiu: a abertura nao se comporta como RAM.");
        return ok ? 0 : 1;
    }

    printf("subcomando vram desconhecido: %s\n", argv[2]);
    return 1;
}

static const char *GfxoffStateName(uint32_t state)
{
    switch (state) {
    case 0: return "GFXOFF";
    case 1: return "SAINDO";
    case 2: return "ATIVO";
    case 3: return "ENTRANDO";
    default: return "INVALIDO";
    }
}

static int CmdTelemetry(int argc, char **argv)
{
    if (argc < 3 || _stricmp(argv[2], "sample") == 0) {
        BC250DBG_TELEMETRY_IO io;
        uint32_t flags = (argc >= 4 && _stricmp(argv[3], "smu") == 0)
                       ? BC250DBG_TELEM_INCLUDE_SMU : 0;
        int rc = Bc250Telemetry(&g_Ctx, flags, &io);
        if (rc) { printf("falha na telemetria: erro %d\n", rc); return 1; }

        printf("Amostra fisica BC-250 (QPC %llu / %llu Hz):\n",
               (unsigned long long)io.QpcTicks,
               (unsigned long long)io.QpcFrequency);
        printf("  SMUIO_GFX_MISC_CNTL : 0x%08X\n", io.GfxMiscCntl);
        if (io.ValidMask & BC250DBG_TELEM_GFXOFF_VALID)
            printf("  GFXOFF fisico       : %u [%s]\n",
                   io.GfxoffState, GfxoffStateName(io.GfxoffState));
        else
            printf("  GFXOFF fisico       : INDISPONIVEL\n");
        printf("  GRBM_STATUS         : 0x%08X\n", io.GrbmStatus);
        printf("  SDMA STATUS/RB_CNTL : 0x%08X / 0x%08X\n",
               io.SdmaStatusReg, io.SdmaRbCntl);
        printf("  SDMA RPTR/WPTR      : 0x%08X / 0x%08X\n",
               io.SdmaRptr, io.SdmaWptr);
        printf("  SDMA WB/shadow      : 0x%08X / 0x%08X\n",
               io.SdmaRptrWriteback, io.SdmaWptrShadow);
        printf("  F32 CNTL/PC         : 0x%08X / 0x%04X\n",
               io.SdmaF32Cntl, io.SdmaF32Pc);
        printf("  SDMA CLK_CTRL       : 0x%08X\n", io.SdmaClkCtrl);
        printf("  termico             : trusted=%u milliC=%d raw=%08X/%08X/%08X\n",
               io.ThermalTrusted, io.ThermalMilliC,
               io.RawEdge, io.RawJunction, io.RawMem);
        if (flags) {
            printf("  SMU gfxclk          : status=0x%02X valor=%u MHz %s\n",
                   io.SmuGfxclkStatus, io.SmuGfxclkMhz,
                   (io.ValidMask & BC250DBG_TELEM_SMU_GFXCLK_VALID) ? "[VALIDO]" : "[INVALIDO]");
            printf("  SMU active WGP      : status=0x%02X valor=%u %s\n",
                   io.SmuActiveWgpStatus, io.SmuActiveWgp,
                   (io.ValidMask & BC250DBG_TELEM_SMU_WGP_VALID) ? "[VALIDO]" : "[INVALIDO]");
            printf("  SMU features        : status=0x%02X valor=0x%08X %s\n",
                   io.SmuFeaturesStatus, io.SmuFeatures,
                   (io.ValidMask & BC250DBG_TELEM_SMU_FEATURE_VALID) ? "[VALIDO]" : "[INVALIDO]");
        }
        printf("  potencia            : INDISPONIVEL (nenhuma fonte calibrada)\n");
        return 0;
    }

    if (_stricmp(argv[2], "watch") == 0) {
        uint32_t count = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 1000u;
        uint32_t intervalMs = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 1u;
        uint32_t smuEvery = (argc >= 6) ? (uint32_t)strtoul(argv[5], NULL, 0) : 0u;
        uint32_t i;

        if (count == 0 || count > 100000 || intervalMs > 1000 || smuEvery > count) {
            printf("parametros invalidos: n=1..100000, ms=0..1000, smu_n=0..n\n");
            return 1;
        }
        printf("sample,qpc,qpc_hz,int100ns,valid,gfx_misc,gfxoff,grbm,sdma_status,rb_cntl,rptr,wptr,rptr_wb,wptr_shadow,f32_cntl,f32_pc,clk_ctrl,temp_trusted,temp_mc,raw_edge,raw_junction,raw_mem,smu_gfx_mhz,smu_wgp,smu_features\n");
        for (i = 0; i < count; i++) {
            BC250DBG_TELEMETRY_IO io;
            uint32_t flags = (smuEvery != 0 && (i % smuEvery) == 0)
                           ? BC250DBG_TELEM_INCLUDE_SMU : 0;
            int rc = Bc250Telemetry(&g_Ctx, flags, &io);
            if (rc) {
                fprintf(stderr, "telemetry sample %u falhou: erro %d\n", i, rc);
                return 1;
            }
            printf("%u,%llu,%llu,%llu,0x%08X,0x%08X,%u,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%04X,0x%08X,%u,%d,0x%08X,0x%08X,0x%08X,%u,%u,0x%08X\n",
                   i, (unsigned long long)io.QpcTicks,
                   (unsigned long long)io.QpcFrequency,
                   (unsigned long long)io.InterruptTime100ns,
                   io.ValidMask, io.GfxMiscCntl, io.GfxoffState,
                   io.GrbmStatus, io.SdmaStatusReg, io.SdmaRbCntl,
                   io.SdmaRptr, io.SdmaWptr, io.SdmaRptrWriteback,
                   io.SdmaWptrShadow, io.SdmaF32Cntl, io.SdmaF32Pc,
                   io.SdmaClkCtrl, io.ThermalTrusted, io.ThermalMilliC,
                   io.RawEdge, io.RawJunction, io.RawMem,
                   io.SmuGfxclkMhz, io.SmuActiveWgp, io.SmuFeatures);
            fflush(stdout);
            if (intervalMs) Sleep(intervalMs);
        }
        return 0;
    }

    printf("uso: bc250ctl telemetry <sample [smu] | watch [n] [ms] [smu_n]>\n");
    return 1;
}

static const char *RollbackRegionName(uint32_t region)
{
    switch (region) {
    case BC250DBG_ROLLBACK_RGN_RING:    return "anel";
    case BC250DBG_ROLLBACK_RGN_RPTR_WB: return "rptr_wb";
    case BC250DBG_ROLLBACK_RGN_WPTR_SH: return "wptr_shadow";
    case BC250DBG_ROLLBACK_RGN_FENCE:   return "fence";
    default:                            return "?";
    }
}

static void PrintSdmaFenceProof(const BC250DBG_SDMA_OP_IO *io)
{
    printf("  FENCE VRAM 0x%08X : poison 0x%08X -> observado 0x%08X, esperado 0x%08X [%s]\n",
           io->FenceOffset, io->FencePoison, io->FenceObserved,
           io->FenceExpected, io->FenceReached ? "ALCANCADO" : "NAO ALCANCADO");
    printf("  STATUS_REG          : 0x%08X -> 0x%08X\n",
           io->StatusRegBefore, io->StatusRegAfter);
    printf("  STATUS no kick      : 0x%08X\n", io->StatusRegKick);
    printf("  F32 PC              : 0x%04X -> 0x%04X -> 0x%04X\n",
           io->F32PcBefore, io->F32PcKick, io->F32PcAfter);
    printf("  GFXOFF fisico       : %u[%s] -> %u[%s] -> %u[%s]\n",
           io->GfxoffBefore, GfxoffStateName(io->GfxoffBefore),
           io->GfxoffKick, GfxoffStateName(io->GfxoffKick),
           io->GfxoffAfter, GfxoffStateName(io->GfxoffAfter));
    printf("  GFX_MISC raw        : 0x%08X -> 0x%08X -> 0x%08X\n",
           io->GfxMiscBefore, io->GfxMiscKick, io->GfxMiscAfter);
    printf("  GRBM_STATUS         : 0x%08X -> 0x%08X -> 0x%08X\n",
           io->GrbmBefore, io->GrbmKick, io->GrbmAfter);
    printf("  SDMA CLK_CTRL       : 0x%08X -> 0x%08X -> 0x%08X\n",
           io->ClkCtrlBefore, io->ClkCtrlKick, io->ClkCtrlAfter);
    printf("  Fence emitida/kick  : 0x%08X / %s\n",
           io->FenceEmitted, io->Kicked ? "publicado" : "NAO PUBLICADO");
    printf("  WPTR (reg)          : 0x%08X -> 0x%08X\n",
           io->WptrRegBefore, io->WptrRegAfter);
    printf("  RPTR reg/writeback  : 0x%08X / 0x%08X\n",
           io->RptrRegAfter, io->RptrWbAfter);
    printf("  WPTR shadow         : 0x%08X\n", io->WptrShadowAfter);
    printf("  Dados conferidos    : %u dwords, %u divergencias (conferido no kernel)\n",
           io->DataVerifiedDwords, io->DataMismatches);
    printf("  Conclusao do pacote : %s (exige RPTR=WPTR, fence GPU e dado conferido)\n",
           io->Completed ? "SIM" : "NAO");
    if (io->EngineSuspect) {
        printf("  ** ENGINE SUSPEITA nesta sessao: submissoes novas serao recusadas.\n");
        printf("     Nada dentro da sessao restaura a confianca; e preciso reiniciar.\n");
    }
}

/* Traduz uma opcao de caso negativo. Retorna 0 se o token nao for uma opcao. */
static uint32_t SdmaNegativeFlag(const char *arg, const char **outName)
{
    if (_stricmp(arg, "-nokick") == 0) {
        *outName = "SEM KICK";
        return BC250DBG_SDMA_FLAG_NO_KICK;
    }
    if (_stricmp(arg, "-badfence") == 0) {
        *outName = "FENCE INCORRETA";
        return BC250DBG_SDMA_FLAG_BAD_FENCE;
    }
    if (_stricmp(arg, "-badop") == 0) {
        *outName = "OPCODE RESERVADO";
        return BC250DBG_SDMA_FLAG_BAD_OPCODE;
    }
    return 0;
}

/* -ignoresuspect NAO e caso negativo: nao remove nenhuma condicao da prova.
 * Existe porque, depois de a engine ser marcada como suspeita, o driver recusa
 * submissao — e os experimentos de recuperacao precisam medir o que acontece
 * dali em diante. */
static uint32_t SdmaOptionFlag(const char *arg)
{
    if (_stricmp(arg, "-ignoresuspect") == 0) {
        return BC250DBG_SDMA_FLAG_IGNORE_SUSPECT;
    }
    return 0;
}

static int CmdSdma(int argc, char **argv)
{
    if (argc < 3) {
        printf("uso: bc250ctl sdma <init [vram] [offset] | rollback | status | nop [n] | load [fw.bin]>\n");
        return 1;
    }

    if (_stricmp(argv[2], "init") == 0) {
        uint64_t gpuAddr = 0;
        uint32_t rbCntl = 0;
        uint32_t ringDwords = 1024;
        uint32_t placement = BC250DBG_SDMA_RING_VRAM;
        uint32_t vramOffset = BC250_SDMA_RING_VRAM_DEFAULT;
        int rc;

        /* 'sdma init [sysram|vram] [offset]'. VRAM e o default: e o unico
         * lugar que a SDMA alcanca sem GART/VMID0 programado. */
        if (argc >= 4) {
            if (_stricmp(argv[3], "sysram") == 0) {
                placement = BC250DBG_SDMA_RING_SYSRAM;
            } else if (_stricmp(argv[3], "vram") == 0) {
                placement = BC250DBG_SDMA_RING_VRAM;
            } else {
                printf("colocacao desconhecida: %s (use 'vram' ou 'sysram')\n", argv[3]);
                return 1;
            }
        }
        if (argc >= 5) {
            vramOffset = (uint32_t)strtoul(argv[4], NULL, 0);
        }

        /* Tamanho do anel em dwords. O driver exige potencia de dois entre 64
         * e 1024 dwords; um anel pequeno e o unico jeito barato de exercitar
         * wraparound com poucas submissoes. */
        if (argc >= 6) {
            ringDwords = (uint32_t)strtoul(argv[5], NULL, 0);
        }
        rc = Bc250SdmaInit(&g_Ctx, ringDwords, placement, vramOffset, &gpuAddr, &rbCntl);
        if (rc) {
            printf("falha ao inicializar SDMA ring: erro %d (rode 'bc250ctl arm write' antes)\n", rc);
            return 1;
        }
        printf("SDMA0 Ring inicializado com sucesso:\n");
        if (placement == BC250DBG_SDMA_RING_VRAM) {
            printf("  Local                : VRAM, offset 0x%X\n", vramOffset & ~0xFFu);
            printf("  Endereco de GPU      : 0x%llX\n", (unsigned long long)gpuAddr);
        } else {
            printf("  Local                : RAM do sistema\n");
            printf("  Endereco Fisico (PA) : 0x%llX\n", (unsigned long long)gpuAddr);
            printf("  ** Sem GART/VMID0 a SDMA nao alcanca esse endereco.\n");
        }
        printf("  Tamanho do anel      : %u dwords (%u bytes)\n",
               ringDwords, ringDwords * 4u);
        printf("  RB_CNTL              : 0x%08X (%s, RB_SIZE=%u)\n",
               rbCntl, (rbCntl & 1) ? "HABILITADO" : "DESABILITADO",
               (rbCntl >> 1) & 0x1F);
        return 0;
    }

    if (_stricmp(argv[2], "rollback") == 0 ||
        _stricmp(argv[2], "free") == 0) {
        BC250DBG_SDMA_ROLLBACK_IO io;
        int rc;

        memset(&io, 0, sizeof(io));
        rc = Bc250SdmaRollback(&g_Ctx, &io);
        printf("Rollback SDMA0:\n");
        printf("  snapshot presente    : %s\n", io.SnapshotPresent ? "sim" : "nao");
        printf("  registradores        : %u restaurados, %u divergencias\n",
               io.RegistersRestored, io.RegisterMismatches);
        printf("  VRAM                  : %u dwords restaurados, %u divergencias\n",
               io.VramDwordsRestored, io.VramMismatches);
        {
            /* Onde exatamente divergiu. Sem isto, um rollback que falha nao
             * da nenhuma pista de investigacao. */
            uint32_t k;
            for (k = 0; k < io.RegBadCount; k++) {
                printf("  REG divergente        : 0x%05X lido 0x%08X, esperado 0x%08X\n",
                       io.RegBadOffset[k], io.RegBadGot[k], io.RegBadWant[k]);
            }
            for (k = 0; k < io.BadCount; k++) {
                printf("  VRAM divergente       : %-10s 0x%08X lido 0x%08X, esperado 0x%08X\n",
                       RollbackRegionName(io.BadRegion[k]), io.BadOffset[k],
                       io.BadGot[k], io.BadWant[k]);
            }
        }
        if (rc || io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO             : FALHA (erro %d, status %u)\n", rc, io.Status);
            return 1;
        }
        printf("  RESULTADO             : estado anterior restaurado\n");
        return 0;
    }

    if (_stricmp(argv[2], "write") == 0 && argc >= 5) {
        /* WRITE_LINEAR e o pacote mais simples que toca memoria: escreve
         * dwords literais. Se ele grava e o CONST_FILL nao, o problema esta na
         * codificacao do fill; se nenhum dos dois grava, esta no endereco ou
         * na coerencia entre o caminho do GPU e a abertura do BAR0. */
        BC250DBG_SDMA_OP_IO io;
        int rc, i, n, dataOk = 1, base = 3;
        uint32_t flags = 0;
        const char *negName = NULL;
        /* Passo 5 do gate de memoria: o endereco do pacote pode ser um VA que a
         * page table resolve para outro lugar. Nesse caso o offset do pacote
         * NAO e onde o dado aparece, e conferir la leria o lugar errado. */
        uint32_t addrMode = BC250DBG_ADDR_GPU_BASE;
        uint32_t verifyAt = 0, verifyValid = 0;
        unsigned long long rawDst = 0;

        /* Opcoes de caso negativo vem antes do offset. Cada uma remove uma
         * condicao obrigatoria; o resultado esperado passa a ser FALHA. */
        while (base < argc && argv[base][0] == '-') {
            const char *name = NULL;
            uint32_t opt = SdmaOptionFlag(argv[base]);
            uint32_t f;

            if (opt != 0) {
                flags |= opt;
                base++;
                continue;
            }
            if (_stricmp(argv[base], "-raw") == 0) {
                /* O endereco vai cru no pacote, sem somar a base de VRAM.
                 * E a unica forma de submeter um VA que so a traducao resolve. */
                addrMode = BC250DBG_ADDR_RAW;
                base++;
                continue;
            }
            if (_stricmp(argv[base], "-rawaddr") == 0 && base + 1 < argc) {
                /* Endereco cru de 64 bits. Necessario porque abaixo de 4 GB
                 * todo endereco e memoria de sistema real: um VA de teste tem
                 * de ser inofensivo caso seja interpretado como fisico. */
                rawDst = strtoull(argv[base + 1], NULL, 0);
                addrMode = BC250DBG_ADDR_RAW;
                base += 2;
                continue;
            }
            if (_stricmp(argv[base], "-verifyat") == 0 && base + 1 < argc) {
                verifyAt = (uint32_t)strtoul(argv[base + 1], NULL, 0);
                verifyValid = 1;
                base += 2;
                continue;
            }
            f = SdmaNegativeFlag(argv[base], &name);
            if (f == 0) {
                printf("opcao desconhecida: %s\n", argv[base]);
                return 1;
            }
            if ((flags & BC250DBG_SDMA_NEGATIVE_MASK) != 0) {
                printf("use apenas um caso negativo por vez.\n");
                return 1;
            }
            flags |= f;
            negName = name;
            base++;
        }
        if (argc - base < 2) {
            printf("uso: bc250ctl sdma write [-nokick|-badfence|-badop] <off> <dw..>\n");
            return 1;
        }

        n = argc - base - 1;
        if (n > BC250DBG_SDMA_WRITE_MAX_DW) n = BC250DBG_SDMA_WRITE_MAX_DW;
        memset(&io, 0, sizeof(io));
        io.Op         = BC250DBG_SDMA_OP_WRITE;
        io.Flags      = flags;
        io.DstOffset  = (uint32_t)strtoul(argv[base], NULL, 0);
        io.DwordCount = (uint32_t)n;
        io.AddrMode   = addrMode;
        io.RawDstGpu  = rawDst;
        io.VerifyVramOffset = verifyAt;
        io.VerifyVramValid  = verifyValid;
        for (i = 0; i < n; i++) {
            io.Data[i] = (uint32_t)strtoul(argv[base + 1 + i], NULL, 0);
        }

        rc = Bc250SdmaOp(&g_Ctx, &io);
        if (rc) { printf("falha no WRITE_LINEAR: erro %d\n", rc); return 1; }
        if (negName) {
            printf("CASO NEGATIVO [%s]: sucesso e proibido.\n", negName);
        }
        if (addrMode == BC250DBG_ADDR_RAW) {
            printf("WRITE_LINEAR: endereco CRU 0x%llX no pacote, %u dwords\n",
                   rawDst ? rawDst : (unsigned long long)io.DstOffset,
                   io.DwordCount);
            printf("  ** endereco cru: nenhuma base de VRAM foi somada. Se este\n");
            printf("     endereco so existir como VA, o dado aparece onde a page\n");
            printf("     table mandar, e nao aqui.\n");
        } else {
            printf("WRITE_LINEAR: VRAM 0x%08X, %u dwords\n", io.DstOffset, io.DwordCount);
        }
        if (verifyValid) {
            printf("  conferencia em      : VRAM 0x%08X (redirecionada)\n", verifyAt);
        }
        printf("  RPTR 0x%X -> 0x%X (WPTR 0x%X)  %s\n",
               io.RptrBefore, io.RptrAfter, io.Wptr,
               io.Completed ? "concluido" : "*** TIMEOUT ***");
        PrintSdmaFenceProof(&io);

        {
            uint32_t readAt = verifyValid ? verifyAt : io.DstOffset;

            printf("  leitura de volta pelo BAR0 (offset 0x%08X):\n", readAt);
            for (i = 0; i < n; i++) {
                uint32_t v = 0;
                Bc250VramRead(&g_Ctx, readAt + i * 4, &v);
                printf("    0x%08X = 0x%08X  %s\n", readAt + i * 4, v,
                       (v == io.Data[i]) ? "ok" : "DIVERGIU");
                if (v != io.Data[i]) dataOk = 0;
            }
        }

        if ((flags & BC250DBG_SDMA_NEGATIVE_MASK) == 0) {
            printf("  Resultado completo  : %s\n",
                   (io.Completed && dataOk) ? "VALIDADO NESTA OPERACAO" : "FALHOU");
            return (io.Completed && dataOk) ? 0 : 1;
        }

        /* Avaliacao do caso negativo. Alem de exigir Completed=0, cada modo
         * tem uma evidencia propria do que deixou de acontecer. */
        {
            int ok = (io.Completed == 0);

            if (flags & BC250DBG_SDMA_FLAG_NO_KICK) {
                /* Nada pode ter sido publicado nem consumido.
                 *
                 * O criterio do RPTR NAO e "ficou igual ao valor de antes":
                 * medimos nesta placa que o writeback chega atrasado em
                 * relacao ao registrador, entao ele pode legitimamente
                 * avancar sozinho ate o ultimo WPTR ja publicado enquanto
                 * este comando roda. O que nao pode acontecer e o RPTR passar
                 * do ultimo WPTR publicado: isso significaria que a engine
                 * consumiu bytes que nunca foram anunciados. */
                if (io.Kicked != 0) ok = 0;
                if (io.WptrRegAfter != io.WptrRegBefore) ok = 0;
                if (io.RptrAfter > io.WptrRegBefore) ok = 0;
                if (io.FenceObserved != io.FencePoison) ok = 0;
                /* O destino nao pode conter os dados desta operacao. Quem
                 * chama tem de usar valores que ainda nao estao la. */
                if (dataOk) ok = 0;
            }
            if (flags & BC250DBG_SDMA_FLAG_BAD_FENCE) {
                /* A GPU escreve, mas com valor diferente do esperado: prova
                 * que a comparacao de fence e real. */
                if (io.FenceReached != 0) ok = 0;
                if (io.FenceObserved != io.FenceEmitted) ok = 0;
                if (io.FenceObserved == io.FenceExpected) ok = 0;
            }
            if (flags & BC250DBG_SDMA_FLAG_BAD_OPCODE) {
                /* Nenhum pacote FENCE foi emitido: a fence tem de continuar
                 * com o poison escrito pela CPU. */
                if (io.FenceObserved != io.FencePoison) ok = 0;
                if (io.FenceReached != 0) ok = 0;
            }

            printf("  Caso negativo       : %s\n",
                   ok ? "REPROVADO COMO ESPERADO" : "*** COMPORTAMENTO INESPERADO ***");
            return ok ? 0 : 1;
        }
    }

    if (_stricmp(argv[2], "fill") == 0 && argc >= 6) {
        BC250DBG_SDMA_OP_IO io;
        int rc;
        memset(&io, 0, sizeof(io));
        io.Op        = BC250DBG_SDMA_OP_FILL;
        io.DstOffset = (uint32_t)strtoul(argv[3], NULL, 0);
        io.Value     = (uint32_t)strtoul(argv[4], NULL, 0);
        io.ByteCount = (uint32_t)strtoul(argv[5], NULL, 0);
        io.FillSize  = (argc >= 7) ? (uint32_t)strtoul(argv[6], NULL, 0) : 2u;

        rc = Bc250SdmaOp(&g_Ctx, &io);
        if (rc) { printf("falha no CONST_FILL: erro %d\n", rc); return 1; }
        printf("CONST_FILL: VRAM 0x%08X, %u bytes com 0x%08X\n",
               io.DstOffset, io.ByteCount, io.Value);
        printf("  RPTR 0x%X -> 0x%X (WPTR 0x%X)  %s\n",
               io.RptrBefore, io.RptrAfter, io.Wptr,
               io.Completed ? "concluido" : "*** TIMEOUT ***");
        PrintSdmaFenceProof(&io);
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "copy") == 0 && argc >= 6) {
        BC250DBG_SDMA_OP_IO io;
        int rc;
        memset(&io, 0, sizeof(io));
        io.Op        = BC250DBG_SDMA_OP_COPY;
        io.SrcOffset = (uint32_t)strtoul(argv[3], NULL, 0);
        io.DstOffset = (uint32_t)strtoul(argv[4], NULL, 0);
        io.ByteCount = (uint32_t)strtoul(argv[5], NULL, 0);

        rc = Bc250SdmaOp(&g_Ctx, &io);
        if (rc) { printf("falha no COPY: erro %d\n", rc); return 1; }
        printf("COPY_LINEAR: VRAM 0x%08X -> 0x%08X, %u bytes\n",
               io.SrcOffset, io.DstOffset, io.ByteCount);
        printf("  RPTR 0x%X -> 0x%X (WPTR 0x%X)  %s\n",
               io.RptrBefore, io.RptrAfter, io.Wptr,
               io.Completed ? "concluido" : "*** TIMEOUT ***");
        PrintSdmaFenceProof(&io);
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "srbmtest") == 0) {
        /* O TESTE QUE PARTICIONA O PROBLEMA.
         *
         * SDMA_OP_SRBM_WRITE escreve um REGISTRADOR, nao memoria. Executar
         * esse pacote exige que a GPU (a) leia o conteudo do anel de verdade
         * e (b) decodifique o pacote — mas NAO exige nenhuma escrita em
         * memoria, que e onde estamos travados.
         *
         * Resultado muda tudo:
         *   SCRATCH mudou   -> leitura do anel e decodificacao funcionam; o
         *                      problema esta isolado na escrita em memoria (VM)
         *   SCRATCH nao mudou -> o problema e anterior: a engine nao le o
         *                        nosso anel ou nao decodifica. Mexer em GART
         *                        seria perda de tempo.
         *
         * Pacote (navi10_sdma_pkt_open.h, 3 dwords):
         *   dw0 = op(14) | byte_en(0xF) << 28
         *   dw1 = addr em DWORDS (18 bits)
         *   dw2 = dado
         *
         * Alvo: SCRATCH_REG0. Byte 0x30100 -> dword 0xC040. */
        const uint32_t SCRATCH_BYTE = 0x30100u;
        const uint32_t SCRATCH_DW   = SCRATCH_BYTE / 4u;
        const uint32_t MAGIC        = 0x5121A7E5u;
        BC250DBG_SDMA_RAW_IO io;
        uint32_t antes = 0, depois = 0, rb = 0;
        int rc;

        printf("Teste SRBM_WRITE — a engine executa pacotes do anel?\n\n");
        printf("  AVISO: SRBM e um barramento pre-GFX9. Medido nesta placa: a engine\n"
               "  PARSEIA o pacote (o RPTR para no meio dele, em 0xE8 de um pacote\n"
               "  que comeca em 0xE0) mas nao o completa — provavelmente SRBM_WRITE\n"
               "  nao existe nesta geracao. O teste TRAVA a engine; recupere com\n"
               "  'psp loadfw sdma0'. Para prova de execucao use 'sdma verify'.\n\n");

        /* Semente conhecida, para nao confundir com valor residual. */
        Bc250RegWrite(&g_Ctx, SCRATCH_BYTE, 0x11111111u, &rb);
        Bc250RegRead(&g_Ctx, SCRATCH_BYTE, &antes);
        printf("  SCRATCH_REG0 (0x%05X, dword 0x%05X) semeado com 0x%08X\n",
               SCRATCH_BYTE, SCRATCH_DW, antes);

        memset(&io, 0, sizeof(io));
        io.DwordCount = 3;
        io.Data[0] = 14u | (0xFu << 28);   /* SRBM_WRITE, todos os bytes */
        io.Data[1] = SCRATCH_DW & 0x3FFFFu;
        io.Data[2] = MAGIC;

        printf("  pacote: 0x%08X 0x%08X 0x%08X\n", io.Data[0], io.Data[1], io.Data[2]);

        rc = Bc250SdmaRaw(&g_Ctx, &io);
        if (rc) { printf("\n  submissao falhou: erro %d (anel iniciado? 'arm write'?)\n", rc); return 1; }

        printf("  RPTR 0x%X -> 0x%X   WPTR 0x%X   PC 0x%04X\n",
               io.RptrBefore, io.RptrAfter, io.Wptr, io.F32Pc);

        Bc250RegRead(&g_Ctx, SCRATCH_BYTE, &depois);
        printf("  SCRATCH_REG0 depois: 0x%08X\n\n", depois);

        if (depois == MAGIC) {
            printf("  ** A ENGINE EXECUTOU O PACOTE.\n"
                   "     Ela le o anel e decodifica corretamente. O bloqueio\n"
                   "     esta isolado na ESCRITA EM MEMORIA — ou seja, na\n"
                   "     traducao de endereco (GCVM/GART). Proximo passo:\n"
                   "     montar VMID 0 de verdade.\n");
            Bc250RegWrite(&g_Ctx, SCRATCH_BYTE, antes, &rb);
            return 0;
        }

        printf("  ** A ENGINE NAO EXECUTOU O PACOTE.\n"
               "     O problema e ANTERIOR a memoria: ou ela nao le o conteudo\n"
               "     do nosso anel, ou nao decodifica. Mexer em GART agora\n"
               "     seria perda de tempo.\n");
        if (io.RptrAfter == io.Wptr) {
            printf("     Note que o RPTR alcancou o WPTR mesmo assim — o\n"
                   "     ponteiro anda sem execucao, que e a assinatura de\n"
                   "     leitura do anel devolvendo zeros (0x00 = NOP).\n");
        }
        Bc250RegWrite(&g_Ctx, SCRATCH_BYTE, antes, &rb);
        return 1;
    }

    if (_stricmp(argv[2], "parsetest") == 0) {
        /* A engine decodifica os pacotes ou so arrasta o RPTR ate o WPTR?
         *
         * Emitimos um NOP anunciando 8 dwords extras e fornecemos apenas o
         * cabecalho. Uma engine que decodifica fica esperando os 8 dwords que
         * faltam e o RPTR PARA. Uma que so arrasta vai direto ate o WPTR.
         *
         * Nao e destrutivo: se ela parar, mandamos os 8 dwords que faltam e
         * ela retoma. */
        BC250DBG_SDMA_RAW_IO io;
        int rc, i;

        printf("Teste de decodificacao de pacotes\n\n");

        memset(&io, 0, sizeof(io));
        io.DwordCount = 1;
        io.Data[0] = 0u | (8u << 16);   /* SDMA_OP_NOP com count=8 */
        printf("1. NOP anunciando 8 dwords extras, enviando so o cabecalho...\n");
        rc = Bc250SdmaRaw(&g_Ctx, &io);
        if (rc) { printf("   IOCTL falhou: erro %d\n", rc); return 1; }
        printf("   RPTR 0x%X -> 0x%X   WPTR 0x%X   STATUS 0x%08X   PC 0x%04X\n",
               io.RptrBefore, io.RptrAfter, io.Wptr, io.StatusReg, io.F32Pc);
        printf("\n");

        if (io.RptrAfter == io.Wptr) {
            printf("  ** A engine consumiu um pacote INCOMPLETO.\n"
                   "     Ela nao esta decodificando: so arrasta o RPTR ate o\n"
                   "     WPTR. Isso significa que 'RPTR alcancou WPTR' nunca\n"
                   "     provou execucao — nem para os NOPs. O microcodigo\n"
                   "     roda (o PC se move), mas nao esta processando o anel.\n");
            return 1;
        }

        printf("  ** RPTR PAROU no pacote incompleto: a engine DECODIFICA.\n"
               "     Entao os pacotes sao mesmo executados, e o problema das\n"
               "     escritas esta na traducao de endereco (VM), nao na engine.\n\n");

        printf("2. Completando os 8 dwords que faltam...\n");
        memset(&io, 0, sizeof(io));
        io.DwordCount = 8;
        for (i = 0; i < 8; i++) io.Data[i] = 0;
        rc = Bc250SdmaRaw(&g_Ctx, &io);
        if (rc) { printf("   IOCTL falhou: erro %d\n", rc); return 1; }
        printf("   RPTR 0x%X -> 0x%X   WPTR 0x%X   %s\n",
               io.RptrBefore, io.RptrAfter, io.Wptr,
               io.Completed ? "retomou" : "continua parada");
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "diag") == 0) {
        /* Testa todas as hipoteses de uma vez, em vez de uma por rodada.
         * Para cada combinacao escreve um valor magico distinto e depois
         * varre a VRAM inteira procurando por ele — assim descobrimos nao so
         * SE a escrita funcionou, mas ONDE ela pousou quando nao funcionou. */
        uint32_t base = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0x0E800000u;
        struct { const char *nome; uint32_t addrMode; uint32_t flags; uint32_t magic; } casos[] = {
            { "GPU base + offset, com GCR",  BC250DBG_ADDR_GPU_BASE, 0,                          0x5EED0001u },
            { "GPU base + offset, sem GCR",  BC250DBG_ADDR_GPU_BASE, BC250DBG_SDMA_FLAG_NO_GCR,  0x5EED0002u },
            { "offset cru, com GCR",         BC250DBG_ADDR_RAW,      0,                          0x5EED0003u },
            { "offset cru, sem GCR",         BC250DBG_ADDR_RAW,      BC250DBG_SDMA_FLAG_NO_GCR,  0x5EED0004u },
        };
        int nc = (int)(sizeof(casos) / sizeof(casos[0])), c, achou = 0;
        uint32_t orig = 0, v;

        printf("Diagnostico de escrita da SDMA (destino base 0x%08X)\n\n", base);
        Bc250VramRead(&g_Ctx, base, &orig);
        printf("conteudo original do destino: 0x%08X\n\n", orig);

        for (c = 0; c < nc; c++) {
            BC250DBG_SDMA_OP_IO io;
            BC250DBG_VRAM_SCAN_IO scan;
            uint32_t off = base + (uint32_t)c * 0x1000u;
            int rc;

            printf("[%d] %s\n", c + 1, casos[c].nome);

            memset(&io, 0, sizeof(io));
            io.Op = BC250DBG_SDMA_OP_WRITE;
            io.DstOffset = off;
            io.DwordCount = 1;
            io.Data[0] = casos[c].magic;
            io.AddrMode = casos[c].addrMode;
            io.Flags = casos[c].flags;

            rc = Bc250SdmaOp(&g_Ctx, &io);
            if (rc) { printf("    IOCTL falhou: erro %d\n\n", rc); continue; }
            printf("    RPTR 0x%X -> 0x%X  %s\n", io.RptrBefore, io.RptrAfter,
                   io.Completed ? "consumido" : "TIMEOUT");

            Bc250VramRead(&g_Ctx, off, &v);
            printf("    leitura direta      : 0x%08X %s\n", v,
                   (v == casos[c].magic) ? "<== GRAVOU" : "");
            if (v == casos[c].magic) achou = 1;

            Bc250HdpInvalidate(&g_Ctx);
            Bc250VramRead(&g_Ctx, off, &v);
            printf("    apos invalidar HDP  : 0x%08X %s\n", v,
                   (v == casos[c].magic) ? "<== GRAVOU (o HDP cacheava)" : "");
            if (v == casos[c].magic) achou = 1;

            memset(&scan, 0, sizeof(scan));
            scan.Value = casos[c].magic;
            printf("    varrendo a VRAM...");
            fflush(stdout);
            if (Bc250VramScan(&g_Ctx, &scan)) {
                printf(" falhou\n");
            } else if (scan.HitCount == 0) {
                printf(" nao esta em lugar nenhum dos 256 MB\n");
            } else {
                uint32_t k, n = scan.HitCount;
                if (n > BC250DBG_VRAM_SCAN_MAX_HITS) n = BC250DBG_VRAM_SCAN_MAX_HITS;
                printf(" %u ocorrencia(s):", scan.HitCount);
                for (k = 0; k < n; k++) printf(" 0x%08X", scan.Hits[k]);
                printf("\n");
            }
            printf("\n");
        }

        printf("--- conclusao ---\n");
        if (achou) {
            printf("Alguma combinacao gravou. Use a que funcionou como padrao.\n");
        } else {
            printf("Nenhuma combinacao gravou, e o valor nao aparece em nenhum\n"
                   "lugar dos 256 MB da janela. A escrita esta sendo descartada\n"
                   "antes da memoria — resta a VM: GCVM_CONTEXT0_CNTL tem\n"
                   "ENABLE_CONTEXT=0 com os *_FAULT_ENABLE_DEFAULT em 1, ou seja,\n"
                   "acesso que falha e desviado em silencio. O proximo passo e\n"
                   "montar GART/VMID0.\n");
        }
        return achou ? 0 : 1;
    }

    if (_stricmp(argv[2], "stress") == 0) {
        /* Mesmo alvo do comando de topo 'bc250ctl stress'. Aceito aqui porque
         * e onde a documentacao e o ciclo.cmd mandam procurar. */
        return Bc250RunStress(&g_Ctx);
    }

    if (_stricmp(argv[2], "verify") == 0) {
        /* Teste de fechamento da Fase 2/3: a SDMA escreve na VRAM por DMA e a
         * CPU le de volta pela abertura do BAR0. Dois caminhos independentes
         * conferindo o mesmo dado — se batem, a engine esta movendo bytes de
         * verdade, nao apenas retirando pacotes do anel. */
        uint32_t base = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0x0F000000u;
        uint32_t bytes = 4096, i, v, bad = 0;
        const uint32_t pattern = 0xC0FFEE00u;
        BC250DBG_SDMA_OP_IO io;
        int rc;

        printf("Teste de ida e volta pela SDMA em VRAM 0x%08X (%u bytes):\n\n", base, bytes);

        printf("1. Sujando a regiao pela CPU (BAR0)...\n");
        for (i = 0; i < bytes; i += 4) {
            if (Bc250VramWrite(&g_Ctx, base + i, 0xDEADBEEFu, NULL)) {
                printf("   falha ao escrever na VRAM (rode 'arm write')\n");
                return 1;
            }
        }
        Bc250VramRead(&g_Ctx, base, &v);
        printf("   0x%08X (confirmado sujo)\n\n", v);

        printf("2. Sonda WRITE_LINEAR (4 dwords literais)...\n");
        memset(&io, 0, sizeof(io));
        io.Op = BC250DBG_SDMA_OP_WRITE;
        io.DstOffset = base;
        io.DwordCount = 4;
        io.Data[0] = 0x11111111u; io.Data[1] = 0x22222222u;
        io.Data[2] = 0x33333333u; io.Data[3] = 0x44444444u;
        rc = Bc250SdmaOp(&g_Ctx, &io);
        if (rc || !io.Completed) {
            printf("   FALHOU (erro %d, completed=%u)\n", rc, io.Completed);
            return 1;
        }
        {
            int wrOk = 1;
            Bc250HdpInvalidate(&g_Ctx);
            for (i = 0; i < 4; i++) {
                Bc250VramRead(&g_Ctx, base + i * 4, &v);
                printf("   0x%08X = 0x%08X  %s\n", base + i * 4, v,
                       (v == io.Data[i]) ? "ok" : "DIVERGIU");
                if (v != io.Data[i]) wrOk = 0;
            }
            if (!wrOk) {
                printf("\n  ** Nem o WRITE_LINEAR grava. O problema nao e a\n"
                       "     codificacao do CONST_FILL: ou o endereco de destino\n"
                       "     nao e o que pensamos, ou o que a GPU escreve nao\n"
                       "     esta visivel pela abertura do BAR0 (coerencia).\n");
                return 1;
            }
            printf("   ** WRITE_LINEAR grava. O caminho de escrita funciona.\n\n");
        }

        printf("3. CONST_FILL pela SDMA com 0x%08X (FILLSIZE=dword)...\n", pattern);
        memset(&io, 0, sizeof(io));
        io.Op = BC250DBG_SDMA_OP_FILL;
        io.DstOffset = base;
        io.Value = pattern;
        io.ByteCount = bytes;
        io.FillSize = 2;   /* dword */
        rc = Bc250SdmaOp(&g_Ctx, &io);
        if (rc || !io.Completed) {
            printf("   FALHOU (erro %d, completed=%u)\n", rc, io.Completed);
            return 1;
        }
        printf("   RPTR 0x%X -> 0x%X, concluido\n\n", io.RptrBefore, io.RptrAfter);

        printf("3. Lendo de volta pela CPU (BAR0)...\n");
        Bc250HdpInvalidate(&g_Ctx);
        for (i = 0; i < bytes; i += 4) {
            if (Bc250VramRead(&g_Ctx, base + i, &v)) { printf("   falha na leitura\n"); return 1; }
            if (v != pattern) {
                if (bad < 4) printf("   0x%08X: esperado 0x%08X, lido 0x%08X\n",
                                    base + i, pattern, v);
                bad++;
            }
        }

        if (bad) {
            printf("\n  ** %u de %u dwords divergiram.\n", bad, bytes / 4);
            printf("     Se TODOS divergiram com um padrao regular, o campo\n"
                   "     FILLSIZE do CONST_FILL pode precisar ser dword (2)\n"
                   "     em vez de byte (0).\n");
            return 1;
        }

        printf("   todos os %u dwords batem.\n\n", bytes / 4);
        printf("  ** A SDMA moveu bytes de verdade na VRAM.\n");

        printf("\n4. COPY_LINEAR de 0x%08X para 0x%08X...\n", base, base + bytes);
        memset(&io, 0, sizeof(io));
        io.Op = BC250DBG_SDMA_OP_COPY;
        io.SrcOffset = base;
        io.DstOffset = base + bytes;
        io.ByteCount = bytes;
        rc = Bc250SdmaOp(&g_Ctx, &io);
        if (rc || !io.Completed) {
            printf("   FALHOU (erro %d, completed=%u)\n", rc, io.Completed);
            return 1;
        }
        bad = 0;
        for (i = 0; i < bytes; i += 4) {
            Bc250VramRead(&g_Ctx, base + bytes + i, &v);
            if (v != pattern) bad++;
        }
        if (bad) {
            printf("   ** %u de %u dwords divergiram no destino da copia.\n",
                   bad, bytes / 4);
            return 1;
        }
        printf("   todos os %u dwords batem.\n\n", bytes / 4);
        printf("  ** COPY_LINEAR tambem funciona. Fase 3 operacional.\n");
        return 0;
    }

    if (_stricmp(argv[2], "enginereset") == 0) {
        /* Ultimo recurso antes do reboot: reseta o bloco SDMA0 inteiro. Os
         * demais bits de GRBM_SOFT_RESET sao preservados. */
        BC250DBG_SDMA_ENGINE_RESET_IO io;
        int rc = Bc250SdmaEngineReset(&g_Ctx, &io);

        if (rc) {
            printf("soft reset falhou: erro %d\n", rc);
            printf("  precisa de 'arm write' e de nenhum snapshot aberto:\n");
            printf("  rode 'sdma rollback' antes se um anel foi criado.\n");
            return 1;
        }
        printf("Soft reset do bloco SDMA0:\n");
        printf("  FREEZE          : 0x%08X -> 0x%08X, FROZEN=%s apos %u iteracoes\n",
               io.FreezeBefore, io.FreezeAfterSet,
               io.FrozenObserved ? "sim" : "nao", io.FreezeIterations);
        printf("  STATUS1_REG     : 0x%08X (ocioso pelo criterio do upstream: %s)\n",
               io.Status1Before, io.Status1Idle ? "sim" : "nao");
        printf("  GRBM_SOFT_RESET : 0x%08X -> 0x%08X -> 0x%08X\n",
               io.GrbmBefore, io.GrbmWithReset, io.GrbmAfterClear);
        printf("  STATUS_REG      : 0x%08X -> 0x%08X\n",
               io.StatusRegBefore, io.StatusRegAfter);
        printf("  STATUS2_REG     : 0x%08X -> 0x%08X\n",
               io.Status2Before, io.Status2After);
        printf("  CMD_OP latchado : %u -> %u\n", io.CmdOpBefore, io.CmdOpAfter);
        printf("  CNTL / F32_CNTL : 0x%08X / 0x%08X\n", io.CntlAfter, io.F32CntlAfter);
        printf("  FREEZE final    : 0x%08X\n", io.FreezeFinal);
        /* Observacao, nao restauracao. O controle positivo de 2026-08-23
         * mostrou que a engine volta a escrever sem reprogramar nada disso:
         * quem reestabelece o bloco e o firmware pelo PSP mais o 'sdma init'. */
        printf("  Config (apenas observada, nao reprogramada):\n");
        printf("    UTCL1_PAGE    : 0x%08X -> reset 0x%08X -> 0x%08X\n",
               io.Utcl1PageBefore, io.Utcl1PageReset, io.Utcl1PageAfter);
        printf("    UTCL1_CNTL    : 0x%08X -> reset 0x%08X -> 0x%08X\n",
               io.Utcl1CntlBefore, io.Utcl1CntlReset, io.Utcl1CntlAfter);
        printf("    CHICKEN_BITS  : 0x%08X -> reset 0x%08X -> 0x%08X\n",
               io.ChickenBefore, io.ChickenReset, io.ChickenAfter);
        printf("    UTCL1_WATERMK : 0x%08X -> 0x%08X\n",
               io.Utcl1WatermkBefore, io.Utcl1WatermkAfter);
        printf("    CLK_CTRL      : 0x%08X -> 0x%08X\n",
               io.ClkCtrlBefore, io.ClkCtrlAfter);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO       : RECUSADO (status %u)\n", io.Status);
            return 1;
        }
        printf("  RESULTADO       : bloco resetado; rode 'sdma init' antes de submeter.\n");
        return 0;
    }

    if (_stricmp(argv[2], "opgate") == 0) {
        /* Bateria da FRONTEIRA fail-closed do IOCTL de operacao SDMA.
         *
         * O primeiro requisito do gate reescrito e "o driver nunca emite pacote
         * malformado". Isso nao se prova com um caso de sucesso: prova-se
         * mostrando que toda entrada invalida e recusada ANTES de virar pacote,
         * e que nenhuma delas mexe no ponteiro do anel.
         *
         * Cada caso abaixo tem de falhar. Um unico que passe significa que
         * existe caminho para o driver montar pacote a partir de entrada que
         * ele nao validou. */
        BC250DBG_SDMA_STATUS_IO st;
        uint32_t wptrAntes = 0, wptrDepois = 0;
        int i, recusados = 0, aceitos = 0;

        struct {
            const char *nome;
            uint32_t op, flags, dst, dwords, fenceOff, poison, expected;
        } casos[] = {
            { "opcode 0 (invalido)",           0,                       0, 0x0FF04200, 4, 0, 0, 0 },
            { "opcode 99 (invalido)",          99,                      0, 0x0FF04200, 4, 0, 0, 0 },
            { "flag desconhecida 0x80",        BC250DBG_SDMA_OP_WRITE,  0x80, 0x0FF04200, 4, 0, 0, 0 },
            { "dois negativos juntos",         BC250DBG_SDMA_OP_WRITE,
              BC250DBG_SDMA_FLAG_NO_KICK | BC250DBG_SDMA_FLAG_BAD_FENCE, 0x0FF04200, 4, 0, 0, 0 },
            { "fence fora do dword reservado", BC250DBG_SDMA_OP_WRITE,  0, 0x0FF04200, 4, 0x0FF05000, 0, 0 },
            { "zero dwords",                   BC250DBG_SDMA_OP_WRITE,  0, 0x0FF04200, 0, 0, 0, 0 },
            { "dwords acima do maximo",        BC250DBG_SDMA_OP_WRITE,  0, 0x0FF04200,
              BC250DBG_SDMA_WRITE_MAX_DW + 1, 0, 0, 0 },
            { "destino desalinhado",           BC250DBG_SDMA_OP_WRITE,  0, 0x0FF04201, 4, 0, 0, 0 },
            { "poison igual ao esperado",      BC250DBG_SDMA_OP_WRITE,  0, 0x0FF04200, 4, 0,
              0xABCD1234u, 0xABCD1234u },
        };

        memset(&st, 0, sizeof(st));
        if (Bc250SdmaStatus(&g_Ctx, &st) == 0) wptrAntes = st.Wptr;

        printf("Fronteira fail-closed do IOCTL de operacao SDMA:\n");
        printf("  WPTR antes da bateria : 0x%08X\n\n", wptrAntes);

        for (i = 0; i < (int)(sizeof(casos) / sizeof(casos[0])); i++) {
            BC250DBG_SDMA_OP_IO io;
            int rc, j;

            memset(&io, 0, sizeof(io));
            io.Op = casos[i].op;
            io.Flags = casos[i].flags;
            io.AddrMode = BC250DBG_ADDR_GPU_BASE;
            io.DstOffset = casos[i].dst;
            io.DwordCount = casos[i].dwords;
            io.FenceOffset = casos[i].fenceOff;
            io.FencePoison = casos[i].poison;
            io.FenceExpected = casos[i].expected;
            for (j = 0; j < (int)casos[i].dwords && j < BC250DBG_SDMA_WRITE_MAX_DW; j++) {
                io.Data[j] = 0xDEADBE00u + (uint32_t)j;
            }

            rc = Bc250SdmaOp(&g_Ctx, &io);
            if (rc != 0) {
                printf("  [recusado] %-32s erro %d\n", casos[i].nome, rc);
                recusados++;
            } else {
                printf("  [ACEITO  ] %-32s *** o driver montou pacote a partir disto\n",
                       casos[i].nome);
                aceitos++;
            }
        }

        memset(&st, 0, sizeof(st));
        if (Bc250SdmaStatus(&g_Ctx, &st) == 0) wptrDepois = st.Wptr;

        printf("\n  WPTR depois da bateria: 0x%08X %s\n", wptrDepois,
               (wptrDepois == wptrAntes) ? "(inalterado)" : "*** MUDOU");
        printf("  Resultado: %d recusados, %d aceitos\n", recusados, aceitos);
        if (aceitos == 0 && wptrDepois == wptrAntes) {
            printf("  FRONTEIRA OK: nenhuma entrada invalida virou pacote.\n");
            return 0;
        }
        printf("  FRONTEIRA FALHOU.\n");
        return 1;
    }

    if (_stricmp(argv[2], "fault") == 0) {
        /* Diagnostico do estado em que a engine consome o anel sem escrever
         * memoria. Somente leitura: nao arma nada e nao muda estado. */
        BC250DBG_SDMA_FAULT_IO io;
        int rc = Bc250SdmaFault(&g_Ctx, &io);
        uint32_t xnack;

        if (rc) { printf("falha no dump: erro %d\n", rc); return 1; }

        printf("Dump de falha da SDMA0 (somente leitura):\n");
        printf("  STATUS_REG     : 0x%08X\n", io.StatusReg);
        printf("  STATUS1_REG    : 0x%08X\n", io.Status1Reg);
        printf("  STATUS2_REG    : 0x%08X\n", io.Status2Reg);
        printf("  STATUS3_REG    : 0x%08X\n", io.Status3Reg);
        printf("  FREEZE         : 0x%08X\n", io.Freeze);
        printf("  ERROR_LOG      : 0x%08X\n", io.ErrorLog);
        printf("  EDC_COUNTER    : 0x%08X\n", io.EdcCounter);
        printf("  POWER_CNTL     : 0x%08X\n", io.PowerCntl);
        printf("  CNTL           : 0x%08X\n", io.Cntl);
        printf("  CHICKEN_BITS   : 0x%08X\n", io.ChickenBits);
        printf("  F32_CNTL / PC  : 0x%08X / 0x%04X\n", io.F32Cntl, io.F32Pc);
        printf("  SEM_WAIT_TIMER : 0x%08X\n", io.SemWaitFailTimer);
        printf("  UTCL1_CNTL     : 0x%08X\n", io.Utcl1Cntl);
        printf("  UTCL1_WATERMK  : 0x%08X\n", io.Utcl1Watermk);
        printf("  UTCL1_RD_STAT  : 0x%08X\n", io.Utcl1RdStatus);
        printf("  UTCL1_WR_STAT  : 0x%08X\n", io.Utcl1WrStatus);
        printf("  UTCL1_INV0/1/2 : 0x%08X 0x%08X 0x%08X\n",
               io.Utcl1Inv0, io.Utcl1Inv1, io.Utcl1Inv2);
        printf("  UTCL1_RD_XNACK : 0x%08X 0x%08X\n", io.Utcl1RdXnack0, io.Utcl1RdXnack1);
        printf("  UTCL1_WR_XNACK : 0x%08X 0x%08X\n", io.Utcl1WrXnack0, io.Utcl1WrXnack1);
        printf("  UTCL1_TIMEOUT  : 0x%08X\n", io.Utcl1Timeout);
        printf("  UTCL1_PAGE     : 0x%08X\n", io.Utcl1Page);
        printf("  GCVM FAULT     : cntl 0x%08X status 0x%08X addr 0x%08X%08X\n",
               io.GcvmFaultCntl, io.GcvmFaultStatus,
               io.GcvmFaultAddrHi, io.GcvmFaultAddrLo);
        printf("  ENGINE SUSPEITA: %s (motivo %u)\n",
               io.EngineSuspect ? "SIM" : "nao", io.SuspectReason);
        printf("  GRBM_STATUS    : 0x%08X\n", io.GrbmStatus);
        printf("  RB_CNTL        : 0x%08X\n", io.RbCntl);
        printf("  RPTR/WPTR      : 0x%08X / 0x%08X\n", io.Rptr, io.Wptr);
        printf("  RPTR_WB/SHADOW : 0x%08X / 0x%08X\n", io.RptrWriteback, io.WptrShadow);

        /* NAO ha heuristica de "suspeito" aqui, e isso e deliberado.
         *
         * A primeira versao deste comando marcava XNACK != 0 e ERROR_LOG != 0
         * como sinais de falha. O dump de referencia de 2026-08-23 01:05
         * derrubou isso: numa engine comprovadamente SAUDAVEL, logo apos uma
         * submissao correta, ERROR_LOG ja vale 0x0000000F e os XNACK ja valem
         * 0x0045FE00/0x01050500. Esses registradores carregam configuracao ou
         * contadores, nao um flag binario de erro.
         *
         * Interpretar bit sem semantica confirmada e o mesmo erro que o
         * projeto proibe em escrita de MMIO. O uso correto e comparar dois
         * dumps: um de referencia saudavel e um da engine quebrada. */
        xnack = io.Utcl1RdXnack0 | io.Utcl1RdXnack1 |
                io.Utcl1WrXnack0 | io.Utcl1WrXnack1;
        printf("\n  Nenhum bit acima e interpretado como falha por este comando.\n");
        printf("  Numa engine saudavel medida em 2026-08-23, ERROR_LOG=0x0000000F\n");
        printf("  e os XNACK ja sao diferentes de zero. Compare com um dump de\n");
        printf("  referencia; valor isolado nao diagnostica nada.\n");
        printf("  (uniao dos XNACK nesta leitura: 0x%08X)\n", xnack);
        return 0;
    }

    if (_stricmp(argv[2], "reset") == 0) {
        BC250DBG_SDMA_F32_RESET_IO out;
        int rc = Bc250SdmaF32Reset(&g_Ctx, &out);
        if (rc) { printf("falha no reset do F32: erro %d (rode 'arm write')\n", rc); return 1; }

        printf("Reset do microcontrolador F32 da SDMA0:\n");
        printf("  PC antes  : 0x%04X\n", out.PcBefore);
        printf("  PC depois : 0x%04X\n", out.PcAfter);
        printf("  transicoes: %u em 16 amostras\n", out.Moved);
        if (out.Moved == 0) {
            printf("\n  ** PC congelado: nao ha microcodigo executando.\n"
                   "     Carregue o firmware primeiro: bc250ctl psp loadfw sdma0\n");
            return 1;
        }
        printf("\n  ** Microcodigo executando.\n");
        return 0;
    }

    if (_stricmp(argv[2], "f32") == 0) {
        /* O F32 e o processador de comandos da SDMA. Contar transicoes do PC
         * NAO basta: um laco de 3 instrucoes colado no ponto de entrada gera
         * varias transicoes e parece "em movimento", mas e init travado.
         *
         * Medido nesta placa: firmware sadio cobre uma faixa larga
         * (0x0140..0x03E0); firmware travado oscila entre 0x000E e 0x0010.
         * Por isso reportamos FAIXA e enderecos DISTINTOS, nao transicoes. */
        enum { SAMPLES = 24 };
        uint32_t pc[SAMPLES], st2 = 0, f32cntl = 0, checksum = 0;
        uint32_t lo = 0x3FFF, hi = 0, distintos = 0;
        int i, j, novo;

        Bc250RegRead(&g_Ctx, 0x04A28, &f32cntl);
        Bc250RegRead(&g_Ctx, 0x04A24, &checksum);

        for (i = 0; i < SAMPLES; i++) {
            if (Bc250RegRead(&g_Ctx, 0x04A60, &st2)) {
                printf("falha ao ler SDMA0_STATUS2_REG\n");
                return 1;
            }
            pc[i] = (st2 >> 2) & 0x3FFF;
            if (pc[i] < lo) lo = pc[i];
            if (pc[i] > hi) hi = pc[i];
            novo = 1;
            for (j = 0; j < i; j++) if (pc[j] == pc[i]) { novo = 0; break; }
            if (novo) distintos++;
        }

        printf("Microcontrolador F32 da SDMA0:\n");
        printf("  F32_CNTL       : 0x%08X   HALT=%u\n", f32cntl, f32cntl & 1u);
        printf("  UCODE_CHECKSUM : 0x%08X\n", checksum);
        printf("  PC             :");
        for (i = 0; i < 12; i++) printf(" 0x%04X", pc[i]);
        printf("\n");
        printf("  faixa          : 0x%04X .. 0x%04X (amplitude 0x%04X)\n", lo, hi, hi - lo);
        printf("  enderecos dist.: %u de %u amostras\n\n", distintos, (uint32_t)SAMPLES);

        if (f32cntl & 1u) {
            printf("  ** F32 em HALT. Nada vai executar.\n");
            return 1;
        }
        if (distintos <= 1) {
            printf("  ** PC CONGELADO em 0x%04X: nao ha microcodigo executando.\n", pc[0]);
            return 1;
        }
        if (hi < 0x0080 && (hi - lo) < 0x0020) {
            printf("  ** PC PRESO NO INICIO (0x%04X..0x%04X, %u enderecos).\n"
                   "     Isso NAO e o laco principal — e o init do firmware travado,\n"
                   "     provavelmente esperando um registrador que nao esta como ele\n"
                   "     espera. Firmware sadio nesta placa cobre 0x0140..0x03E0.\n",
                   lo, hi, distintos);
            return 1;
        }
        if ((hi - lo) < 0x0080) {
            printf("  ** PC numa faixa estreita (0x%04X..0x%04X). Suspeito: pode ser\n"
                   "     um laco de espera em vez do laco principal.\n", lo, hi);
            return 1;
        }
        printf("  ** Faixa larga e %u enderecos distintos: laco principal rodando.\n", distintos);
        return 0;
    }

    if (_stricmp(argv[2], "status") == 0) {
        BC250DBG_SDMA_STATUS_IO st;
        int rc = Bc250SdmaStatus(&g_Ctx, &st);
        if (rc) { printf("falha ao ler status do SDMA: erro %d\n", rc); return 1; }

        printf("Estado do Motor SDMA0:\n");
        printf("  STATUS_REG    : 0x%08X\n", st.StatusReg);
        printf("  RB_CNTL       : 0x%08X (%s)\n", st.RbCntl,
               (st.RbCntl & 1) ? "HABILITADO" : "DESABILITADO");
        printf("  RB_BASE       : 0x%llX\n", (unsigned long long)st.RbBase);
        printf("  WPTR (reg)    : 0x%08X\n", st.Wptr);
        printf("  WPTR (shadow) : 0x%08X   <- valido para anel criado por este driver\n", st.WptrShadow);
        printf("  RPTR (reg)    : 0x%08X   (nao e fonte de verdade)\n", st.Rptr);
        printf("  RPTR (writeb.): 0x%08X   <- valido para anel criado por este driver\n", st.RptrWriteback);
        if (st.Wptr != 0 && st.WptrShadow == 0 && st.RptrWriteback == 0) {
            printf("  Pendente      : INDISPONIVEL (anel externo/POST; shadows nao mapeados)\n");
        } else {
            printf("  Pendente      : %d bytes\n", (int)(st.WptrShadow - st.RptrWriteback));
        }
        return 0;
    }

    if (_stricmp(argv[2], "raw") == 0) {
        /*
         * Injecao crua de dwords no anel da SDMA0.
         *
         * BcSdmaRaw ja existia no driver e era usada so por testes fixos
         * (srbmtest, parsetest). E o unico caminho para emitir um pacote
         * arbitrario -- INDIRECT com VMID != 0, por exemplo -- sem recompilar
         * kernel. E devolve F32Pc, o program counter do microcontrolador, que
         * o driver de producao nao expoe.
         */
        BC250DBG_SDMA_RAW_IO rawIo;
        int rawRc, k, nraw;

        if (argc < 4) {
            printf("uso: bc250ctl sdma raw <dw> [dw ...]\n");
            printf("  emite os dwords tal como estao no anel, sem montar pacote.\n");
            return 1;
        }

        nraw = argc - 3;
        if (nraw > BC250DBG_SDMA_RAW_MAX_DW) nraw = BC250DBG_SDMA_RAW_MAX_DW;

        memset(&rawIo, 0, sizeof(rawIo));
        rawIo.DwordCount = (uint32_t)nraw;
        for (k = 0; k < nraw; k++) {
            rawIo.Data[k] = (uint32_t)strtoul(argv[3 + k], NULL, 0);
        }

        printf("SDMA raw: %d dwords\n", nraw);
        for (k = 0; k < nraw; k++) {
            printf("  dw%02d = 0x%08X\n", k, rawIo.Data[k]);
        }

        rawRc = Bc250SdmaRaw(&g_Ctx, &rawIo);
        if (rawRc) {
            printf("submissao falhou: erro %d (anel iniciado? 'arm write'?)\n", rawRc);
            return 1;
        }

        printf("  RPTR 0x%X -> 0x%X   WPTR 0x%X   STATUS 0x%08X   PC 0x%04X\n",
               rawIo.RptrBefore, rawIo.RptrAfter, rawIo.Wptr,
               rawIo.StatusReg, rawIo.F32Pc);
        printf("  RESULTADO: %s\n",
               rawIo.Completed ? "RPTR alcancou WPTR"
                               : "*** RPTR NAO ALCANCOU WPTR ***");
        printf("  ATENCAO: RPTR alcancar WPTR NAO prova execucao. A prova e a\n");
        printf("           fence gravada pela GPU e os dados no destino.\n");
        return rawIo.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "nop") == 0) {
        uint32_t count = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 4;
        uint32_t dwords[64];
        uint32_t rptrBefore = 0, wptrAfter = 0, rptrAfter = 0;
        uint32_t i;
        int rc;

        if (count == 0 || count > 64) count = 4;
        for (i = 0; i < count; i++) {
            dwords[i] = 0x00000000; /* SDMA NOP packet */
        }

        printf("Submetendo %u NOPs ao anel SDMA0...\n", count);
        rc = Bc250SdmaSubmit(&g_Ctx, dwords, count, &rptrBefore, &wptrAfter, &rptrAfter);
        if (rc) {
            printf("submissao SDMA falhou: erro %d (rode 'bc250ctl arm write' antes)\n", rc);
            return 1;
        }
        printf("Resultado da Submissao SDMA0:\n");
        printf("  RPTR Inicial : 0x%X\n", rptrBefore);
        printf("  WPTR Final   : 0x%X (+%u bytes)\n", wptrAfter, count * 4);
        printf("  RPTR Final   : 0x%X\n", rptrAfter);
        if (rptrAfter == wptrAfter) {
            printf("  >>> SUCESSO! O hardware da GPU processou os comandos e avancou o RPTR!\n");
        } else {
            printf("  >>> AVISO: RPTR nao alcancou WPTR (pendente ou sem clock).\n");
        }
        return 0;
    }

    if (_stricmp(argv[2], "load") == 0) {
        const char *path = (argc >= 4) ? argv[3] : "firmware\\cyan_skillfish2_sdma.bin";
        FILE *f = fopen(path, "rb");
        uint8_t *buf;
        long sz;
        uint32_t *hdr;
        uint32_t ucodeOff, ucodeSz, dwordCount;
        uint32_t version = 0, checksum = 0;
        int rc;

        if (!f) {
            printf("nao consegui abrir '%s'\n", path);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);

        buf = (uint8_t *)malloc(sz);
        if (!buf) { fclose(f); return 1; }
        fread(buf, 1, sz, f);
        fclose(f);

        hdr = (uint32_t *)buf;
        ucodeSz  = hdr[5];
        ucodeOff = hdr[6];

        if (ucodeOff + ucodeSz > (uint32_t)sz) {
            printf("formato de firmware invalido em '%s'\n", path);
            free(buf);
            return 1;
        }

        dwordCount = ucodeSz / 4;
        printf("Carregando microcodigo SDMA (%u dwords / %u bytes) de '%s'...\n",
               dwordCount, ucodeSz, path);

        rc = Bc250SdmaLoadFirmware(&g_Ctx, (const uint32_t *)(buf + ucodeOff),
                                   dwordCount, &version, &checksum);
        free(buf);

        if (rc) {
            printf("falha ao carregar firmware SDMA: erro %d (rode 'bc250ctl arm write' antes)\n", rc);
            return 1;
        }

        printf("Firmware SDMA0 carregado com sucesso!\n");
        printf("  Versao / Ucode Addr   : 0x%08X\n", version);
        printf("  Checksum de Hardware  : 0x%08X\n", checksum);
        return 0;
    }

    if (_stricmp(argv[2], "bringup") == 0) {
        BC250DBG_PSP_RING_INIT_IO pspRing;
        BC250DBG_GCVM_INIT_IO gcvmIo;
        uint64_t sdmaGpu = 0;
        uint32_t sdmaRbCntl = 0;
        uint32_t rptr0 = 0, wptr1 = 0, rptr1 = 0;
        uint32_t nopData[8] = {0};
        uint32_t fwSt = 0, fwAddr = 0;
        uint32_t checksum = 0, st2 = 0;
        int rc;

        printf("========================================================\n");
        printf("  [FASE 3] Pipeline Unificado SDMA0 Bring-up            \n");
        printf("========================================================\n");

        Bc250Arm(&g_Ctx, BC250DBG_ARM_WRITE);

        /* 1. PSP Ring */
        rc = Bc250PspRingInit(&g_Ctx, &pspRing);
        if (rc) { printf("  [FALHA] PSP Ring Init (erro %d)\n", rc); return 1; }
        printf("  [1/5] PSP GPCOM Ring      : PA 0x%llX (TOS 0x80020000) [OK]\n",
               (unsigned long long)pspRing.RingPhysical);

        /* 2. Carga Firmware SDMA0 */
        rc = BcpLoadIpFwSilent(BC250_PSP_FW_SDMA0, "firmware\\cyan_skillfish2_sdma.bin", &fwSt, &fwAddr);
        if (rc || fwSt != 0) { printf("  [FALHA] Carga Firmware SDMA0 (status 0x%08X)\n", fwSt); return 1; }
        printf("  [2/5] Firmware SDMA0      : Carregado pelo PSP (RespStatus = 0x0) [OK]\n");

        /* 3. Microcontrolador F32 */
        Bc250RegRead(&g_Ctx, 0x04A24, &checksum);
        Bc250RegRead(&g_Ctx, 0x04A60, &st2);
        printf("  [3/5] Microcontrolador F32: PC 0x%04X, Checksum 0x%08X [OK]\n",
               (st2 >> 2) & 0x3FFF, checksum);

        /* 4. GCVM / Abertura de Sistema */
        memset(&gcvmIo, 0, sizeof(gcvmIo));
        Bc250GcvmInit(&g_Ctx, &gcvmIo);
        printf("  [4/5] GCVM / System Apert.: L2 0x%08X, Context0 0x%08X [OK]\n",
               gcvmIo.L2CntlAfter, gcvmIo.Context0CntlAfter);

        /* 5. Anel SDMA0 na VRAM e Sonda NOP */
        rc = Bc250SdmaInit(&g_Ctx, 1024, BC250DBG_SDMA_RING_VRAM, 0x0FF00000, &sdmaGpu, &sdmaRbCntl);
        if (rc) { printf("  [FALHA] SDMA Init (erro %d)\n", rc); return 1; }
        rc = Bc250SdmaSubmit(&g_Ctx, nopData, 8, &rptr0, &wptr1, &rptr1);
        if (rc || rptr1 != wptr1) {
            printf("  [FALHA] SDMA NOP Submit (RPTR 0x%X -> 0x%X)\n", rptr0, rptr1);
            return 1;
        }
        printf("  [5/5] SDMA Ring & Sonda   : RPTR 0x%X -> 0x%X (+32 bytes) [OK]\n", rptr0, rptr1);
        printf(">>> STATUS: SDMA0 100%% OPERACIONAL E VALIDADO!\n\n");
        return 0;
    }

    printf("subcomando sdma desconhecido: %s\n", argv[2]);
    return 1;
}

static int CmdFirmware(int argc, char **argv)
{
    const char *dir = (argc >= 3) ? argv[2] : "firmware";
    WIN32_FIND_DATAA fd;
    char path[MAX_PATH];
    HANDLE hFind;
    int count = 0;

    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\*.bin", dir);
    hFind = FindFirstFileA(path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("nenhum arquivo de firmware .bin encontrado em '%s'\n", dir);
        return 1;
    }

    printf("Blobs de Microcodigo encontrados em '%s':\n\n", dir);
    printf("%-28s %-10s %-10s %-10s %s\n", "BLOB", "TAMANHO", "VERSAO", "UCODE_SZ", "OFFSET");
    printf("----------------------------------------------------------------------\n");
    do {
        char filePath[MAX_PATH];
        FILE *f;
        _snprintf_s(filePath, sizeof(filePath), _TRUNCATE, "%s\\%s", dir, fd.cFileName);
        f = fopen(filePath, "rb");
        if (f) {
            uint32_t hdr[16];
            size_t read = fread(hdr, 1, sizeof(hdr), f);
            fclose(f);
            if (read >= 32) {
                uint32_t ucodeVer = hdr[4];
                uint32_t ucodeSz = hdr[5];
                uint32_t ucodeOff = hdr[6];
                printf("%-28s %8u B 0x%06X   %8u B 0x%04X\n",
                       fd.cFileName, fd.nFileSizeLow, ucodeVer, ucodeSz, ucodeOff);
                count++;
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    printf("----------------------------------------------------------------------\n");
    printf("Total: %d microcodigos validados.\n", count);
    return 0;
}

/* Instrumentacao do ramo direct load: PFP, ME, CE e MEC.
 *
 * O 0x80000203 medido em 2026-08-23 veio de uma sequencia PSP incompleta e
 * nao prova que esta rota seja obrigatoria. O Linux saudavel usa o LOAD_IP_FW
 * generico depois de TMR. Nada aqui tira engine nenhuma do HALT. */
static uint32_t CpEngineFromName(const char *s, const char **outFile)
{
    if (_stricmp(s, "pfp") == 0) {
        if (outFile) *outFile = "firmware/cyan_skillfish2_pfp.bin";
        return BC250DBG_CP_ENGINE_PFP;
    }
    if (_stricmp(s, "me") == 0) {
        if (outFile) *outFile = "firmware/cyan_skillfish2_me.bin";
        return BC250DBG_CP_ENGINE_ME;
    }
    if (_stricmp(s, "ce") == 0) {
        if (outFile) *outFile = "firmware/cyan_skillfish2_ce.bin";
        return BC250DBG_CP_ENGINE_CE;
    }
    if (_stricmp(s, "mec") == 0) {
        if (outFile) *outFile = "firmware/cyan_skillfish2_mec.bin";
        return BC250DBG_CP_ENGINE_MEC;
    }
    return 0xFFFFFFFFu;
}

/* gfx_firmware_header_v1_0: common_firmware_header de 0x20 bytes, depois
 * ucode_feature_version (+0x20), jt_offset (+0x24) e jt_size (+0x28), os dois
 * ultimos em DWORDS contados a partir do inicio do payload. */
typedef struct _CP_FW_INFO {
    uint32_t Version, UcodeSizeBytes, UcodeOffsetBytes;
    uint32_t FeatureVersion, JtOffsetDw, JtSizeDw;
    uint32_t HeaderSizeBytes, HdrMajor, HdrMinor, IpMajor, IpMinor, Crc32;
    uint8_t *Raw;
    long RawSize;
} CP_FW_INFO;

static int CpFwRead(const char *path, CP_FW_INFO *out)
{
    FILE *f;
    uint8_t *buf;
    long sz;
    uint32_t *h;

    memset(out, 0, sizeof(*out));
    f = fopen(path, "rb");
    if (!f) { printf("nao consegui abrir '%s'\n", path); return 1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0x2C) { printf("'%s' menor que um header v1_0\n", path); fclose(f); return 1; }
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return 1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        printf("leitura incompleta de '%s'\n", path); free(buf); fclose(f); return 1;
    }
    fclose(f);

    h = (uint32_t *)buf;
    out->HeaderSizeBytes  = h[1];
    out->HdrMajor = (uint16_t)(h[2] & 0xFFFF);
    out->HdrMinor = (uint16_t)(h[2] >> 16);
    out->IpMajor  = (uint16_t)(h[3] & 0xFFFF);
    out->IpMinor  = (uint16_t)(h[3] >> 16);
    out->Version          = h[4];
    out->UcodeSizeBytes   = h[5];
    out->UcodeOffsetBytes = h[6];
    out->Crc32            = h[7];
    out->FeatureVersion   = h[8];
    out->JtOffsetDw       = h[9];
    out->JtSizeDw         = h[10];
    out->Raw = buf;
    out->RawSize = sz;

    /* Conferencias que nao custam nada e evitam mandar lixo ao hardware. */
    if (out->HdrMajor != 1 || out->HdrMinor != 0) {
        printf("recusado: header v%u.%u, esperado v1.0\n", out->HdrMajor, out->HdrMinor);
        free(buf); return 1;
    }
    if (out->UcodeSizeBytes == 0 ||
        (uint64_t)out->UcodeOffsetBytes + out->UcodeSizeBytes > (uint64_t)sz) {
        printf("recusado: payload 0x%X + 0x%X passa do arquivo 0x%lX\n",
               out->UcodeOffsetBytes, out->UcodeSizeBytes, sz);
        free(buf); return 1;
    }
    if ((uint64_t)(out->JtOffsetDw + out->JtSizeDw) * 4 > out->UcodeSizeBytes) {
        printf("recusado: jump table (dw %u + %u) passa do payload de %u bytes\n",
               out->JtOffsetDw, out->JtSizeDw, out->UcodeSizeBytes);
        free(buf); return 1;
    }
    return 0;
}

static void CpPrintState(const BC250DBG_CP_UCODE_IO *io)
{
    printf("  CP_ME_CNTL         : 0x%08lX  [%s%s%s]\n", io->CpMeCntl,
           (io->CpMeCntl & 0x04000000u) ? "PFP_HALT " : "",
           (io->CpMeCntl & 0x10000000u) ? "ME_HALT "  : "",
           (io->CpMeCntl & 0x01000000u) ? "CE_HALT"   : "");
    printf("  CP_STAT / GRBM     : 0x%08lX / 0x%08lX  GRBM2 0x%08lX\n",
           io->CpStat, io->GrbmStatus, io->GrbmStatus2);
    printf("  INSTR_PNTR pfp/me/ce/mec1: 0x%08lX 0x%08lX 0x%08lX 0x%08lX\n",
           io->PfpInstrPntr, io->MeInstrPntr, io->CeInstrPntr, io->Mec1InstrPntr);
    printf("  JT_STAT pfp/ce/mec : 0x%08lX (JT_LOADED=%lu) / 0x%08lX (JT_LOADED=%lu) / 0x%08lX (JT_LOADED=0x%lX)\n",
           io->PfpJtStat, io->PfpJtStat & 3u,
           io->CeJtStat,  io->CeJtStat & 3u,
           io->MecJtStat, io->MecJtStat & 0xFFu);
}

static void CpPrintWrites(const BC250DBG_CP_UCODE_IO *io)
{
    unsigned long k;
    int algumWriteOnly = 0;

    for (k = 0; k < io->WriteCount && k < BC250DBG_CP_MAX_WRITES; k++) {
        const char *nota;

        /* Registrador write-only NAO pode ser declarado confirmado.
         *
         * Antes desta distincao, o driver preenchia WrAfter com o valor
         * QUERIDO sem ler o hardware, e a saida mostrava
         * "-> querido (queria querido)" — o que parecia readback e nao era.
         * Foi isso que produziu a anomalia das 16:19 e sustentou dias de
         * hipoteses sobre estado da GPU que nunca existiu. */
        if (io->WrWriteOnly[k]) {
            nota = "  [WRITE-ONLY: readback nao confirma]";
            algumWriteOnly = 1;
        } else if (io->WrAfter[k] != io->WrWanted[k]) {
            nota = "  DIVERGENCIA";
        } else {
            nota = "";
        }
        printf("    0x%05lX        : 0x%08lX -> 0x%08lX  (queria 0x%08lX)%s\n",
               io->WrOffset[k], io->WrBefore[k], io->WrAfter[k], io->WrWanted[k],
               nota);
    }
    if (algumWriteOnly) {
        printf("  ** Escrita write-only nao tem confirmacao possivel. A prova\n");
        printf("     destes passos e o ponteiro de instrucao da microengine\n");
        printf("     ANDAR depois do unhalt, variando entre amostras.\n");
    }
}

static int CmdCp(int argc, char **argv)
{
    BC250DBG_CP_UCODE_IO io;
    CP_FW_INFO fw;
    const char *defFile = NULL;
    uint32_t engine;
    int rc;

    if (argc < 3) {
        printf("uso: bc250ctl cp <subcomando>\n"
               "  cp info <pfp|me|ce> [arquivo]        le e confere o header do ucode\n"
               "  cp txn <snapshot|restore|verify>     transacao dos 25 registradores do CP\n"
               "  cp golden <preflight|apply>          preflight readonly; apply em QUARENTENA\n"
               "  cp stage <pfp|me|ce> <off> [arq]     copia o ucode para a VRAM e confere\n"
               "  cp rollback [pfp|me|ce|all]          devolve a regiao de VRAM\n"
               "  cp icinval <pfp|me|ce>               invalida a cache de instrucoes\n"
               "  cp clocks                            ungate do clock do CPF (fetcher)\n"
               "  cp pipereset <assert|deassert>       reset das pipes pelo CP_ME_CNTL\n"
               "  cp icinval-mecntl                    invalida I-cache pelo CP_ME_CNTL\n"
               "  cp icbase <pfp|me|ce> <off> [-va A] [-exedisable]\n"
               "                                       aponta IC_BASE para o ucode\n"
               "                                     (-va: endereco explicito, VMID 0)\n"
               "  cp jt <pfp|me|ce> <off> [arq]        escreve a jump table\n"
               "  cp status <pfp|me|ce>                le o estado, sem escrever nada\n"
               "  cp unhalt                            tira as 3 engines do HALT (CP_ME_CNTL=0)\n"
               "  cp halt                              volta as 3 engines para HALT (0x15000000)\n"
               "  cp sample                            amostra os ponteiros de instrucao e status\n");
        return 1;
    }

    /* 'cp info' nao fala com o driver: e leitura de arquivo. */
    if (_stricmp(argv[2], "info") == 0 && argc >= 4) {
        engine = CpEngineFromName(argv[3], &defFile);
        if (engine == 0xFFFFFFFFu) { printf("engine desconhecida: %s\n", argv[3]); return 1; }
        if (CpFwRead((argc >= 5) ? argv[4] : defFile, &fw)) return 1;

        printf("Header do ucode '%s':\n", (argc >= 5) ? argv[4] : defFile);
        printf("  header             : v%u.%u, %u bytes\n", fw.HdrMajor, fw.HdrMinor, fw.HeaderSizeBytes);
        printf("  IP                 : v%u.%u\n", fw.IpMajor, fw.IpMinor);
        printf("  ucode_version      : 0x%08X\n", fw.Version);
        printf("  feature_version    : 0x%08X\n", fw.FeatureVersion);
        printf("  payload            : %u bytes (%u dwords) no offset 0x%X\n",
               fw.UcodeSizeBytes, fw.UcodeSizeBytes / 4, fw.UcodeOffsetBytes);
        printf("  jump table         : dw %u .. %u (%u dwords, %u bytes)\n",
               fw.JtOffsetDw, fw.JtOffsetDw + fw.JtSizeDw, fw.JtSizeDw, fw.JtSizeDw * 4);
        printf("  crc32 declarado    : 0x%08X\n", fw.Crc32);
        printf("  RESULTADO          : header coerente, jump table dentro do payload\n");
        printf("  ** header coerente NAO diz nada sobre o CP funcionar.\n");
        free(fw.Raw);
        return 0;
    }

    memset(&io, 0, sizeof(io));

    if (_stricmp(argv[2], "txn") == 0 && argc >= 4) {
        const char *nome = argv[3];
        unsigned long k;

        if (_stricmp(nome, "snapshot") == 0)     io.Mode = BC250DBG_CP_MODE_TXN_SNAP;
        else if (_stricmp(nome, "restore") == 0) io.Mode = BC250DBG_CP_MODE_TXN_RESTORE;
        else if (_stricmp(nome, "verify") == 0)  io.Mode = BC250DBG_CP_MODE_TXN_VERIFY;
        else { printf("uso: bc250ctl cp txn <snapshot|restore|verify>\n"); return 1; }

        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha na transacao do CP: erro %d (rode 'arm write')\n", rc); return 1; }

        printf("Transacao do CP (%s):\n", nome);
        printf("  snapshot presente  : %s\n", io.SnapshotPresent ? "sim" : "nao");
        printf("  registradores      : %lu\n", io.RegisterCount);
        if (io.Mode == BC250DBG_CP_MODE_TXN_SNAP) {
            for (k = 0; k < io.RegisterCount && k < BC250DBG_CP_TXN_REG_COUNT; k++) {
                printf("    0x%05lX        : 0x%08lX\n", io.Offsets[k], io.Values[k]);
            }
        } else {
            if (io.Mode == BC250DBG_CP_MODE_TXN_RESTORE) {
                printf("  restaurados        : %lu\n", io.Restored);
            }
            printf("  divergencias       : %lu\n", io.Mismatches);
            for (k = 0; k < io.Mismatches && k < BC250DBG_CP_TXN_BAD_SAMPLES; k++) {
                printf("    0x%05lX        : leu 0x%08lX, esperava 0x%08lX\n",
                       io.BadOffset[k], io.BadGot[k], io.BadWant[k]);
            }
        }
        CpPrintState(&io);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            if (io.Status == BC250DBG_OPS_BADOP) {
                printf("    rode 'cp txn snapshot' antes.\n");
            }
            return 1;
        }
        printf("  RESULTADO          : ok\n");
        return 0;
    }

    if (_stricmp(argv[2], "status") == 0 && argc >= 4) {
        engine = CpEngineFromName(argv[3], NULL);
        if (engine == 0xFFFFFFFFu) { printf("engine desconhecida: %s\n", argv[3]); return 1; }
        io.Mode = BC250DBG_CP_MODE_TXN_VERIFY;   /* leitura; se nao houver snapshot, so reporta */
        io.Engine = engine;
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha ao ler estado do CP: erro %d\n", rc); return 1; }
        printf("Estado do CP (engine %s):\n", argv[3]);
        printf("  IC_BASE            : 0x%08lX%08lX\n", io.RegBaseHi, io.RegBaseLo);
        printf("  IC_BASE_CNTL       : 0x%08lX  (VMID %lu, ADDRESS_CLAMP %lu)\n",
               io.RegBaseCntl, io.RegBaseCntl & 0xFu, (io.RegBaseCntl >> 4) & 1u);
        printf("  IC_OP_CNTL         : 0x%08lX  (COMPLETE %lu)\n",
               io.RegOpCntl, (io.RegOpCntl >> 1) & 1u);
        printf("  portas UCODE       : ADDR 0x%05lX  DATA 0x%05lX\n",
               io.RegUcodeAddr, io.RegUcodeData);
        CpPrintState(&io);
        return 0;
    }

    if (_stricmp(argv[2], "rollback") == 0) {
        io.Mode = BC250DBG_CP_MODE_ROLLBACK;
        if (argc >= 4 && _stricmp(argv[3], "all") != 0) {
            engine = CpEngineFromName(argv[3], NULL);
            io.Engine = (engine != 0xFFFFFFFFu) ? engine : 0xFFFFFFFFu;
        } else {
            io.Engine = 0xFFFFFFFFu;
        }
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha no rollback do ucode: erro %d\n", rc); return 1; }
        printf("Rollback do ucode do CP:\n");
        printf("  snapshot presente  : %s\n", io.SnapshotPresent ? "sim" : "nao");
        printf("  dwords devolvidos  : %lu, %lu divergencias\n",
               io.SnapshotDwords, io.Mismatches);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            return 1;
        }
        printf("  RESULTADO          : ok, regiao de VRAM devolvida\n");
        return 0;
    }

    if (_stricmp(argv[2], "unhalt") == 0) {
        io.Mode = BC250DBG_CP_MODE_UNHALT;
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha no unhalt: erro %d (rode 'arm write')\n", rc); return 1; }
        printf("Unhalt das microengines do CP (CP_ME_CNTL -> 0):\n");
        printf("  CP_ME_CNTL antes   : 0x%08lX\n", io.WrBefore[0]);
        printf("  CP_ME_CNTL depois  : 0x%08lX\n", io.WrAfter[0]);
        CpPrintState(&io);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            if (io.Status == BC250DBG_OPS_BADOP) {
                printf("    RECUSADO: rode 'cp txn snapshot' antes.\n");
            }
            return 1;
        }
        printf("  RESULTADO          : ok, microengines unhaltadas\n");
        return 0;
    }

    if (_stricmp(argv[2], "halt") == 0) {
        io.Mode = BC250DBG_CP_MODE_HALT;
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha no halt: erro %d (rode 'arm write')\n", rc); return 1; }
        printf("Halt das microengines do CP (CP_ME_CNTL -> 0x15000000):\n");
        printf("  CP_ME_CNTL antes   : 0x%08lX\n", io.WrBefore[0]);
        printf("  CP_ME_CNTL depois  : 0x%08lX\n", io.WrAfter[0]);
        CpPrintState(&io);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            return 1;
        }
        printf("  RESULTADO          : ok, microengines haltadas\n");
        return 0;
    }

    if (_stricmp(argv[2], "sample") == 0) {
        io.Mode = BC250DBG_CP_MODE_SAMPLE;
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha ao amostrar CP: erro %d\n", rc); return 1; }
        printf("Amostra do estado das microengines do CP:\n");
        CpPrintState(&io);
        return 0;
    }

    if (_stricmp(argv[2], "icinval") == 0 && argc >= 4) {
        engine = CpEngineFromName(argv[3], NULL);
        if (engine == 0xFFFFFFFFu) { printf("engine desconhecida: %s\n", argv[3]); return 1; }
        io.Mode = BC250DBG_CP_MODE_ICINVAL;
        io.Engine = engine;
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha na invalidacao: erro %d (rode 'arm write')\n", rc); return 1; }

        printf("Invalidacao da cache de instrucoes (%s):\n", argv[3]);
        printf("  IC_OP_CNTL         : 0x%08lX -> 0x%08lX -> 0x%08lX\n",
               io.OpCntlBefore, io.OpCntlAfterWrite, io.OpCntlAfterWait);
        printf("  espera             : %lu iteracoes de 1 us\n", io.PollIterations);
        CpPrintState(&io);
        if (!io.InvConfirmavel) {
            /* IC_OP_CNTL le 0xFFFFFFFF, e 0xFFFFFFFF satisfaz QUALQUER mascara
             * de bit — inclusive INVALIDATE_CACHE_COMPLETE, que e o bit 1. Duas
             * sessoes ja anunciaram "o hardware confirmou por COMPLETE" sem uma
             * unica leitura valida por causa disso.
             *
             * Nao se recusa a invalidacao: o amdgpu faz a mesma escrita e o CP
             * funciona la. O que nao se faz e chamar isto de confirmado. */
            printf("  RESULTADO          : invalidacao PEDIDA, NAO CONFIRMAVEL\n");
            printf("    IC_OP_CNTL e write-only e le 0xFFFFFFFF, valor que\n");
            printf("    satisfaz o bit COMPLETE sozinho. Ler isso como confirmacao\n");
            printf("    seria ler o proprio 0xFFFFFFFF. Este passo nao prova nada;\n");
            printf("    a prova fica toda no ponteiro de instrucao apos o unhalt.\n");
            return 0;
        }
        if (io.TimedOut) {
            printf("  RESULTADO          : TIMEOUT, INVALIDATE_CACHE_COMPLETE nunca acendeu\n");
            printf("    A invalidacao foi pedida e nao confirmada. Nao siga para o\n");
            printf("    icbase: a cache pode ter conteudo velho.\n");
            return 1;
        }
        printf("  RESULTADO          : ok, o hardware confirmou por COMPLETE\n");
        return 0;
    }

    if (_stricmp(argv[2], "pipereset") == 0 && argc >= 4) {
        io.Mode = BC250DBG_CP_MODE_PIPE_RESET;
        io.PipeResetAssert = (_stricmp(argv[3], "assert") == 0) ? 1u : 0u;
        if (_stricmp(argv[3], "assert") != 0 && _stricmp(argv[3], "deassert") != 0) {
            printf("uso: bc250ctl cp pipereset <assert|deassert>\n");
            return 1;
        }
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha no pipereset: erro %d (rode 'arm write')\n", rc); return 1; }

        printf("Reset das pipes do CP (%s), pelo CP_ME_CNTL:\n", argv[3]);
        printf("  escritas           : %lu\n", io.WriteCount);
        CpPrintWrites(&io);
        if (io.Status == BC250DBG_OPS_OK) {
            printf("  RESULTADO          : ok, CONFIRMADO POR READBACK\n");
            printf("  ** Os HALT sao mantidos nos dois sentidos: isto mexe nas pipes,\n");
            printf("     nao tira engine do HALT.\n");
        } else {
            printf("  RESULTADO          : NAO confirmado (status %lu)\n", io.Status);
        }
        CpPrintState(&io);
        return (io.Status == BC250DBG_OPS_OK) ? 0 : 1;
    }

    if (_stricmp(argv[2], "icinval-mecntl") == 0) {
        io.Mode = BC250DBG_CP_MODE_ICACHE_MECNTL;
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha no icinval-mecntl: erro %d (rode 'arm write')\n", rc); return 1; }

        printf("Invalidacao da I-cache pelo CP_ME_CNTL:\n");
        printf("  antes              : 0x%08lX\n", io.OpCntlBefore);
        printf("  logo apos a escrita: 0x%08lX\n", io.OpCntlAfterWrite);
        printf("  devolvido          : 0x%08lX\n", io.OpCntlAfterWait);
        if (io.InvConfirmavel) {
            printf("  bits lidos de volta: SIM\n");
        } else {
            printf("  bits lidos de volta: NAO\n");
            printf("  ** Auto-limpante ou nao aceito. Estas duas coisas NAO se\n");
            printf("     distinguem por readback, e nenhuma delas e falha.\n");
        }
        CpPrintState(&io);
        return (io.Status == BC250DBG_OPS_OK) ? 0 : 1;
    }

    if (_stricmp(argv[2], "clocks") == 0) {
        io.Mode = BC250DBG_CP_MODE_CLOCKS;
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha no clocks: erro %d (rode 'arm write')\n", rc); return 1; }

        printf("Ungate do clock do CPF (Command Processor Fetcher):\n");
        printf("  escritas           : %lu\n", io.WriteCount);
        CpPrintWrites(&io);
        if (io.Status == BC250DBG_OPS_OK) {
            printf("  RESULTADO          : ok, CGTT_CPF_CLK_CTRL = 0xF8000100\n");
            printf("  ** Este registrador RESPONDE: o readback acima e confirmacao\n");
            printf("     de verdade, nao 'emitido sem recusa'.\n");
        } else {
            printf("  RESULTADO          : NAO confirmado (status %lu)\n", io.Status);
        }
        CpPrintState(&io);
        return (io.Status == BC250DBG_OPS_OK) ? 0 : 1;
    }

    if (_stricmp(argv[2], "golden") == 0) {
        unsigned long k;
        const char *acao;

        if (argc < 4) {
            printf("uso: bc250ctl cp golden <preflight|apply>\n");
            return 1;
        }
        acao = argv[3];
        if (_stricmp(acao, "preflight") == 0) {
            io.Mode = BC250DBG_CP_MODE_GOLDEN_PREFLIGHT;
        } else if (_stricmp(acao, "apply") == 0) {
            io.Mode = BC250DBG_CP_MODE_GOLDEN_INIT;
        } else {
            printf("uso: bc250ctl cp golden <preflight|apply>\n");
            return 1;
        }
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha no golden %s: erro %d\n", acao, rc); return 1; }

        printf("Golden Cyan Skillfish (%s):\n", acao);
        printf("  registradores      : %lu\n", io.RegisterCount);
        printf("  escritas           : %lu\n", io.WriteCount);
        if (io.Mode == BC250DBG_CP_MODE_GOLDEN_PREFLIGHT) {
            for (k = 0; k < io.RegisterCount && k < BC250DBG_CP_REPORT_REG_COUNT; k++) {
                printf("    0x%05lX        : 0x%08lX\n", io.Offsets[k], io.Values[k]);
            }
            printf("  falhas             : %lu\n", io.Mismatches);
            for (k = 0; k < io.Mismatches && k < BC250DBG_CP_TXN_BAD_SAMPLES; k++) {
                printf("    0x%05lX        : leituras 0x%08lX / 0x%08lX\n",
                       io.BadOffset[k], io.BadWant[k], io.BadGot[k]);
            }
        } else {
            CpPrintWrites(&io);
        }
        if (io.Status == BC250DBG_OPS_OK) {
            printf("  RESULTADO          : preflight readonly aprovado; nenhuma escrita emitida\n");
        } else {
            printf("  RESULTADO          : RECUSADO/DIVERGENCIA (status %lu)\n", io.Status);
            if (io.Mode == BC250DBG_CP_MODE_GOLDEN_INIT) {
                printf("    apply segue em QUARENTENA: falta snapshot/rollback por SE/SH/instancia.\n");
            }
        }
        CpPrintState(&io);
        return (io.Status == BC250DBG_OPS_OK) ? 0 : 1;
    }

    if (_stricmp(argv[2], "icbase") == 0 && argc >= 5) {
        engine = CpEngineFromName(argv[3], NULL);
        if (engine == 0xFFFFFFFFu) { printf("engine desconhecida: %s\n", argv[3]); return 1; }
        io.Mode = BC250DBG_CP_MODE_ICBASE;
        io.Engine = engine;
        io.VramOffset = (uint32_t)strtoul(argv[4], NULL, 0);
        /* ABI 21: -va aponta o IC_BASE para um endereco explicito. O caso de
         * uso e uma VA que a page table do VMID 0 resolve, como o amdgpu faz
         * com endereco de GTT. Sem -va, nada muda. */
        for (int kva = 5; kva < argc; kva++) {
            if ((_stricmp(argv[kva], "-va") == 0 || _stricmp(argv[kva], "-gpu") == 0) && kva + 1 < argc) {
                io.OverrideGpuAddr = strtoull(argv[++kva], NULL, 0);
                io.OverrideValid = 1;
            } else if (_stricmp(argv[kva], "-exedisable") == 0) {
                /* ABI 23: teste diferencial. Ver o comentario no ABI. */
                io.ExeDisable = 1;
            }
        }
        rc = Bc250CpUcode(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha no icbase: erro %d (rode 'arm write')\n", rc); return 1; }

        printf("IC_BASE da engine %s:\n", argv[3]);
        if (io.ExeDisable) {
            printf("  EXE_DISABLE        : 1  (TESTE DIFERENCIAL, nao e a configuracao normal)\n");
        }
        if (io.OverrideValid) {
            printf("  ucode em           : VRAM 0x%08lX\n", io.VramOffset);
            printf("  IC_BASE aponta para: VA 0x%llX  (VMID 0, pela page table)\n",
                   (unsigned long long)io.OverrideGpuAddr);
            printf("  ** A VA so resolve se CONTEXT0 estiver ligado e a pagina mapeada.\n");
        } else {
            printf("  ucode em           : VRAM 0x%08lX  ->  GPU 0x%llX\n",
                   io.VramOffset, (unsigned long long)io.GpuAddr);
        }
        printf("  escritas           : %lu\n", io.WriteCount);
        CpPrintWrites(&io);
        CpPrintState(&io);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            if (io.Status == BC250DBG_OPS_BADOP) {
                printf("    RECUSADO: rode 'cp txn snapshot' antes. O driver nao escreve\n");
                printf("    em registrador do CP sem caminho de volta provado.\n");
            }
            return 1;
        }
        printf("  RESULTADO          : ok, IC_BASE apontado para o ucode\n");
        printf("  ** \"ok\" aqui significa que as escritas foram emitidas sem\n");
        printf("     recusa, NAO que o hardware as confirmou: IC_BASE_* e\n");
        printf("     write-only e le 0xFFFFFFFF tambem no Linux, onde o CP\n");
        printf("     funciona. Ninguem busca ainda: as engines seguem em HALT.\n");
        return 0;
    }

    if ((_stricmp(argv[2], "stage") == 0 || _stricmp(argv[2], "jt") == 0) && argc >= 5) {
        int isJt = (_stricmp(argv[2], "jt") == 0);
        const uint32_t *payload;
        uint32_t count;

        engine = CpEngineFromName(argv[3], &defFile);
        if (engine == 0xFFFFFFFFu) { printf("engine desconhecida: %s\n", argv[3]); return 1; }
        if (CpFwRead((argc >= 6) ? argv[5] : defFile, &fw)) return 1;

        io.Engine = engine;
        io.VramOffset = (uint32_t)strtoul(argv[4], NULL, 0);
        io.UcodeVersion = fw.Version;

        if (isJt) {
            io.Mode = BC250DBG_CP_MODE_JT;
            payload = (const uint32_t *)(fw.Raw + fw.UcodeOffsetBytes) + fw.JtOffsetDw;
            count = fw.JtSizeDw;
        } else {
            io.Mode = BC250DBG_CP_MODE_STAGE;
            payload = (const uint32_t *)(fw.Raw + fw.UcodeOffsetBytes);
            count = fw.UcodeSizeBytes / 4;
        }
        io.DwordCount = count;

        rc = Bc250CpUcode(&g_Ctx, &io, payload, count);
        free(fw.Raw);
        if (rc) {
            printf("falha no %s: erro %d (rode 'arm write')\n", isJt ? "jt" : "stage", rc);
            return 1;
        }

        if (isJt) {
            printf("Jump table da engine %s:\n", argv[3]);
            printf("  dwords escritos    : %lu pela porta DATA\n", io.DwordsWritten);
            printf("  versao no ADDR     : 0x%08lX  (ADDR releu 0x%08lX)\n",
                   io.UcodeVersion, io.WrAfter[1]);
            printf("  ** a porta DATA escreve na RAM interna da microengine: nao ha\n");
            printf("     readback possivel dela. O que se confere e o ADDR.\n");
        } else {
            printf("Ucode da engine %s na VRAM:\n", argv[3]);
            printf("  destino            : VRAM 0x%08lX  ->  GPU 0x%llX\n",
                   io.VramOffset, (unsigned long long)io.GpuAddr);
            printf("  snapshot           : %s, %lu dwords\n",
                   io.SnapshotPresent ? "guardado" : "AUSENTE", io.SnapshotDwords);
            printf("  escritos           : %lu dwords, %lu divergencias\n",
                   io.DwordsWritten, io.Mismatches);
            if (io.Mismatches) {
                printf("    primeira em dw %lu: leu 0x%08lX, queria 0x%08lX\n",
                       io.FirstMismatchIndex, io.FirstMismatchGot, io.FirstMismatchWant);
            }
        }
        CpPrintState(&io);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            return 1;
        }
        printf("  RESULTADO          : ok\n");
        return 0;
    }

    printf("subcomando desconhecido. Rode 'bc250ctl cp' para a lista.\n");
    return 1;
}

/* Bring-up do RLC.
 *
 * Medido em 2026-08-23 23:07, contra a referencia do Linux com o CP executando:
 * RLC_GPM_STAT.RLC_EXEC_ROM_CODE e 1 aqui e 0 la. O RLC deste lado esta
 * executando o bootloader interno, sem microcodigo.
 *
 * O gate de todo este caminho e esse bit cair para 0. Escrita aceita nao e
 * execucao — este projeto ja pagou por confundir os dois. */
static void RlcPrintState(const BC250DBG_RLC_IO *io)
{
    printf("  RLC_CNTL           : 0x%08lX  [ENABLE_F32 %lu]\n", io->Cntl, io->EnableF32);
    printf("  RLC_GPM_STAT       : 0x%08lX  [EXEC_ROM_CODE %lu, RLC_BUSY %lu]\n",
           io->GpmStat, io->ExecRomCode, io->RlcBusy);
    printf("  RLC_CGCG_CGLS_CTRL : 0x%08lX   (Linux com CP vivo: 0x00000000)\n", io->CgcgCglsCntl);
    printf("  RLC_PG_CNTL        : 0x%08lX   (Linux: 0x00000000)\n", io->PgCntl);
    printf("  RLC_SAFE_MODE      : 0x%08lX\n", io->SafeMode);
    printf("  RLC_SRM_CNTL       : 0x%08lX\n", io->SrmCntl);
    printf("  CGTT_CPF_CLK_CTRL  : 0x%08lX   (Linux: 0xF8000100, cinco SOFT_OVERRIDE)\n",
           io->CgttCpfClkCtrl);
    printf("  GRBM_STATUS/2      : 0x%08lX / 0x%08lX\n", io->GrbmStatus, io->GrbmStatus2);
}

static void RlcPrintWrites(const BC250DBG_RLC_IO *io)
{
    unsigned long k;
    for (k = 0; k < io->WriteCount && k < BC250DBG_RLC_WRITES; k++) {
        printf("    0x%05lX        : 0x%08lX -> 0x%08lX  (queria 0x%08lX)%s\n",
               io->WrOffset[k], io->WrBefore[k], io->WrAfter[k], io->WrWanted[k],
               (io->WrAfter[k] == io->WrWanted[k]) ? "" : "  DIVERGENCIA");
    }
}

static int CmdRlc(int argc, char **argv)
{
    BC250DBG_RLC_IO io;
    int rc;
    unsigned long k;

    if (argc < 3) {
        printf("uso: bc250ctl rlc <subcomando>\n"
               "  rlc status                       le o estado, nao escreve nada\n"
               "  rlc txn <snapshot|restore|verify> transacao dos 4 registradores\n"
               "  rlc stop                         RLC_ENABLE_F32 = 0\n"
               "  rlc cgcg                         RLC_CGCG_CGLS_CTRL = 0\n"
               "  rlc pg                           RLC_PG_CNTL = 0\n"
               "  rlc load [arquivo.bin]           ucode pelas portas ADDR/DATA\n"
               "  rlc start                        RLC_ENABLE_F32 = 1\n"
               "  rlc srm                          RLC_SRM_CNTL, save/restore machine\n"
               "\n"
               "O GATE e RLC_GPM_STAT.RLC_EXEC_ROM_CODE cair de 1 para 0. Escrita\n"
               "aceita NAO e execucao.\n");
        return 1;
    }

    memset(&io, 0, sizeof(io));

    if (_stricmp(argv[2], "status") == 0) {
        io.Mode = BC250DBG_RLC_MODE_STATUS;
        rc = Bc250Rlc(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha ao ler estado do RLC: erro %d\n", rc); return 1; }
        printf("Estado do RLC:\n");
        RlcPrintState(&io);
        if (io.ExecRomCode) {
            printf("  ** EXEC_ROM_CODE = 1: o RLC esta rodando o bootloader interno,\n");
            printf("     sem microcodigo. E o analogo do F32 da SDMA em HALT.\n");
        } else {
            printf("  ** EXEC_ROM_CODE = 0: o RLC saiu da ROM.\n");
        }
        return 0;
    }

    if (_stricmp(argv[2], "txn") == 0 && argc >= 4) {
        const char *nome = argv[3];
        if (_stricmp(nome, "snapshot") == 0)     io.Mode = BC250DBG_RLC_MODE_TXN_SNAP;
        else if (_stricmp(nome, "restore") == 0) io.Mode = BC250DBG_RLC_MODE_TXN_RESTORE;
        else if (_stricmp(nome, "verify") == 0)  io.Mode = BC250DBG_RLC_MODE_TXN_VERIFY;
        else { printf("uso: bc250ctl rlc txn <snapshot|restore|verify>\n"); return 1; }

        rc = Bc250Rlc(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha na transacao do RLC: erro %d\n", rc); return 1; }
        printf("Transacao do RLC (%s):\n", nome);
        printf("  snapshot presente  : %s\n", io.SnapshotPresent ? "sim" : "nao");
        printf("  registradores      : %lu\n", io.RegisterCount);
        if (io.Mode == BC250DBG_RLC_MODE_TXN_SNAP) {
            for (k = 0; k < io.RegisterCount && k < BC250DBG_RLC_TXN_REG_COUNT; k++) {
                printf("    0x%05lX        : 0x%08lX\n", io.Offsets[k], io.Values[k]);
            }
        } else {
            if (io.Mode == BC250DBG_RLC_MODE_TXN_RESTORE) {
                printf("  restaurados        : %lu\n", io.Restored);
            }
            printf("  divergencias       : %lu\n", io.Mismatches);
            for (k = 0; k < io.Mismatches && k < BC250DBG_RLC_TXN_BAD_SAMPLES; k++) {
                printf("    0x%05lX        : leu 0x%08lX, esperava 0x%08lX\n",
                       io.BadOffset[k], io.BadGot[k], io.BadWant[k]);
            }
        }
        RlcPrintState(&io);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            if (io.Status == BC250DBG_OPS_BADOP) printf("    rode 'rlc txn snapshot' antes.\n");
            return 1;
        }
        printf("  RESULTADO          : ok\n");
        return 0;
    }

    if (_stricmp(argv[2], "load") == 0) {
        /* rlc_firmware_header_v2_0: o payload sai de ucode_array_offset_bytes com
         * ucode_size_bytes, igual ao que o PSP recebe. */
        const char *path = (argc >= 4) ? argv[3] : "firmware/cyan_skillfish2_rlc.bin";
        FILE *f;
        uint8_t *buf;
        long sz;
        uint32_t *h, ver, usz, uoff, count;

        f = fopen(path, "rb");
        if (!f) { printf("nao consegui abrir '%s'\n", path); return 1; }
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz < 0x20) { printf("'%s' menor que um header\n", path); fclose(f); return 1; }
        buf = (uint8_t *)malloc((size_t)sz);
        if (!buf) { fclose(f); return 1; }
        if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            printf("leitura incompleta de '%s'\n", path); free(buf); fclose(f); return 1;
        }
        fclose(f);
        h = (uint32_t *)buf;
        ver = h[4]; usz = h[5]; uoff = h[6];
        if (usz == 0 || (uint64_t)uoff + usz > (uint64_t)sz || (usz % 4) != 0) {
            printf("recusado: payload 0x%X + 0x%X fora do arquivo 0x%lX\n", uoff, usz, sz);
            free(buf); return 1;
        }
        count = usz / 4;
        printf("Ucode do RLC de '%s':\n", path);
        printf("  versao             : 0x%08X\n", ver);
        printf("  payload            : %u dwords (%u bytes) no offset 0x%X\n", count, usz, uoff);

        io.Mode = BC250DBG_RLC_MODE_LOAD_UCODE;
        io.UcodeVersion = ver;
        io.DwordCount = count;
        rc = Bc250Rlc(&g_Ctx, &io, (const uint32_t *)(buf + uoff), count);
        free(buf);
        if (rc) { printf("falha no load do RLC: erro %d (rode 'arm write')\n", rc); return 1; }

        printf("  dwords escritos    : %lu a partir de 0x%X\n",
               io.DwordsWritten, BC250DBG_RLC_UCODE_START_ADDR);
        printf("  ADDR relido        : 0x%08lX\n", io.AddrReadback);
        printf("  ** ADDR e DATA sao portas de escrita: leem 0xFFFFFFFF nos dois\n");
        printf("     sistemas. O ADDR relido e indicio, nao prova.\n");
        RlcPrintState(&io);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            return 1;
        }
        printf("  RESULTADO          : payload entregue\n");
        return 0;
    }

    {
        const char *rotulo = NULL;
        if (_stricmp(argv[2], "stop") == 0)  { io.Mode = BC250DBG_RLC_MODE_STOP;      rotulo = "stop (ENABLE_F32 = 0)"; }
        else if (_stricmp(argv[2], "cgcg") == 0) { io.Mode = BC250DBG_RLC_MODE_CGCG_OFF; rotulo = "CGCG_CGLS_CTRL = 0"; }
        else if (_stricmp(argv[2], "pg") == 0)   { io.Mode = BC250DBG_RLC_MODE_PG_OFF;   rotulo = "PG_CNTL = 0"; }
        else if (_stricmp(argv[2], "start") == 0){ io.Mode = BC250DBG_RLC_MODE_START;    rotulo = "start (ENABLE_F32 = 1)"; }
        else if (_stricmp(argv[2], "srm") == 0)  { io.Mode = BC250DBG_RLC_MODE_SRM_ENABLE; rotulo = "SRM_CNTL"; }
        else { printf("subcomando desconhecido. Rode 'bc250ctl rlc' para a lista.\n"); return 1; }

        rc = Bc250Rlc(&g_Ctx, &io, NULL, 0);
        if (rc) { printf("falha em '%s': erro %d (rode 'arm write')\n", argv[2], rc); return 1; }

        printf("RLC %s:\n", rotulo);
        printf("  escritas           : %lu, %lu divergencias\n", io.WriteCount, io.Mismatches);
        RlcPrintWrites(&io);
        RlcPrintState(&io);
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO          : FALHOU (status %lu)\n", io.Status);
            if (io.Status == BC250DBG_OPS_BADOP) {
                printf("    RECUSADO: rode 'rlc txn snapshot' antes. Sem caminho de\n");
                printf("    volta provado o driver nao escreve em registrador do RLC.\n");
            }
            return 1;
        }
        if (io.Mode == BC250DBG_RLC_MODE_START) {
            if (io.ExecRomCode == 0) {
                printf("  RESULTADO          : ok, e EXEC_ROM_CODE caiu para 0\n");
                printf("  ** O RLC SAIU DA ROM. Esta e a prova que o gate pedia.\n");
            } else {
                printf("  RESULTADO          : escrita aceita, mas EXEC_ROM_CODE continua 1\n");
                printf("  ** O RLC NAO saiu da ROM. Escrita aceita nao e execucao:\n");
                printf("     falta ucode valido, ou falta um passo antes do start.\n");
                return 1;
            }
        } else {
            printf("  RESULTADO          : ok, confirmado por readback\n");
        }
        return 0;
    }
}

static int CmdGcvm(int argc, char **argv)
{
    if (argc >= 3 && _stricmp(argv[2], "semrelease") == 0) {
        /* Metade que faltava do protocolo do semaforo de invalidacao. No
         * upstream, ler o registrador adquire e escrever 0 libera. Enquanto o
         * dump lia SEM, o laboratorio adquiria o semaforo sem saber e o mantinha
         * ate o reboot. */
        BC250DBG_GCVM_SEM_IO io;
        int rc = Bc250GcvmSemaphoreRelease(&g_Ctx, &io);

        if (rc) {
            printf("falha ao liberar o semaforo: erro %d (precisa de 'arm write')\n", rc);
            return 1;
        }
        printf("Semaforo da engine de invalidacao (GFXHUB, engine 0):\n");
        printf("  ACK antes  : 0x%08X  retido: %s\n",
               io.AckBefore, io.HeldBefore ? "SIM" : "nao");
        printf("  ACK depois : 0x%08X  retido: %s\n",
               io.AckAfter, io.HeldAfter ? "SIM" : "nao");
        if (io.HeldBefore && !io.HeldAfter) {
            printf("  RESULTADO  : semaforo liberado.\n");
        } else if (!io.HeldBefore) {
            printf("  RESULTADO  : nao estava retido; nada a liberar.\n");
        } else {
            printf("  RESULTADO  : continua retido apos escrever 0. Investigar.\n");
            return 1;
        }
        return 0;
    }

    if (argc >= 4 && _stricmp(argv[2], "txn") == 0) {
        /* Passo 2 do gate de memoria: provar o caminho de volta ANTES de existir
         * divergencia para desfazer. O 'restore' reescreve exatamente os valores
         * lidos; se escrever valor identico tivesse efeito colateral em algum
         * destes registradores, e melhor descobrir agora, com o bypass intacto. */
        BC250DBG_GCVM_TXN_IO io;
        int rc;
        uint32_t k;
        const char *nome = argv[3];

        memset(&io, 0, sizeof(io));
        if (_stricmp(nome, "snapshot") == 0)      io.Mode = BC250DBG_GCVM_TXN_SNAPSHOT;
        else if (_stricmp(nome, "restore") == 0)  io.Mode = BC250DBG_GCVM_TXN_RESTORE;
        else if (_stricmp(nome, "verify") == 0)   io.Mode = BC250DBG_GCVM_TXN_VERIFY;
        else {
            printf("uso: bc250ctl gcvm txn <snapshot|restore|verify>\n");
            return 1;
        }

        rc = Bc250GcvmTransaction(&g_Ctx, &io);

        printf("Transacao GCVM (%s):\n", nome);
        printf("  snapshot presente : %s\n", io.SnapshotPresent ? "sim" : "nao");
        printf("  registradores     : %u\n", io.RegisterCount);
        if (io.Mode == BC250DBG_GCVM_TXN_SNAPSHOT) {
            for (k = 0; k < io.RegisterCount && k < BC250DBG_GCVM_TXN_REG_COUNT; k++) {
                printf("    0x%05X        : 0x%08X\n", io.Offsets[k], io.Values[k]);
            }
        } else {
            if (io.Mode == BC250DBG_GCVM_TXN_RESTORE) {
                printf("  restaurados       : %u\n", io.Restored);
            }
            printf("  divergencias      : %u\n", io.Mismatches);
            for (k = 0; k < io.BadCount; k++) {
                printf("    0x%05X        : leu 0x%08X, esperava 0x%08X\n",
                       io.BadOffset[k], io.BadGot[k], io.BadWant[k]);
            }
        }
        if (rc || io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO         : FALHOU (erro %d, status %u)\n", rc, io.Status);
            if (io.Status == BC250DBG_OPS_DENIED) {
                printf("    'restore' precisa de 'arm write'.\n");
            }
            if (io.Status == BC250DBG_OPS_BADOP) {
                printf("    rode 'gcvm txn snapshot' antes.\n");
            }
            return 1;
        }
        printf("  RESULTADO         : ok\n");
        return 0;
    }

    if (argc >= 3 && _stricmp(argv[2], "invalidate") == 0) {
        /* Passo 4 do gate de memoria. A sequencia vem de
         * gmc_v10_0_flush_gpu_tlb(), caminho anterior a KIQ/MES, que e o caso
         * deste laboratorio: engine 17 (a da GART), sem semaforo (o GFXHUB nao
         * usa), com a leitura descartada de REQ que este chip exige. */
        BC250DBG_GCVM_INVALIDATE_IO io;
        int rc;

        memset(&io, 0, sizeof(io));
        io.Vmid      = (argc >= 4) ? (unsigned long)strtoul(argv[3], NULL, 0) : 0u;
        io.FlushType = (argc >= 5) ? (unsigned long)strtoul(argv[4], NULL, 0) : 0u;

        rc = Bc250GcvmInvalidate(&g_Ctx, &io);
        if (rc) {
            printf("falha na invalidacao: erro %d (rode 'arm write' antes)\n", rc);
            return 1;
        }

        printf("Invalidacao de TLB do GFXHUB:\n");
        printf("  engine            : %lu  (a da GART no upstream)\n", io.Engine);
        printf("  vmid / flush type : %lu / %lu\n", io.Vmid, io.FlushType);
        printf("  REQ escrito       : 0x%08lX\n", io.ReqValue);
        printf("  REQ relido        : 0x%08lX  (leitura obrigatoria neste chip)\n",
               io.ReqReadback);
        printf("  ACK               : 0x%08lX -> 0x%08lX  (mascara 0x%08lX)\n",
               io.AckBefore, io.AckAfter, io.AckExpectedMask);
        printf("  espera            : %lu iteracoes de 1 us\n", io.PollIterations);
        printf("  FAULT_STATUS      : 0x%08lX\n", io.FaultStatus);
        printf("  FAULT_ADDR        : 0x%08lX%08lX\n", io.FaultAddrHi, io.FaultAddrLo);

        if (io.Status == BC250DBG_OPS_TIMEOUT) {
            printf("  RESULTADO         : TIMEOUT, o ACK nunca acendeu o bit do vmid\n");
            printf("    A invalidacao foi pedida e nao foi confirmada. Nao trate\n");
            printf("    isso como sucesso: a TLB pode ter conteudo velho.\n");
            return 1;
        }
        if (io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO         : FALHOU (status %lu)\n", io.Status);
            return 1;
        }
        printf("  RESULTADO         : ok, ACK confirmado pelo hardware\n");
        printf("  ** ACK confirma que a invalidacao foi ACEITA. Nao prova que a\n");
        printf("     traducao funciona: isso e a submissao do passo 5.\n");
        return 0;
    }

    if (argc >= 4 && _stricmp(argv[2], "program") == 0) {
        /* Passo 3 do gate de memoria: programar valores novos nos registradores
         * de traducao, um estagio por vez, cada escrita com readback conferido.
         *
         * A ordem e os valores saem de gfxhub_v2_0.c. Os estagios tlb, cache e
         * context0 substituem o bypass do POST, do qual todo o sucesso medido
         * depende, e por isso exigem -danger explicito. O driver recusa
         * qualquer estagio sem snapshot de transacao aberto: sem caminho de
         * volta provado, nao ha escrita. */
        BC250DBG_GCVM_PROGRAM_IO io;
        int rc, k;
        const char *nome = argv[3];
        const char *rotulo = NULL;

        memset(&io, 0, sizeof(io));
        /* Defaults iguais aos do ensaio do passo 1, para nao existirem dois
         * conjuntos de endereco no projeto. */
        io.TableVramOffset = 0x0FF10000u;
        io.DummyVramOffset = 0x0FF20000u;
        io.PtStartVa       = 0x0000000000000000ull;
        io.PtEndVa         = 0x00000000001FFFFFull;   /* 512 paginas de 4 KB */

        if      (_stricmp(nome, "gart") == 0)     io.Stage = BC250DBG_GCVM_PROG_STAGE_GART;
        else if (_stricmp(nome, "system") == 0)   io.Stage = BC250DBG_GCVM_PROG_STAGE_SYSTEM;
        else if (_stricmp(nome, "tlb") == 0)      io.Stage = BC250DBG_GCVM_PROG_STAGE_TLB;
        else if (_stricmp(nome, "cache") == 0)    io.Stage = BC250DBG_GCVM_PROG_STAGE_CACHE;
        else if (_stricmp(nome, "context0") == 0) io.Stage = BC250DBG_GCVM_PROG_STAGE_CONTEXT0;
        else if (_stricmp(nome, "identity") == 0) io.Stage = BC250DBG_GCVM_PROG_STAGE_IDENTITY;
        else if (_stricmp(nome, "invrange") == 0) io.Stage = BC250DBG_GCVM_PROG_STAGE_INVRANGE;
        else {
            printf("uso: bc250ctl gcvm program <estagio> [opcoes]\n");
            printf("  estagios seguros  : gart system identity invrange\n");
            printf("  estagios de risco : tlb cache context0   (exigem -danger)\n");
            printf("  opcoes: -table <off> -dummy <off> -start <va> -end <va> -danger\n");
            return 1;
        }

        for (k = 4; k < argc; k++) {
            if (_stricmp(argv[k], "-danger") == 0) {
                io.Flags |= BC250DBG_GCVM_PROG_FLAG_DANGER;
            } else if (_stricmp(argv[k], "-table") == 0 && k + 1 < argc) {
                io.TableVramOffset = (unsigned long)strtoul(argv[++k], NULL, 0);
            } else if (_stricmp(argv[k], "-dummy") == 0 && k + 1 < argc) {
                io.DummyVramOffset = (unsigned long)strtoul(argv[++k], NULL, 0);
            } else if (_stricmp(argv[k], "-start") == 0 && k + 1 < argc) {
                io.PtStartVa = strtoull(argv[++k], NULL, 0);
            } else if (_stricmp(argv[k], "-end") == 0 && k + 1 < argc) {
                io.PtEndVa = strtoull(argv[++k], NULL, 0);
            } else {
                printf("argumento desconhecido: %s\n", argv[k]);
                return 1;
            }
        }

        if (io.Stage == BC250DBG_GCVM_PROG_STAGE_TLB)      rotulo = "PERIGOSO";
        if (io.Stage == BC250DBG_GCVM_PROG_STAGE_CACHE)    rotulo = "PERIGOSO";
        if (io.Stage == BC250DBG_GCVM_PROG_STAGE_CONTEXT0) rotulo = "PONTO SEM VOLTA";

        rc = Bc250GcvmProgram(&g_Ctx, &io);

        printf("Programacao GCVM, estagio '%s'%s%s%s:\n", nome,
               rotulo ? "  [" : "", rotulo ? rotulo : "", rotulo ? "]" : "");
        printf("  FB_OFFSET         : 0x%08lX  -> vram_base_offset 0x%llX\n",
               io.FbOffsetReg, (unsigned long long)io.VramBaseOffset);
        printf("  FB_LOCATION       : base 0x%08lX  top 0x%08lX\n",
               io.FbLocationBase, io.FbLocationTop);
        if (io.Stage == BC250DBG_GCVM_PROG_STAGE_GART) {
            printf("  tabela            : offset 0x%08lX  PA 0x%llX\n",
                   io.TableVramOffset, (unsigned long long)io.PtBasePa);
            printf("  faixa de VA       : 0x%llX .. 0x%llX\n",
                   (unsigned long long)io.PtStartVa, (unsigned long long)io.PtEndVa);
        }
        if (io.Stage == BC250DBG_GCVM_PROG_STAGE_SYSTEM) {
            printf("  pagina de falha   : offset 0x%08lX  PA 0x%llX\n",
                   io.DummyVramOffset, (unsigned long long)io.DummyPa);
        }
        printf("  escritas          : %lu, %lu divergencias\n",
               io.WriteCount, io.Mismatches);
        for (k = 0; k < (int)io.WriteCount && k < (int)BC250DBG_GCVM_PROG_MAX_WRITES; k++) {
            unsigned long keep = ~io.IgnoredBits[k];
            int ok = ((io.After[k] & keep) == (io.Wanted[k] & keep));

            printf("    0x%05lX        : 0x%08lX -> 0x%08lX  (queria 0x%08lX)%s\n",
                   io.Offset[k], io.Before[k], io.After[k], io.Wanted[k],
                   ok ? "" : "  DIVERGENCIA");
            if (io.IgnoredBits[k]) {
                /* Dito em voz alta, sempre. Excecao silenciosa a regra de
                 * readback seria pior que nao ter a regra. */
                printf("                       bits 0x%08lX fora da conferencia: "
                       "sao gatilhos, o hardware os limpa depois de agir\n",
                       io.IgnoredBits[k]);
            }
        }

        if (rc || io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO         : FALHOU (erro %d, status %lu)\n", rc, io.Status);
            switch (io.DenyReason) {
            case BC250DBG_GCVM_PROG_DENY_NO_TXN:
                printf("    RECUSADO: nenhum snapshot de transacao aberto.\n");
                printf("    Rode 'gcvm txn snapshot' primeiro. O driver nao escreve em\n");
                printf("    registrador de traducao sem caminho de volta provado.\n");
                break;
            case BC250DBG_GCVM_PROG_DENY_UNCOVERED:
                printf("    RECUSADO: 0x%05lX nao esta na lista da transacao;\n",
                       io.DenyOffset);
                printf("    o rollback nao o desfaria. Nenhuma escrita foi feita.\n");
                break;
            case BC250DBG_GCVM_PROG_DENY_NEED_FLAG:
                printf("    RECUSADO: este estagio substitui o bypass do POST.\n");
                printf("    Repita com -danger se essa e a intencao.\n");
                break;
            case BC250DBG_GCVM_PROG_DENY_BAD_STAGE:
                printf("    RECUSADO: estagio invalido.\n");
                break;
            case BC250DBG_GCVM_PROG_DENY_BAD_ARG:
                printf("    RECUSADO: endereco fora da janela ou desalinhado.\n");
                printf("    Veja o log do driver: ele imprime os numeros exatos.\n");
                break;
            default:
                if (io.Mismatches) {
                    printf("    O readback nao confirmou uma escrita; o estagio abortou\n");
                    printf("    naquele registrador. Rode 'gcvm txn restore'.\n");
                }
                break;
            }
            return 1;
        }
        printf("  RESULTADO         : ok, todas as escritas confirmadas por readback\n");
        return 0;
    }

    if (argc >= 3 && (_stricmp(argv[2], "ptbuild") == 0 ||
                      _stricmp(argv[2], "ptrollback") == 0)) {
        /* Passo 1 do gate de memoria. Escreve PTEs pela CPU e confere tudo por
         * leitura da CPU; nenhum registrador de traducao e tocado, entao a placa
         * fica no mesmo estado mesmo que a tabela saia errada.
         *
         * O PA de cada pagina sai de amdgpu_gmc_vram_mc2pa(), com o
         * vram_base_offset lido do registrador GCMC_VM_FB_OFFSET pelo driver. */
        BC250DBG_GCVM_PT_IO io;
        int rollback = (_stricmp(argv[2], "ptrollback") == 0);
        int rc;
        uint32_t k;

        memset(&io, 0, sizeof(io));
        io.Mode = rollback ? BC250DBG_GCVM_PT_MODE_ROLLBACK
                           : BC250DBG_GCVM_PT_MODE_BUILD;
        if (!rollback) {
            /* Default do alvo em 0x0FD00000: 512 paginas de 4 KB cobrem 2 MB e
             * terminam exatamente em 0x0FF00000, onde comeca o anel da SDMA.
             * O default anterior (0x0FF20000) estourava a abertura de 256 MB —
             * recusado corretamente pelo driver em 2026-08-23 09:42. */
            io.TableVramOffset  = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0x0FF10000u;
            io.TargetVramOffset = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 0x0FD00000u;
            io.PageCount        = (argc >= 6) ? (uint32_t)strtoul(argv[5], NULL, 0) : 0;

            {
                /* Conferencia antecipada, so para dar mensagem util: o driver
                 * mantem a validacao autoritativa. Payload de saida nao volta
                 * em caminho de falha, entao sem isto o usuario ve apenas
                 * "erro 87". */
                uint32_t paginas = io.PageCount ? io.PageCount : BC250DBG_GCVM_PT_MAX_PAGES;
                unsigned long long fimTabela = (unsigned long long)io.TableVramOffset + paginas * 8ull;
                unsigned long long fimAlvo = (unsigned long long)io.TargetVramOffset + paginas * 4096ull;

                if (paginas > BC250DBG_GCVM_PT_MAX_PAGES) {
                    printf("recusado: %u paginas pedidas, maximo %u\n",
                           paginas, BC250DBG_GCVM_PT_MAX_PAGES);
                    return 1;
                }
                if ((io.TableVramOffset & 0xFFFu) || (io.TargetVramOffset & 0xFFFu)) {
                    printf("recusado: tabela e alvo precisam de alinhamento de 4 KB\n");
                    return 1;
                }
                if (fimTabela > BC250_VRAM_TOTAL_BYTES) {
                    printf("recusado: tabela 0x%08X + %u bytes termina em 0x%llX, "
                           "passa da janela de 0x%X\n",
                           io.TableVramOffset, paginas * 8u, fimTabela,
                           BC250_VRAM_TOTAL_BYTES);
                    return 1;
                }
                if (fimAlvo > BC250_VRAM_TOTAL_BYTES) {
                    printf("recusado: alvo 0x%08X + %u paginas termina em 0x%llX, "
                           "passa da janela de 0x%X\n",
                           io.TargetVramOffset, paginas, fimAlvo,
                           BC250_VRAM_TOTAL_BYTES);
                    printf("  caberiam %llu paginas a partir desse alvo.\n",
                           (BC250_VRAM_TOTAL_BYTES - (unsigned long long)io.TargetVramOffset) / 4096ull);
                    return 1;
                }
            }
        }

        rc = Bc250GcvmPageTable(&g_Ctx, &io);

        if (rollback) {
            printf("Rollback da page table de laboratorio:\n");
            printf("  snapshot presente : %s\n", io.SnapshotPresent ? "sim" : "nao");
            printf("  regiao            : 0x%08X, %u dwords\n",
                   io.TableVramOffset, io.SnapshotDwords);
            printf("  divergencias      : %u\n", io.RestoreMismatches);
            if (rc || io.Status != BC250DBG_OPS_OK) {
                printf("  RESULTADO         : FALHOU (erro %d, status %u)\n", rc, io.Status);
                return 1;
            }
            printf("  RESULTADO         : conteudo original devolvido\n");
            return 0;
        }

        if (rc && io.Status == BC250DBG_OPS_DENIED) {
            printf("recusado: precisa de 'arm write' (erro %d)\n", rc);
            return 1;
        }
        printf("Page table de laboratorio na VRAM:\n");
        printf("  tabela            : 0x%08X (%u PTEs, %u bytes)\n",
               io.TableVramOffset, io.EntriesWritten, io.EntriesWritten * 8u);
        printf("  paginas alvo      : VRAM 0x%08X em diante\n", io.TargetVramOffset);
        printf("  GCMC_VM_FB_OFFSET : 0x%08X  -> vram_base_offset 0x%llX\n",
               io.FbOffsetReg, (unsigned long long)io.VramBaseOffset);
        printf("  PA coberto        : 0x%llX .. 0x%llX\n",
               (unsigned long long)io.FirstPa, (unsigned long long)io.LastPa);
        printf("  flags do PTE      : 0x%llX (VALID|EXEC|READ|WRITE|MTYPE_UC)\n",
               (unsigned long long)io.PteFlags);
        printf("  snapshot          : %s, %u dwords\n",
               io.SnapshotPresent ? "guardado" : "AUSENTE", io.SnapshotDwords);
        for (k = 0; k < BC250DBG_GCVM_PT_SAMPLES && k < io.EntriesWritten; k++) {
            printf("    PTE[%u]          : 0x%016llX\n", k,
                   (unsigned long long)io.PteSample[k]);
        }
        printf("  conferencia       : %u divergencias em %u entradas\n",
               io.Mismatches, io.EntriesWritten);
        if (io.Mismatches) {
            printf("    primeira em [%u]: leu 0x%016llX, esperava 0x%016llX\n",
                   io.FirstMismatchIndex,
                   (unsigned long long)io.FirstMismatchGot,
                   (unsigned long long)io.FirstMismatchWant);
        }
        if (rc || io.Status != BC250DBG_OPS_OK) {
            printf("  RESULTADO         : FALHOU (erro %d, status %u)\n", rc, io.Status);
            return 1;
        }
        printf("  RESULTADO         : tabela escrita e conferida dword a dword.\n");
        printf("  NENHUM registrador de traducao foi tocado.\n");
        return 0;
    }

    if (argc >= 3 && _stricmp(argv[2], "dump") == 0) {
        /* Estado de traducao do GC, somente leitura.
         *
         * Serve ao gate de memoria: antes de montar VMID 0 e preciso saber o
         * que o POST deixou. A comparacao com o que o upstream exige e feita
         * por tools/decode-gcvm.py, que decodifica bit a bit; aqui os valores
         * saem crus, sem interpretacao. */
        BC250DBG_GCVM_DUMP_IO io;
        int rc = Bc250GcvmDump(&g_Ctx, &io);

        if (rc) { printf("falha no dump GCVM: erro %d\n", rc); return 1; }

        printf("Estado de traducao do GC (somente leitura):\n");
        printf("  L2_CNTL              : 0x%08X\n", io.L2Cntl);
        printf("  L2_CNTL2             : 0x%08X\n", io.L2Cntl2);
        printf("  L2_CNTL3             : 0x%08X\n", io.L2Cntl3);
        printf("  L2_CNTL4             : 0x%08X\n", io.L2Cntl4);
        printf("  L2_CNTL5             : 0x%08X\n", io.L2Cntl5);
        printf("  FAULT_CNTL           : 0x%08X\n", io.FaultCntl);
        printf("  FAULT_CNTL2          : 0x%08X\n", io.FaultCntl2);
        printf("  FAULT_STATUS         : 0x%08X\n", io.FaultStatus);
        printf("  FAULT_ADDR           : 0x%08X%08X\n", io.FaultAddrHi, io.FaultAddrLo);
        printf("  FAULT_DEFAULT_ADDR   : 0x%08X%08X\n",
               io.FaultDefaultAddrHi, io.FaultDefaultAddrLo);
        printf("  CONTEXT0_CNTL        : 0x%08X\n", io.Context0Cntl);
        printf("  CONTEXT1_CNTL        : 0x%08X\n", io.Context1Cntl);
        printf("  CTX0_PT_BASE         : 0x%08X%08X\n", io.Ctx0PtBaseHi, io.Ctx0PtBaseLo);
        printf("  CTX0_PT_START        : 0x%08X%08X\n", io.Ctx0PtStartHi, io.Ctx0PtStartLo);
        printf("  CTX0_PT_END          : 0x%08X%08X\n", io.Ctx0PtEndHi, io.Ctx0PtEndLo);
        printf("  MX_L1_TLB_CNTL       : 0x%08X\n", io.MxL1TlbCntl);
        printf("  SYS_APERTURE_LOW     : 0x%08X\n", io.SysApertureLow);
        printf("  SYS_APERTURE_HIGH    : 0x%08X\n", io.SysApertureHigh);
        printf("  SYS_APERTURE_DEFAULT : 0x%08X%08X\n",
               io.SysApertureDefaultMsb, io.SysApertureDefaultLsb);
        printf("  AGP_BASE             : 0x%08X\n", io.AgpBase);
        printf("  AGP_BOT              : 0x%08X\n", io.AgpBot);
        printf("  AGP_TOP              : 0x%08X\n", io.AgpTop);
        printf("  FB_LOCATION_BASE     : 0x%08X\n", io.FbLocationBase);
        printf("  FB_LOCATION_TOP      : 0x%08X\n", io.FbLocationTop);
        printf("  FB_OFFSET            : 0x%08X\n", io.FbOffset);
        printf("  IDENTITY_APER_LOW    : 0x%08X\n", io.IdentityApertureLowLo);
        printf("  IDENTITY_APER_HIGH   : 0x%08X\n", io.IdentityApertureHighLo);
        printf("  INVALIDATE_ENG0_REQ  : 0x%08X\n", io.InvalidateEng0Req);
        printf("  INVALIDATE_ENG0_ACK  : 0x%08X\n", io.InvalidateEng0Ack);
        printf("  SEMAFORO retido      : %s (bit SEMAPHORE do ACK)\n",
               (io.InvalidateEng0Ack & 0x00010000u) ? "SIM" : "nao");
        printf("    GCVM_INVALIDATE_ENG0_SEM nao e lido de proposito: ler esse\n");
        printf("    registrador ADQUIRE o semaforo. Use 'gcvm semrelease' para\n");
        printf("    liberar, se algum ensaio antigo tiver adquirido.\n");
        printf("\n  Nenhum bit e interpretado aqui. Use:\n");
        printf("    python tools\\decode-gcvm.py <arquivo com esta saida>\n");
        return 0;
    }

    if (argc < 3 || _stricmp(argv[2], "status") == 0) {
        BC250DBG_GCVM_STATUS_IO st;
        int rc = Bc250GcvmStatus(&g_Ctx, &st);
        if (rc) {
            printf("Falha ao obter status do GCVM (erro %d)\n", rc);
            return rc;
        }
        printf("Estado da MMU / GCVM (Contexto 0 / GART):\n");
        printf("  L2_CNTL           : 0x%08X  [L2_CACHE=%d DEFAULT_OUT_SYS=%d ID_ACCESS=%d]\n",
               st.L2Cntl,
               (st.L2Cntl & 1) ? 1 : 0,
               (st.L2Cntl & 0x800) ? 1 : 0,
               (st.L2Cntl & 0x80000) ? 1 : 0);
        printf("  L2_CNTL2          : 0x%08X\n", st.L2Cntl2);
        printf("  CONTEXT0_CNTL     : 0x%08X  [%s, DEPTH=%u]\n",
               st.Context0Cntl,
               (st.Context0Cntl & 1)
                   ? "LIGADO"
                   : "desligado (a abertura de sistema resolve os operandos)",
               (st.Context0Cntl >> 1) & 3);
        if ((st.Context0Cntl & 1) && st.Context0PtBaseLo == 0 && st.Context0PtBaseHi == 0) {
            printf("  ** LIGADO com PT_BASE=0: operandos de endereco vindos de pacote\n"
                   "     falham a traducao em silencio. Rode 'gcvm init' para desligar.\n");
        }
        printf("  CONTEXT0_PT_BASE  : 0x%08X%08X\n", st.Context0PtBaseHi, st.Context0PtBaseLo);
        printf("  CONTEXT0_START    : 0x%08X\n", st.Context0StartLo);
        printf("  CONTEXT0_END      : 0x%08X\n", st.Context0EndLo);
        printf("  SYSTEM_APERTURE   : 0x%08X -> 0x%08X  (GPU 0x%llX -> 0x%llX)\n",
               st.SysApertureLow, st.SysApertureHigh,
               ((uint64_t)st.SysApertureLow) << 18,
               ((uint64_t)st.SysApertureHigh) << 18);
        printf("  SYS_APERTURE_DEF  : 0x%08X%08X\n", st.SysApertureDefHi, st.SysApertureDefLo);
        printf("  L2_PROT_FAULT_STAT: 0x%08X\n", st.L2ProtFaultStatus);
        return 0;
    }

    if (_stricmp(argv[2], "init") == 0) {
        BC250DBG_GCVM_INIT_IO io;
        int rc;
        printf("Inicializando GCVM / Contexto 0 (GART/System Domain)...\n");
        rc = Bc250GcvmInit(&g_Ctx, &io);
        if (rc) {
            printf("Falha ao inicializar GCVM (erro %d). Rode 'arm write'.\n", rc);
            return rc;
        }
        /* O sucesso agora e o contexto DESLIGADO. Ligar ENABLE_CONTEXT sem
         * page table faz todo operando de endereco vindo de pacote falhar a
         * traducao em silencio — medido nesta placa. Ver secao 2.2 dos docs. */
        printf("  CONTEXT0_CNTL : 0x%08X -> 0x%08X (%s)\n",
               io.Context0CntlBefore, io.Context0CntlAfter,
               (io.Context0CntlAfter & 1) ? "AINDA LIGADO — nao deveria"
                                          : "desligado, como esperado");
        printf("  L2_CNTL       : 0x%08X\n", io.L2CntlAfter);
        if (io.Context0CntlAfter & 1) {
            printf("  Status        : FALHOU — com o contexto ligado e PT_BASE=0,\n"
                   "                  escritas de pacote nao chegam na memoria.\n");
            return 1;
        }
        printf("  Status        : OK (a abertura de sistema resolve os operandos)\n");
        return 0;
    }

    printf("Uso: bc250ctl gcvm <status|init>\n");
    return 1;
}

static void GfxPrintSubmitStatus(const BC250DBG_GFX_SUBMIT_IO *io)
{
    printf("  RPTR Inicial : 0x%X\n"
           "  WPTR Final   : 0x%X\n"
           "  RPTR Final   : 0x%X\n"
           "  Conclusao    : %s\n"
           "  CP_ME_CNTL   : 0x%08X (MEC_CNTL 0x%08X)  CP_STAT 0x%08X\n"
           "  INSTR_PNTR   : PFP=0x%08X  ME=0x%08X  CE=0x%08X  MEC1=0x%08X\n"
           "  JT_STAT      : PFP=0x%08X (JT_LOADED=%u)  CE=0x%08X (JT_LOADED=%u)  MEC=0x%08X (JT_LOADED=0x%X)\n",
           io->RptrBefore, io->Wptr, io->RptrAfter,
           io->Completed ? ">>> SUCESSO! O CP processou os pacotes PM4 e avancou o RPTR!"
                        : "TIMEOUT (RPTR nao alcancou WPTR)",
           io->CpMeCntl, io->CpMecCntl, io->CpStat,
           io->PfpInstrPntr, io->MeInstrPntr, io->CeInstrPntr, io->Mec1InstrPntr,
           io->PfpJtStat, io->PfpJtStat & 3,
           io->CeJtStat, io->CeJtStat & 3,
           io->MecJtStat, io->MecJtStat & 0xFF);
}

static int CmdGfx(int argc, char **argv)
{
    if (argc < 3) {
        printf("uso: bc250ctl gfx <init|nop|status> [args...]\n");
        return 1;
    }

    if (_stricmp(argv[2], "init") == 0) {
        BC250DBG_GFX_INIT_IO io;
        int rc, a;

        memset(&io, 0, sizeof(io));
        io.VramOffset = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0x0E200000u;
        io.RingSizeBytes = (argc >= 5 && argv[4][0] != '-') ? (uint32_t)strtoul(argv[4], NULL, 0) : 4096u;
        for (a = 3; a < argc; a++) {
            if (_stricmp(argv[a], "-gpu") == 0 && a + 1 < argc) {
                io.GpuAddr = strtoull(argv[++a], NULL, 0);
            }
        }
        rc = Bc250GfxInit(&g_Ctx, &io);
        if (rc) {
            printf("falha ao inicializar anel GFX CP: erro %d\n", rc);
            return 1;
        }

        printf("GFX CP Ring 0 (PM4) inicializado:\n"
               "  VRAM offset  : 0x%08X (GPU 0x%llX)\n"
               "  Tamanho      : %u bytes\n"
               "  CP_RB0_CNTL  : 0x%08X\n"
               "  CP_ME_CNTL   : 0x%08X -> 0x%08X (HALT=%s)\n",
               io.VramOffset, (unsigned long long)io.GpuAddr,
               io.RingSizeBytes, io.RbCntl,
               io.MeCntlBefore, io.MeCntlAfter,
               (io.MeCntlAfter & 0x15000000) ? "SIM (travado)" : "NAO (livre)");
        return 0;
    }

    if (_stricmp(argv[2], "status") == 0) {
        BC250DBG_GFX_STATUS_IO io;
        int rc = Bc250GfxStatus(&g_Ctx, &io);
        if (rc) {
            printf("falha ao ler status GFX: erro %d\n", rc);
            return 1;
        }

        printf("Status GFX CP Ring 0:\n"
               "  CP_ME_CNTL   : 0x%08X (HALT: ME=%u PFP=%u CE=%u)\n"
               "  CP_RB0_CNTL  : 0x%08X\n"
               "  CP_RB0_BASE  : 0x%016llX\n"
               "  CP_RB0_WPTR  : 0x%08X\n"
               "  CP_RB0_RPTR  : 0x%08X (reg) | 0x%08X (writeback)\n"
               "  GRBM_STATUS  : 0x%08X (GUI_ACTIVE=%u)\n",
               io.MeCntl,
               (io.MeCntl >> 28) & 1, (io.MeCntl >> 26) & 1, (io.MeCntl >> 24) & 1,
               io.Rb0Cntl, (unsigned long long)io.Rb0Base,
               io.Rb0Wptr, io.Rb0Rptr, io.Rb0RptrWb,
               io.GrbmStatus, (io.GrbmStatus >> 31) & 1);
        return 0;
    }

    if (_stricmp(argv[2], "nop") == 0) {
        uint32_t count = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 4;
        BC250DBG_GFX_SUBMIT_IO io;
        uint32_t i;
        int rc;

        if (count == 0 || count > 64) count = 4;

        memset(&io, 0, sizeof(io));
        /* PACKET3 COUNT codifica (numero de dwords de payload - 1).
         * Como count inclui o header, o campo e count - 2. */
        if (count < 2) count = 2;
        io.Data[0] = ((3u) << 30) | (0x10u << 8) | (((count - 2) & 0x3FFFu) << 16);
        for (i = 1; i < count; i++) {
            io.Data[i] = 0x5EED0000u | i;
        }
        io.DwordCount = count;

        printf("Submetendo %u dwords ao anel GFX CP (Header PM4: 0x%08X)...\n",
               count, io.Data[0]);

        rc = Bc250GfxSubmit(&g_Ctx, &io);
        if (rc) {
            printf("falha ao submeter PM4 ao anel GFX: erro %d\n", rc);
            return 1;
        }

        printf("Resultado da Submissao GFX CP:\n");
        GfxPrintSubmitStatus(&io);
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "probe") == 0) {
        BC250DBG_GFX_SUBMIT_IO io;
        int rc;

        printf("Enviando sequencia PM4 de boot: PREAMBLE_BEGIN + CONTEXT_CONTROL + PREAMBLE_END + CLEAR_STATE + NOP (13 dwords)...\n");
        memset(&io, 0, sizeof(io));
        /* 1. PACKET3_PREAMBLE_CNTL: BEGIN_CLEAR_STATE (Opcode 0x4A, count=0 -> 2 dw) */
        io.Data[0] = ((3u << 30) | (0u << 16) | (0x4Au << 8)); /* 0xC0004A00 */
        io.Data[1] = 0x00000000u;                               /* BEGIN_CLEAR_STATE */
        /* 2. PACKET3_CONTEXT_CONTROL (Opcode 0x28, count=1 -> 3 dw) */
        io.Data[2] = ((3u << 30) | (1u << 16) | (0x28u << 8)); /* 0xC0012800 */
        io.Data[3] = 0x80000000u;
        io.Data[4] = 0x80000000u;
        /* 3. PACKET3_PREAMBLE_CNTL: END_CLEAR_STATE (Opcode 0x4A, count=0 -> 2 dw) */
        io.Data[5] = ((3u << 30) | (0u << 16) | (0x4Au << 8)); /* 0xC0004A00 */
        io.Data[6] = 0x00000001u;                               /* END_CLEAR_STATE */
        /* 4. PACKET3_CLEAR_STATE (Opcode 0x12, count=0 -> 2 dw) */
        io.Data[7] = ((3u << 30) | (0u << 16) | (0x12u << 8)); /* 0xC0001200 */
        io.Data[8] = 0x00000000u;
        /* 5. PACKET3_NOP (Opcode 0x10, count=2 -> 4 dw) */
        io.Data[9] = ((3u << 30) | (2u << 16) | (0x10u << 8));  /* 0xC0021000 */
        io.Data[10] = 0x5EED0001u;
        io.Data[11] = 0x5EED0002u;
        io.Data[12] = 0x5EED0003u;
        io.DwordCount = 13;

        rc = Bc250GfxSubmit(&g_Ctx, &io);
        if (rc) {
            printf("falha no envio de probe PM4: erro %d\n", rc);
            return 1;
        }

        printf("Resultado da Sonda GFX CP (Boot Preamble + CLEAR_STATE + NOP):\n");
        GfxPrintSubmitStatus(&io);
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "writedata") == 0) {
        uint32_t targetOff = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0x0E201200u;
        uint32_t value     = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 0xCAFEBABEu;
        uint64_t targetGpu = 0xF400000000ull + (uint64_t)targetOff;
        BC250DBG_GFX_SUBMIT_IO io;
        int rc;

        printf("Enviando PACKET3_WRITE_DATA: VRAM 0x%08X (GPU 0x%llX) <- 0x%08X (5 dwords)...\n",
               targetOff, (unsigned long long)targetGpu, value);
        memset(&io, 0, sizeof(io));
        /* PACKET3_WRITE_DATA (Opcode 0x37, 4 dwords de payload -> count=3, total 5 dw) */
        io.Data[0] = ((3u << 30) | (3u << 16) | (0x37u << 8)); /* 0xC0033700 */
        io.Data[1] = 0x00000500u; /* Engine_SEL=0 (ME), DST_SEL=5 (Memory Async) */
        io.Data[2] = (uint32_t)(targetGpu & 0xFFFFFFFFull);
        io.Data[3] = (uint32_t)(targetGpu >> 32);
        io.Data[4] = value;
        io.DwordCount = 5;

        rc = Bc250GfxSubmit(&g_Ctx, &io);
        if (rc) {
            printf("falha no envio de PACKET3_WRITE_DATA: erro %d\n", rc);
            return 1;
        }

        printf("Resultado do PACKET3_WRITE_DATA:\n");
        GfxPrintSubmitStatus(&io);
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "set_uconfig") == 0) {
        uint32_t regOffset = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0xC040u; /* SOC15 SCRATCH_REG0: SEG1 0xA000 + mm 0x2040 */
        uint32_t value     = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 0xDEADBEEFu;
        BC250DBG_GFX_SUBMIT_IO io;
        int rc;

        printf("Enviando PACKET3_SET_UCONFIG_REG: Reg 0x%04X (UCONFIG offset 0x%04X) <- 0x%08X (3 dwords)...\n",
               regOffset, (regOffset >= 0xC000u) ? (regOffset - 0xC000u) : regOffset, value);
        memset(&io, 0, sizeof(io));
        /* PACKET3_SET_UCONFIG_REG (Opcode 0x79, 2 dwords de payload -> count=1, total 3 dw) */
        io.Data[0] = ((3u << 30) | (1u << 16) | (0x79u << 8)); /* 0xC0017900 */
        io.Data[1] = (regOffset >= 0xC000u) ? (regOffset - 0xC000u) : regOffset;
        io.Data[2] = value;
        io.DwordCount = 3;

        rc = Bc250GfxSubmit(&g_Ctx, &io);
        if (rc) {
            printf("falha no envio de PACKET3_SET_UCONFIG_REG: erro %d\n", rc);
            return 1;
        }

        printf("Resultado do PACKET3_SET_UCONFIG_REG:\n"
               "  RPTR Inicial : 0x%X\n"
               "  WPTR Final   : 0x%X\n"
               "  RPTR Final   : 0x%X\n"
               "  Conclusao    : %s\n",
               io.RptrBefore, io.Wptr, io.RptrAfter,
               io.Completed ? ">>> SUCESSO! O CP processou o SET_UCONFIG_REG e avancou o RPTR!"
                            : "TIMEOUT (RPTR nao alcancou WPTR)");
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "write_reg") == 0) {
        uint32_t regOffset = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0x02040u; /* SCRATCH_REG0 */
        uint32_t value     = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 0x22222222u;
        BC250DBG_GFX_SUBMIT_IO io;
        int rc;

        printf("Enviando PACKET3_WRITE_DATA para MMIO: Reg 0x%05X <- 0x%08X (5 dwords)...\n",
               regOffset, value);
        memset(&io, 0, sizeof(io));
        /* PACKET3_WRITE_DATA (Opcode 0x37, DST_SEL=0 (MMIO Register), WR_CONFIRM=1) */
        io.Data[0] = ((3u << 30) | (3u << 16) | (0x37u << 8)); /* 0xC0033700 */
        io.Data[1] = (1u << 20) | (0u << 8); /* WR_CONFIRM=1, DST_SEL=0 (Register) */
        io.Data[2] = regOffset;
        io.Data[3] = 0;
        io.Data[4] = value;
        io.DwordCount = 5;

        rc = Bc250GfxSubmit(&g_Ctx, &io);
        if (rc) {
            printf("falha no envio de PACKET3_WRITE_DATA MMIO: erro %d\n", rc);
            return 1;
        }

        printf("Resultado do PACKET3_WRITE_DATA (MMIO):\n"
               "  RPTR Inicial : 0x%X\n"
               "  WPTR Final   : 0x%X\n"
               "  RPTR Final   : 0x%X\n"
               "  Conclusao    : %s\n",
               io.RptrBefore, io.Wptr, io.RptrAfter,
               io.Completed ? ">>> SUCESSO! O CP processou o WRITE_DATA MMIO e avancou o RPTR!"
                            : "TIMEOUT (RPTR nao alcancou WPTR)");
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "boot") == 0) {
        BC250DBG_GFX_SUBMIT_IO io;
        int rc;

        printf("Enviando sequencia canonica de inicializacao GFX10 (SET_BASE + CONTEXT_CONTROL + CLEAR_STATE)...\n");
        memset(&io, 0, sizeof(io));
        /* 1. PACKET3_SET_BASE (Opcode 0x11, CE_PARTITION_BASE=2, 0x8000, 0x8000 -> count=2, 4 dw) */
        io.Data[0] = ((3u << 30) | (2u << 16) | (0x11u << 8)); /* 0xC0021100 */
        io.Data[1] = 2u;                                       /* CE_PARTITION_BASE */
        io.Data[2] = 0x00008000u;
        io.Data[3] = 0x00008000u;

        /* 2. PACKET3_CONTEXT_CONTROL (Opcode 0x28, count=1, 3 dw) */
        io.Data[4] = ((3u << 30) | (1u << 16) | (0x28u << 8)); /* 0xC0012800 */
        io.Data[5] = 0x80000000u;
        io.Data[6] = 0x80000000u;

        /* 3. PACKET3_CLEAR_STATE (Opcode 0x12, count=0, 2 dw) */
        io.Data[7] = ((3u << 30) | (0u << 16) | (0x12u << 8)); /* 0xC0001200 */
        io.Data[8] = 0x00000000u;

        /* 4. PACKET3_NOP de fechamento (Opcode 0x10, count=0, 2 dw) */
        io.Data[9]  = ((3u << 30) | (0u << 16) | (0x10u << 8)); /* 0xC0001000 */
        io.Data[10] = 0x5EED0001u;
        io.DwordCount = 11;

        rc = Bc250GfxSubmit(&g_Ctx, &io);
        if (rc) {
            printf("falha no envio da sequencia de boot GFX10: erro %d\n", rc);
            return 1;
        }

        printf("Resultado da Sequencia de Boot GFX10:\n"
               "  RPTR Inicial : 0x%X\n"
               "  WPTR Final   : 0x%X\n"
               "  RPTR Final   : 0x%X\n"
               "  Conclusao    : %s\n",
               io.RptrBefore, io.Wptr, io.RptrAfter,
               io.Completed ? ">>> SUCESSO! O CP processou a sequencia de boot e avancou o RPTR!"
                            : "TIMEOUT (RPTR nao alcancou WPTR)");
        return io.Completed ? 0 : 1;
    }

    if (_stricmp(argv[2], "matrix") == 0) {
        static const struct {
            const char *Name;
            uint32_t Base;
            uint32_t BaseHi;
        } c_Bases[] = {
            { "GPU Virtual (0xF40FE000)", 0xF40FE000u, 0 },
            { "Physical PA (0x045FE000)", 0x045FE000u, 0 },
            { "VRAM Offset (0x000FE000)", 0x000FE000u, 0 }
        };
        int b;
        uint32_t vramOff = 0x0FE00000;
        uint32_t dummy = 0;

        printf("=== MATRIZ DE ALINHAMENTO DE ENDERECO DO CP (PM4 RING 0) ===\n\n");

        /* Prepara o conteudo do anel na VRAM */
        Bc250VramWrite(&g_Ctx, vramOff + 0, 0xC0001200u, &dummy); /* CLEAR_STATE */
        Bc250VramWrite(&g_Ctx, vramOff + 4, 0xC0001000u, &dummy); /* NOP */
        Bc250HdpInvalidate(&g_Ctx);

        for (b = 0; b < 3; b++) {
            uint32_t rptr0, rptr1, grbm0, grbm1, mec;
            int i;

            printf("[Teste %d/3] Espaco: %s\n", b + 1, c_Bases[b].Name);

            /* 1. Programa BASE e BASE_HI */
            Bc250RegWrite(&g_Ctx, 0x0C100, c_Bases[b].Base, &dummy);
            Bc250RegWrite(&g_Ctx, 0x0C2C4, c_Bases[b].BaseHi, &dummy);

            /* 2. Reseta ponteiros */
            Bc250RegWrite(&g_Ctx, 0x0C154, 0, &dummy);
            Bc250RegWrite(&g_Ctx, 0x0C150, 0, &dummy);

            /* 3. Unhalt CP */
            Bc250RegWrite(&g_Ctx, 0x086D8, 0x15000150, &dummy);
            Sleep(1);
            Bc250RegWrite(&g_Ctx, 0x086D8, 0x00000000, &dummy);
            Sleep(1);

            rptr0 = Bc250RegRead(&g_Ctx, 0x08700, &dummy);
            grbm0 = Bc250RegRead(&g_Ctx, 0x08010, &dummy);

            /* 4. Submete WPTR = 2 (dwords) */
            Bc250RegWrite(&g_Ctx, 0x0C154, 0, &dummy);
            Bc250RegWrite(&g_Ctx, 0x0C150, 2, &dummy);

            rptr1 = rptr0;
            for (i = 0; i < 50; i++) {
                rptr1 = Bc250RegRead(&g_Ctx, 0x08700, &dummy);
                if (rptr1 != 0) break;
                Sleep(2);
            }
            grbm1 = Bc250RegRead(&g_Ctx, 0x08010, &dummy);
            mec   = Bc250RegRead(&g_Ctx, 0x086D8, &dummy);

            printf("  WPTR=2 (dwords) -> RPTR: 0x%X -> 0x%X  GRBM: 0x%08X  ME_CNTL: 0x%08X  [%s]\n",
                   rptr0, rptr1, grbm1, mec,
                   (rptr1 == 2) ? "*** SUCESSO! AVANCOU! ***" : (rptr1 > 0) ? "PARCIAL" : "sem avanco");

            if (rptr1 == 2) {
                printf("\n>>> BASE CORRETA ENCONTRADA: %s!\n\n", c_Bases[b].Name);
                return 0;
            }

            /* 5. Submete WPTR = 8 (bytes) */
            Bc250RegWrite(&g_Ctx, 0x0C150, 8, &dummy);
            for (i = 0; i < 50; i++) {
                rptr1 = Bc250RegRead(&g_Ctx, 0x08700, &dummy);
                if (rptr1 != 0) break;
                Sleep(2);
            }
            grbm1 = Bc250RegRead(&g_Ctx, 0x08010, &dummy);

            printf("  WPTR=8 (bytes)  -> RPTR: 0x%X -> 0x%X  GRBM: 0x%08X  [%s]\n\n",
                   rptr0, rptr1, grbm1,
                   (rptr1 == 8) ? "*** SUCESSO EM MODO BYTES! ***" : (rptr1 > 0) ? "PARCIAL" : "sem avanco");

            if (rptr1 == 8) {
                printf("\n>>> BASE CORRETA ENCONTRADA (MODO BYTES): %s!\n\n", c_Bases[b].Name);
                return 0;
            }
        }

        printf("Fim da matriz. Se nenhum avancou, vamos inspecionar o status do pipeline CE/PFP/ME.\n");
        return 0;
    }

    if (_stricmp(argv[2], "diag") == 0) {
        uint32_t vramOff = 0x0FE00000;
        uint32_t dummy = 0;
        uint32_t rptr0, rptr1, st1, st2, st3, cpStat, grbm, meHdr, faultStat, faultLo, faultHi;
        uint32_t smuVer = 0, smuFeat = 0, activeWgp = 0, gfxClk = 0;
        uint32_t val0 = 0, val1 = 0, valCopy0 = 0, valCopy1 = 0;
        int i;

        printf("========================================================\n");
        printf("  BC-250 (GFX10.1) - DIAGNOSTICO DO CP / RCIU / GFXHUB  \n");
        printf("========================================================\n\n");

        /* 1. Telemetria SMU */
        {
            uint32_t st = 0, res = 0;
            if (Bc250SmuMsg(&g_Ctx, 0, 0x02, 0, 0, &st, &res) == 0) smuVer = res;
            if (Bc250SmuMsg(&g_Ctx, 0, 0x3D, 0, 0, &st, &res) == 0) smuFeat = res;
            if (Bc250SmuMsg(&g_Ctx, 0, 0x1E, 0, 0, &st, &res) == 0) activeWgp = res;
            if (Bc250SmuMsg(&g_Ctx, 0, 0x0F, 0, 0, &st, &res) == 0) gfxClk = res;
        }

        printf("[1] Telemetria SMU / Energia:\n");
        printf("  SMU Versao     : 0x%08X\n", smuVer);
        printf("  SMU Features   : 0x%08X (GFXOFF = %s)\n", smuFeat, (smuFeat & 4) ? "LIGADO (bloqueia)" : "DESLIGADO (ok)");
        printf("  WGPs Ativos    : %u (%u CUs) %s\n", activeWgp, activeWgp * 2, (activeWgp > 0) ? "[OK]" : "[ALERTA: Deep Sleep]");
        printf("  GFX Clock      : %u MHz\n\n", gfxClk);

        /* 2. Grava 2 dwords no Ring Buffer (NOP minimo: 0xC0001000, 0x0) */
        Bc250VramWrite(&g_Ctx, vramOff + 0, 0xC0001000u, &dummy); /* PACKET3_NOP */
        Bc250VramWrite(&g_Ctx, vramOff + 4, 0x00000000u, &dummy); /* NOP body */
        Bc250HdpInvalidate(&g_Ctx);

        Bc250VramRead(&g_Ctx, vramOff + 0, &val0);
        Bc250VramRead(&g_Ctx, vramOff + 4, &val1);

        printf("[2] Visibilidade do Anel PM4 pela CPU (BAR0):\n");
        printf("  [0x%08X] = 0x%08X (esperado 0xC0001000) %s\n", vramOff + 0, val0, (val0 == 0xC0001000u) ? "[OK]" : "[FALHA]");
        printf("  [0x%08X] = 0x%08X (esperado 0x00000000) %s\n\n", vramOff + 4, val1, (val1 == 0x00000000u) ? "[OK]" : "[FALHA]");

        /* 3. Prova cruzada de leitura GPU via SDMA */
        {
            BC250DBG_SDMA_OP_IO sdmaIo;
            int sdmaRc;
            memset(&sdmaIo, 0, sizeof(sdmaIo));
            sdmaIo.Op = BC250DBG_SDMA_OP_COPY;
            sdmaIo.SrcOffset = vramOff;
            sdmaIo.DstOffset = vramOff + 0x100;
            sdmaIo.ByteCount = 8;
            sdmaRc = Bc250SdmaOp(&g_Ctx, &sdmaIo);
            if (sdmaRc == 0 && sdmaIo.Completed) {
                Bc250HdpInvalidate(&g_Ctx);
                Bc250VramRead(&g_Ctx, vramOff + 0x100, &valCopy0);
                Bc250VramRead(&g_Ctx, vramOff + 0x104, &valCopy1);
                printf("[3] Prova Cruzada de Memoria pela GPU (SDMA Copy):\n");
                printf("  Copia [0x%08X] = 0x%08X %s\n", vramOff + 0x100, valCopy0, (valCopy0 == 0xC0001000u) ? "[OK: GPU enxerga os dados fisicos]" : "[DIVERGENCIA]");
                printf("  Copia [0x%08X] = 0x%08X %s\n\n", vramOff + 0x104, valCopy1, (valCopy1 == 0x00000000u) ? "[OK: GPU enxerga os dados fisicos]" : "[DIVERGENCIA]");
            } else {
                printf("[3] Prova Cruzada de Memoria (SDMA): Anel SDMA nao armado nesta sessao (ignorado).\n\n");
            }
        }

        /* 4. Estado Inicial do CP antes do Kick */
        rptr0     = Bc250RegRead(&g_Ctx, 0x08700, &dummy); /* CP_RB0_RPTR */
        st1       = Bc250RegRead(&g_Ctx, 0x08674, &dummy); /* CP_STALLED_STAT1 */
        st2       = Bc250RegRead(&g_Ctx, 0x08678, &dummy); /* CP_STALLED_STAT2 */
        st3       = Bc250RegRead(&g_Ctx, 0x0867C, &dummy); /* CP_STALLED_STAT3 */
        cpStat    = Bc250RegRead(&g_Ctx, 0x08680, &dummy); /* CP_STAT */
        meHdr     = Bc250RegRead(&g_Ctx, 0x08684, &dummy); /* CP_ME_HEADER_DUMP */
        grbm      = Bc250RegRead(&g_Ctx, 0x08010, &dummy); /* GRBM_STATUS */
        faultStat = Bc250RegRead(&g_Ctx, 0x0A180, &dummy); /* GCVM_L2_PROTECTION_FAULT_STATUS */
        faultLo   = Bc250RegRead(&g_Ctx, 0x0A184, &dummy); /* GCVM_L2_PROTECTION_FAULT_ADDR_LO32 */
        faultHi   = Bc250RegRead(&g_Ctx, 0x0A188, &dummy); /* GCVM_L2_PROTECTION_FAULT_ADDR_HI32 */

        printf("[4] Estado do CP antes do Kick:\n");
        printf("  CP_RB0_BASE    : 0x%08X (GPU 0xF40FE00000)\n", Bc250RegRead(&g_Ctx, 0x0C100, &dummy));
        printf("  CP_RB0_CNTL    : 0x%08X (RB_EXE = %u)\n", Bc250RegRead(&g_Ctx, 0x0C104, &dummy), (Bc250RegRead(&g_Ctx, 0x0C104, &dummy) >> 28) & 1);
        printf("  CP_RB0_RPTR    : 0x%X\n", rptr0);
        printf("  CP_ME_HEADER   : 0x%08X (%s)\n", meHdr, (meHdr == 0xC0004200) ? "PACKET3_PFP_SYNC_ME" : "outro");
        printf("  GRBM_STATUS    : 0x%08X (GUI_ACTIVE = %u, CMDFIFO_AVAIL = %u)\n", grbm, (grbm >> 31) & 1, grbm & 0xF);
        printf("  STALLED_STAT2  : 0x%08X (PFP_RCIU_READ_PENDING = %u)\n\n", st2, (st2 >> 5) & 1);

        /* 5. Kick de WPTR = 2 */
        printf("[5] Executando Kick de WPTR = 2 (dwords)...\n");
        Bc250RegWrite(&g_Ctx, 0x0C154, 0, &dummy);
        Bc250RegWrite(&g_Ctx, 0x0C150, 2, &dummy);

        /* 6. Amostragem em tempo real */
        rptr1 = rptr0;
        for (i = 0; i < 50; i++) {
            rptr1 = Bc250RegRead(&g_Ctx, 0x08700, &dummy);
            if (rptr1 == 2) break;
            Sleep(1);
        }

        /* 7. Leitura da telemetria pos-kick */
        st1       = Bc250RegRead(&g_Ctx, 0x08674, &dummy);
        st2       = Bc250RegRead(&g_Ctx, 0x08678, &dummy);
        st3       = Bc250RegRead(&g_Ctx, 0x0867C, &dummy);
        cpStat    = Bc250RegRead(&g_Ctx, 0x08680, &dummy);
        meHdr     = Bc250RegRead(&g_Ctx, 0x08684, &dummy);
        grbm      = Bc250RegRead(&g_Ctx, 0x08010, &dummy);
        faultStat = Bc250RegRead(&g_Ctx, 0x0A180, &dummy);
        faultLo   = Bc250RegRead(&g_Ctx, 0x0A184, &dummy);
        faultHi   = Bc250RegRead(&g_Ctx, 0x0A188, &dummy);

        printf("\n[6] Telemetria Pos-Kick:\n");
        printf("  RPTR Inicial   : 0x%X -> Final: 0x%X\n", rptr0, rptr1);
        printf("  CP_STAT        : 0x%08X (CP_BUSY=%u, RCIU_BUSY=%u, PFP_BUSY=%u)\n",
               cpStat, (cpStat >> 31) & 1, (cpStat >> 23) & 1, (cpStat >> 15) & 1);
        printf("  STALLED_STAT1  : 0x%08X\n", st1);
        printf("  STALLED_STAT2  : 0x%08X (PFP_RCIU_READ_PENDING = %u)\n", st2, (st2 >> 5) & 1);
        printf("  STALLED_STAT3  : 0x%08X\n", st3);
        printf("  CP_ME_HEADER   : 0x%08X\n", meHdr);
        printf("  GRBM_STATUS    : 0x%08X\n", grbm);
        printf("  GFXHUB Fault   : 0x%08X (Addr: 0x%08X_%08X)\n\n", faultStat, faultHi, faultLo);

        printf("=== DIAGNOSTICO FINAL ===\n");
        if (rptr1 == 2) {
            printf(">>> SUCESSO ABSOLUTO! O CP CONSUMIU O PACOTE E AVANCOU O RPTR!\n");
        } else if ((st2 & 0x20)) {
            printf(">>> PFP AGUARDANDO RESPOSTA DA RCIU/GFXHUB (Leitura em voo no barramento).\n");
            printf("    A requisicao alcancou a interface RCIU mas nao retornou da L2.\n");
        } else if (faultStat != 0) {
            printf(">>> FALHA DE PROTECAO NO GFXHUB: Status 0x%08X no endereco 0x%08X_%08X\n",
                   faultStat, faultHi, faultLo);
        } else {
            printf(">>> WPTR rejeitado ou motor CP inativo (verifique SMU/WGPs e CP_ME_CNTL).\n");
        }
        return 0;
    }

    /* Fail closed antes do bloco historico abaixo: nenhum arm ou IOCTL. */
    if (_stricmp(argv[2], "bringup") == 0) {
        printf("GFX bringup legado em QUARENTENA: depende do SETUP_TMR fixo e inseguro.\n"
               "Nenhum arm, IOCTL de escrita ou acesso ao hardware foi solicitado.\n");
        return 1;
    }

    if (_stricmp(argv[2], "bringup") == 0) {
        static const struct {
            uint32_t Type;
            const char *Name;
            const char *File;
        } c_Fw[] = {
            { BC250_PSP_FW_RLC_G, "RLC",  "firmware\\cyan_skillfish2_rlc.bin" },
            { BC250_PSP_FW_CP_CE, "CE",   "firmware\\cyan_skillfish2_ce.bin" },
            { BC250_PSP_FW_CP_PFP,"PFP",  "firmware\\cyan_skillfish2_pfp.bin" },
            { BC250_PSP_FW_CP_ME, "ME",   "firmware\\cyan_skillfish2_me.bin" },
            { BC250_PSP_FW_CP_MEC,"MEC",  "firmware\\cyan_skillfish2_mec.bin" },
            { 5,                  "MEC2", "firmware\\cyan_skillfish2_mec2.bin" },
        };
        BC250DBG_PSP_SETUP_TMR_IO tmrIo;
        BC250DBG_GFX_INIT_IO gfxIo;
        BC250DBG_GFX_SUBMIT_IO submitIo;
        uint32_t st = 0, res = 0;
        uint32_t rptr0, rptr1, st2, cpStat, grbm, meHdr;
        int i, rc;

        printf("========================================================\n");
        printf("  [FASE 4] Pipeline Unificado GFX CP Bring-up           \n");
        printf("========================================================\n");

        Bc250Arm(&g_Ctx, BC250DBG_ARM_WRITE);

        /* 1. Desabilita GFXOFF na Fila 2 */
        Bc250SmuMsg(&g_Ctx, 2, 0x06, 0x04, 0, &st, &res);
        printf("  [1/5] SMU GFXOFF Power    : Fila 2 Msg 6 -> Status 0x%02X [OK]\n", st);

        /* 2. PSP TMR Carveout */
        memset(&tmrIo, 0, sizeof(tmrIo));
        tmrIo.TmrSizeBytes = 4 * 1024 * 1024;
        tmrIo.VramOffset   = 0x0F800000;
        rc = Bc250PspSetupTmr(&g_Ctx, &tmrIo);
        if (rc || tmrIo.RespStatus != 0) {
            printf("  [FALHA] PSP Setup TMR (status 0x%08X)\n", tmrIo.RespStatus);
            return 1;
        }
        printf("  [2/5] PSP TMR Carveout    : 4 MB em GPU 0x%llX [OK]\n",
               (unsigned long long)tmrIo.TmrGpuAddr);

        /* 3. Carga dos 6 Microcodigos Graficos */
        printf("  [3/5] Autenticacao Firmwares Graficos (PSP TMR):\n");
        for (i = 0; i < 6; i++) {
            uint32_t fwSt = 0, fwAddr = 0;
            rc = BcpLoadIpFwSilent(c_Fw[i].Type, c_Fw[i].File, &fwSt, &fwAddr);
            if (rc || fwSt != 0) {
                printf("        [!] %-5s : FALHOU (status 0x%08X)\n", c_Fw[i].Name, fwSt);
                return 1;
            }
            printf("        [*] %-5s : Autenticado pelo PSP (RespStatus = 0x0) [OK]\n", c_Fw[i].Name);
        }

        /* 4. Inicializa MMU GCVM e Aberturas da GFXHUB */
        BC250DBG_GCVM_INIT_IO gcvmIo;
        memset(&gcvmIo, 0, sizeof(gcvmIo));
        rc = Bc250GcvmInit(&g_Ctx, &gcvmIo);
        if (rc) { printf("  [FALHA] GCVM Init (erro %d)\n", rc); return 1; }

        /* 5. Inicializa Anel PM4 (GFX Ring 0 em VRAM 254 MB) */
        memset(&gfxIo, 0, sizeof(gfxIo));
        gfxIo.VramOffset = 0x0FE00000;
        rc = Bc250GfxInit(&g_Ctx, &gfxIo);
        if (rc) { printf("  [FALHA] GFX Init (erro %d)\n", rc); return 1; }
        printf("  [4/5] Anel PM4 (Ring 0)   : 4 KB em %s 0x%llX (CP_ME_CNTL=0x0) [OK]\n",
               (gfxIo.VramOffset == 0) ? "System RAM PA" : "VRAM GPU",
               (unsigned long long)gfxIo.GpuAddr);

        /* 5. Sequencia Canonica PM4: CLEAR_STATE + CONTEXT_CONTROL + PFP_SYNC_ME + WRITE_DATA(Reg) + WRITE_DATA(Vram) + NOP (19 dwords) */
        uint32_t scratchBefore = 0, scratchAfter = 0, dummyWrite = 0;
        uint32_t vramFenceBefore = 0, vramFenceAfter = 0;

        Bc250RegWrite(&g_Ctx, 0x30100, 0xCAFEDEADu, &dummyWrite);
        Bc250RegRead(&g_Ctx, 0x30100, &scratchBefore);
        uint32_t vramReleaseBefore = 0, vramReleaseAfter = 0;
        Bc250VramWrite(&g_Ctx, 0x0FE01200, 0x11111111u, &dummyWrite);
        Bc250VramRead(&g_Ctx, 0x0FE01200, &vramFenceBefore);
        Bc250VramWrite(&g_Ctx, 0x0FE01300, 0x22222222u, &dummyWrite);
        Bc250VramRead(&g_Ctx, 0x0FE01300, &vramReleaseBefore);

        memset(&submitIo, 0, sizeof(submitIo));
        /* 1. NOP Inicial */
        submitIo.Data[0] = 0xC0001000u; /* PACKET3_NOP */
        submitIo.Data[1] = 0x00000000u;
        /* 2. Prova A: Escrita em Registrador SOC15 mmSCRATCH_REG0 (0x2040) */
        submitIo.Data[2] = 0xC0033700u; /* PACKET3_WRITE_DATA */
        submitIo.Data[3] = 0x00010000u; /* WR_CONFIRM=1, DST_SEL=0 (Register), ENGINE_SEL=0 (ME) */
        submitIo.Data[4] = 0x00002040u; /* mmSCRATCH_REG0 */
        submitIo.Data[5] = 0x00000000u;
        submitIo.Data[6] = 0xDEADBEEFu; /* Valor de Prova */
        /* 3. NOP Final */
        submitIo.Data[7] = 0xC0001000u; /* PACKET3_NOP */
        submitIo.DwordCount = 8;
        Bc250GfxSubmit(&g_Ctx, &submitIo);

        rptr0  = submitIo.RptrBefore;
        rptr1  = submitIo.RptrAfter;
        Bc250RegRead(&g_Ctx, 0x30100, &scratchAfter);
        Bc250HdpInvalidate(&g_Ctx);
        Bc250VramRead(&g_Ctx, 0x0FE01200, &vramFenceAfter);
        Bc250VramRead(&g_Ctx, 0x0FE01300, &vramReleaseAfter);
        Bc250RegRead(&g_Ctx, 0x08678, &st2);    /* CP_STALLED_STAT2 */
        Bc250RegRead(&g_Ctx, 0x08680, &cpStat); /* CP_STAT */
        Bc250RegRead(&g_Ctx, 0x08684, &meHdr);  /* CP_ME_HEADER_DUMP */
        Bc250RegRead(&g_Ctx, 0x08010, &grbm);   /* GRBM_STATUS */

        printf("  [5/5] Sonda PM4 e Telemetria do CP:\n");
        printf("        RPTR: 0x%X -> 0x%X (WPTR: 0x%X)\n", rptr0, rptr1, submitIo.Wptr);
        printf("        SCRATCH_REG0 : 0x%08X -> 0x%08X %s\n",
               scratchBefore, scratchAfter,
               (scratchAfter == 0xDEADBEEF) ? "[PROVA CONFIRMADA: GPU GRAVOU REGISTRO!]" : "");
        printf("        VRAM FENCE   : 0x%08X -> 0x%08X %s\n",
               vramFenceBefore, vramFenceAfter,
               (vramFenceAfter == 0xCAFEBABE) ? "[PROVA CONFIRMADA: GPU GRAVOU VRAM!]" : "");
        printf("        RELEASE_MEM  : 0x%08X -> 0x%08X %s\n",
               vramReleaseBefore, vramReleaseAfter,
               (vramReleaseAfter == 0xBEEFBEEF) ? "[PROVA CONFIRMADA: EOP FENCE GRAVOU VRAM!]" : "");
        printf("        GRBM_STATUS  : 0x%08X (GUI_ACTIVE = %u)\n", grbm, (grbm >> 31) & 1);
        printf("        CP_STAT      : 0x%08X (CP_BUSY = %u, PFP_BUSY = %u)\n",
               cpStat, (cpStat >> 31) & 1, (cpStat >> 15) & 1);
        printf("        CP_ME_HEADER : 0x%08X\n", meHdr);
        printf("        STALLED_STAT2: 0x%08X (PFP_RCIU_READ_PENDING = %u)\n", st2, (st2 >> 5) & 1);

        if (rptr1 == submitIo.Wptr || scratchAfter == 0xDEADBEEF || vramFenceAfter == 0xCAFEBABE || vramReleaseAfter == 0xBEEFBEEF) {
            printf("\n>>> SUCESSO HISTORICO: O CP EXECUTOU OS PACOTES PM4 E COMPROVOU A FASE 4!\n\n");
            return 0;
        } else {
            printf("\n>>> STATUS: CP e Firmwares Ativos. Aguardando conclusao do fetch RCIU/L2.\n\n");
            return 0;
        }
    }
    printf("subcomando gfx '%s' desconhecido.\n", argv[2]);
    return 1;
}

static int CmdRing(int argc, char **argv)
{
    uint32_t rptr0 = 0, rptr1 = 0, grbm = 0, meCntl = 0, dummy = 0;
    uint32_t scratchBefore = 0, scratchAfter = 0;
    uint32_t fenceBefore = 0, fenceAfter = 0;
    uint32_t vramRingBase = 0x0FE00000;
    uint32_t fenceOffset  = 0x0FE01200;
    int i;

    (void)argc;
    (void)argv;

    printf("========================================================\n");
    printf("  AMD BC-250 (GFX10.1) - Teste Rigoroso de Anel PM4     \n");
    printf("========================================================\n");

    /* 1. Alinha a MMU, System Aperture e Page Table da GART */
    Bc250RegWrite(&g_Ctx, 0x0A5AC, 0x00000400, &dummy); /* A_GCMC_VM_FB_OFFSET = 0x400 (PA 0x0400000000) */
    Bc250RegWrite(&g_Ctx, 0x0A61C, 0x00001859, &dummy); /* A_GCMC_VM_MX_L1_TLB_CNTL (SYSTEM_ACCESS_MODE=3) */
    Bc250RegWrite(&g_Ctx, 0x0A100, 0x00080E03, &dummy); /* A_GCVM_L2_CNTL */

    /* Preenche as entradas da GART para os 256 MB */
    for (i = 0; i < 4096; i++) {
        uint32_t pteOffset = 0x0FC00000 + i * 8;
        uint32_t pageLo    = (i * 4096) | 0x000000E1u;
        uint32_t pageHi    = 0x0C000004u;
        Bc250VramWrite(&g_Ctx, pteOffset + 0, pageLo, NULL);
        Bc250VramWrite(&g_Ctx, pteOffset + 4, pageHi, NULL);
    }

    Bc250RegWrite(&g_Ctx, 0x0A3AC, 0x0FC00000, &dummy); /* GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO */
    Bc250RegWrite(&g_Ctx, 0x0A3B0, 0x00000004, &dummy); /* GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI */
    Bc250RegWrite(&g_Ctx, 0x0A42C, 0x00000000, &dummy); /* GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO */
    Bc250RegWrite(&g_Ctx, 0x0A430, 0x00000000, &dummy);
    Bc250RegWrite(&g_Ctx, 0x0A4AC, 0x0000FFFF, &dummy); /* GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO */
    Bc250RegWrite(&g_Ctx, 0x0A4B0, 0x00000000, &dummy);
    Bc250RegWrite(&g_Ctx, 0x0A200, 0x007FFE81, &dummy); /* ENABLE_CONTEXT = 1 */
    Bc250RegWrite(&g_Ctx, 0x0A104, 3, &dummy);          /* TLB Flush & L2 Invalidate */
    Bc250RegWrite(&g_Ctx, 0x03FC4, 1, &dummy);          /* HDP Read Cache Invalidate */

    /* 2. Limpa todo o anel de 4 KB com NOPs validos */
    for (i = 0; i < 1024; i += 2) {
        Bc250VramWrite(&g_Ctx, vramRingBase + i * 4, 0xC0001000u, NULL);
        Bc250VramWrite(&g_Ctx, vramRingBase + (i + 1) * 4, 0x00000000u, NULL);
    }

    /* 3. Prepara a Cerca e o Registrador de Teste */
    Bc250VramWrite(&g_Ctx, fenceOffset, 0x11111111u, NULL);
    Bc250VramRead(&g_Ctx, fenceOffset, &fenceBefore);
    Bc250RegWrite(&g_Ctx, 0x30100, 0x22222222u, &dummy);
    Bc250RegRead(&g_Ctx, 0x30100, &scratchBefore);

    /* 4. Monta os Pacotes PM4 na base do anel:
          - NOP Inicial
          - WRITE_DATA para mmSCRATCH_REG0 (0x2040) -> 0xDEADBEEF
          - WRITE_DATA para VRAM Fence (GART 0x000FE01200) -> 0xCAFEBABE
          - NOP Final                                                */
    /* NOP (2 dwords) */
    Bc250VramWrite(&g_Ctx, vramRingBase + 0, 0xC0001000u, NULL);
    Bc250VramWrite(&g_Ctx, vramRingBase + 4, 0x00000000u, NULL);
    /* WRITE_DATA to mmSCRATCH_REG0 (5 dwords) */
    Bc250VramWrite(&g_Ctx, vramRingBase + 8,  0xC0033700u, NULL); /* PACKET3_WRITE_DATA */
    Bc250VramWrite(&g_Ctx, vramRingBase + 12, 0x00010000u, NULL); /* WR_CONFIRM=1, DST_SEL=0 (Reg), ME */
    Bc250VramWrite(&g_Ctx, vramRingBase + 16, 0x00002040u, NULL); /* mmSCRATCH_REG0 */
    Bc250VramWrite(&g_Ctx, vramRingBase + 20, 0x00000000u, NULL);
    Bc250VramWrite(&g_Ctx, vramRingBase + 24, 0xDEADBEEFu, NULL); /* Valor de prova */
    /* WRITE_DATA to VRAM Fence (5 dwords) */
    Bc250VramWrite(&g_Ctx, vramRingBase + 28, 0xC0033700u, NULL); /* PACKET3_WRITE_DATA */
    Bc250VramWrite(&g_Ctx, vramRingBase + 32, 0x00010200u, NULL); /* WR_CONFIRM=1, DST_SEL=2 (Mem), ME */
    Bc250VramWrite(&g_Ctx, vramRingBase + 36, 0x0FE01200u, NULL); /* 0x0FE01200 GART LO */
    Bc250VramWrite(&g_Ctx, vramRingBase + 40, 0x00000000u, NULL); /* 0x0FE01200 GART HI */
    Bc250VramWrite(&g_Ctx, vramRingBase + 44, 0xCAFEBABEu, NULL); /* Valor de prova da Cerca */
    /* NOP Final */
    Bc250VramWrite(&g_Ctx, vramRingBase + 48, 0xC0001000u, NULL);
    Bc250VramWrite(&g_Ctx, vramRingBase + 52, 0x00000000u, NULL);

    /* Flush de coerencia */
    Bc250RegWrite(&g_Ctx, 0x03FC4, 1, &dummy);

    /* 5. Congela CP, Reseta Ponteiros com RB_RPTR_WR_ENA=1, Descongela com RB_NO_UPDATE=0 */
    Bc250RegWrite(&g_Ctx, 0x086D8, 0x15000150, &dummy);
    Bc250RegWrite(&g_Ctx, 0x0C100, 0x0000FE00, &dummy); /* CP_RB0_BASE */
    Bc250RegWrite(&g_Ctx, 0x0C104, 0x80A0080A, &dummy);
    Bc250RegWrite(&g_Ctx, 0x0C10C, 0x0FE01000, &dummy); /* CP_RB0_RPTR_ADDR */
    Bc250RegWrite(&g_Ctx, 0x0C110, 0x00000000, &dummy); /* CP_RB0_RPTR_ADDR_HI */
    Bc250RegWrite(&g_Ctx, 0x08700, 0, &dummy);
    Bc250RegWrite(&g_Ctx, 0x0C150, 0, &dummy);
    Bc250RegWrite(&g_Ctx, 0x0C104, 0x00A0080A, &dummy); /* Linux canonical RB_CNTL */
    Bc250RegWrite(&g_Ctx, 0x086D8, 0x00000000, &dummy); /* CP Unhalt */

    /* 5. Telemetria inicial */
    Bc250RegRead(&g_Ctx, 0x086D8, &meCntl);
    Bc250RegRead(&g_Ctx, 0x08010, &grbm);
    Bc250RegRead(&g_Ctx, 0x0DA6C, &rptr0);

    printf("  [1/4] Estado Inicial do CP:\n");
    printf("        CP_ME_CNTL   : 0x%08X (CP %s)\n", meCntl, (meCntl == 0) ? "ATIVO" : "CONGELADO");
    printf("        GRBM_STATUS  : 0x%08X\n", grbm);
    printf("        RPTR Inicial : 0x%08X\n\n", rptr0);

    /* 6. Dispara o Kick de WPTR = 14 no bloco canonico 0xDA78 */
    printf("  [2/4] Disparando Kick em Ring 0 (0xDA78) e HQD (MEC)...\n");
    Bc250RegWrite(&g_Ctx, 0x0DA78, (rptr0 & 0xFFFF0000) | 14, &dummy); /* Ring 0 WPTR (formato 0x0020000E) */
    Bc250RegWrite(&g_Ctx, 0x0C150, 14, &dummy);                        /* Fallback Legado */

    /* Tambem dispara no HQD MEC (0x091DC) */
    Bc250RegWrite(&g_Ctx, 0x034D0, 0xE0000000, &dummy); /* GRBM_GFX_INDEX = Broadcast */
    Bc250RegWrite(&g_Ctx, 0x0910C, 1, &dummy);          /* HQD_ACTIVE */
    Bc250RegWrite(&g_Ctx, 0x091DC, 14, &dummy);         /* HQD_PQ_WPTR = 14 */

    /* 7. Amostragem do hardware */
    rptr1 = rptr0;
    for (i = 0; i < 100; i++) {
        Bc250RegRead(&g_Ctx, 0x0DA6C, &rptr1);
        if (rptr1 != rptr0 && (rptr1 & 0xFFFF) == 14) break;
        Sleep(1);
    }

    Bc250RegWrite(&g_Ctx, 0x03FC4, 1, &dummy); /* Invalida Cache de Leitura */
    Bc250RegRead(&g_Ctx, 0x30100, &scratchAfter);
    Bc250VramRead(&g_Ctx, fenceOffset, &fenceAfter);
    Bc250RegRead(&g_Ctx, 0x08010, &grbm);

    printf("\n  [3/4] Telemetria Pos-Execucao:\n");
    printf("        RPTR: 0x%08X -> 0x%08X (WPTR: 0x%08X)\n", rptr0, rptr1, (rptr0 & 0xFFFF0000) | 14);
    printf("        SCRATCH_REG0 : 0x%08X -> 0x%08X %s\n",
           scratchBefore, scratchAfter,
           (scratchAfter == 0xDEADBEEF) ? "[PROVA CONFIRMADA: GPU GRAVOU REGISTRO!]" : "");
    printf("        VRAM FENCE   : 0x%08X -> 0x%08X %s\n",
           fenceBefore, fenceAfter,
           (fenceAfter == 0xCAFEBABE) ? "[PROVA CONFIRMADA: GPU GRAVOU GDDR6!]" : "");
    printf("        GRBM_STATUS  : 0x%08X\n\n", grbm);

    printf("========================================================\n");
    if (rptr1 == 14 || scratchAfter == 0xDEADBEEF || fenceAfter == 0xCAFEBABE) {
        printf(">>> [VALIDADO EM HARDWARE] O CP EXECUTOU OS PACOTES PM4! <<<\n");
        return 0;
    } else {
        printf(">>> [NAO VALIDADO EM HARDWARE] CP nao consumiu o anel (RPTR estagnado).\n");
        return 1;
    }
}

int main(int argc, char **argv)
{
    int rc;

    if (argc < 2) {
        Usage();
        return 1;
    }

    if (_stricmp(argv[1], "vbios") == 0) {
        return CmdVbios(argc, argv);
    }
    if (_stricmp(argv[1], "fw") == 0) {
        return CmdFirmware(argc, argv);
    }
    /* 'cp info' e leitura de arquivo: nao precisa do driver, e e util poder
     * conferir um header sem o laboratorio carregado. */
    if (_stricmp(argv[1], "cp") == 0 && argc >= 3 && _stricmp(argv[2], "info") == 0) {
        return CmdCp(argc, argv);
    }

    rc = Bc250Open(&g_Ctx);
    if (rc) {
        printf("nao consegui abrir %s (erro %d).\n", BC250DBG_USER_DEVICE_PATH, rc);
        printf("O driver esta carregado? Rode dbg\\install.bat como admin.\n");
        return 1;
    }

    if (_stricmp(argv[1], "info") == 0)          rc = CmdInfo();
    else if (_stricmp(argv[1], "regs") == 0)     rc = CmdRegs();
    else if (_stricmp(argv[1], "reg") == 0)      rc = CmdReg(argc, argv, 0);
    else if (_stricmp(argv[1], "smn") == 0)      rc = CmdReg(argc, argv, 1);
    else if (_stricmp(argv[1], "smu") == 0)      rc = CmdSmu(argc, argv);
    else if (_stricmp(argv[1], "snap") == 0)     rc = CmdSnap(argc, argv);
    else if (_stricmp(argv[1], "diff") == 0)     rc = CmdDiff(argc, argv);
    else if (_stricmp(argv[1], "dumpall") == 0)  rc = CmdDumpAll(argc, argv);
    else if (_stricmp(argv[1], "scan") == 0)     rc = CmdScan(argc, argv);
    else if (_stricmp(argv[1], "oracle") == 0)   rc = CmdOracle(argc, argv);
    else if (_stricmp(argv[1], "run") == 0)      rc = CmdRun(argc, argv);
    else if (_stricmp(argv[1], "log") == 0)      rc = CmdLog(argc, argv);
    else if (_stricmp(argv[1], "arm") == 0)      rc = CmdArm(argc, argv);
    else if (_stricmp(argv[1], "thermal") == 0)  rc = CmdThermal(argc, argv);
    else if (_stricmp(argv[1], "telemetry") == 0) rc = CmdTelemetry(argc, argv);
    else if (_stricmp(argv[1], "sdma") == 0)     rc = CmdSdma(argc, argv);
    else if (_stricmp(argv[1], "vram") == 0)     rc = CmdVram(argc, argv);
    /*
     * Coerencia HDP entre CPU e GPU.
     *
     * BcVramWriteRaw NAO faz flush: ele so escreve. Quem invalida e a LEITURA
     * (BcVramReadRaw chama BcHdpReadCacheInvalidate). Entao uma sequencia de
     * 'vram w' seguida de submissao pode deixar os bytes no buffer de escrita
     * do HDP, e a GPU le o que estava la antes.
     *
     * Medido em 05/09 11:51: com IB montado por 'vram w', a engine carregou
     * IB_BASE corretamente e IB_RPTR andou os 8 dwords -- mas o CONST_FILL nao
     * teve efeito. Ler lixo residual e executa-lo como pacote explica tanto o
     * "nao escreveu" quanto a engine parar em seguida.
     *
     * BcHdpReadCacheInvalidate faz as duas direcoes: HDP_MEM_COHERENCY_FLUSH
     * (0x4A80) para empurrar escrita da CPU, e READ_CACHE_INVALIDATE para a
     * CPU enxergar escrita da GPU.
     */
    else if (_stricmp(argv[1], "hdp") == 0) {
        rc = Bc250HdpInvalidate(&g_Ctx);
        if (rc) {
            printf("falha no HDP: erro %d (rode 'arm write' antes)\n", rc);
        } else {
            printf("HDP: flush de escrita da CPU + invalidacao da cache de leitura\n");
        }
    }
    else if (_stricmp(argv[1], "psp") == 0)      rc = CmdPsp(argc, argv);
    else if (_stricmp(argv[1], "audit") == 0)    rc = Bc250RunAudit(&g_Ctx);
    else if (_stricmp(argv[1], "stress") == 0)   rc = Bc250RunStress(&g_Ctx);
    else if (_stricmp(argv[1], "gcvm") == 0)     rc = CmdGcvm(argc, argv);
    else if (_stricmp(argv[1], "cp") == 0)       rc = CmdCp(argc, argv);
    else if (_stricmp(argv[1], "rlc") == 0)      rc = CmdRlc(argc, argv);
    else if (_stricmp(argv[1], "gfx") == 0)      rc = CmdGfx(argc, argv);
    else if (_stricmp(argv[1], "ring") == 0)     rc = CmdRing(argc, argv);
    else { Usage(); rc = 1; }

    Bc250Close(&g_Ctx);
    return rc;
}
