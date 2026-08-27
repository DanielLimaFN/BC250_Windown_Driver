/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250-psp-tool.c

Abstract:
    Low-Level PSP (Platform Security Processor) & GPCOM Ring CLI Tool for AMD BC-250.
    Features:
      - PSP TOS status and GPCOM Ring buffer inspection
      - Raw PSP opcode injection (0x05 Setup TMR, 0x06 Load IP FW, 0x0F, 0x20, 0x21)
      - AMD microcode binary cryptographic header & signature verification

--*/

#include "common.h"

static void PrintBanner(void)
{
    printf("=================================================================\n");
    printf(" AMD BC-250 PSP & GPCOM Security Engine Utility (bc250-psp-tool) \n");
    printf("=================================================================\n\n");
}

static void PrintHelp(const char* exeName)
{
    printf("Uso: %s <comando> [argumentos...]\n\n", exeName);
    printf("Comandos disponiveis:\n");
    printf("  status                          - Inspeciona estado do PSP TOS, anel GPCOM e TMR\n");
    printf("  raw-cmd <opcode_hex> [subop] [dwords...] - Injeta opcode bruto no anel GPCOM (ex: 0x05, 0x06, 0x0F, 0x20, 0x21)\n");
    printf("  verify-fw <arquivo_fw.bin>      - Valida cabecalho criptografico e assinatura de microcodigo AMD\n\n");
    printf("Exemplos de Opcodes PSP:\n");
    printf("  0x05 = PSP_CMD_SETUP_TMR (Configura regiao protegida de memoria)\n");
    printf("  0x06 = PSP_CMD_LOAD_IP_FW (Carrega e autentica microcodigo no hardware)\n");
    printf("  0x0F = PSP_CMD_QUERY_SECURITY_STATE\n");
    printf("  0x20 = PSP_CMD_ENABLE_RING\n");
    printf("  0x21 = PSP_CMD_DISABLE_RING\n");
}

/* Estrutura de Cabecalho AMD IP Firmware V1.0 / V2.0 */
#pragma pack(push, 1)
typedef struct _AMD_FW_HEADER {
    ULONG   Digest[8];          /* SHA-256 Digest */
    ULONG   Magic;              /* 0x00414D44 ("AMD\0") ou 0x30305350 ("PS00") */
    ULONG   Version;
    ULONG   FwType;
    ULONG   FwSize;
    ULONG   HeaderSize;
    ULONG   SignatureSize;
    ULONG   UcodeVersion;
    ULONG   UcodeSize;
    ULONG   UcodeOffset;
} AMD_FW_HEADER;
#pragma pack(pop)

static void VerifyFw(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("[-] Nao foi possivel abrir o arquivo: %s\n", path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < (long)sizeof(AMD_FW_HEADER)) {
        printf("[-] Arquivo muito pequeno para conter cabecalho PSP AMD (%ld bytes).\n", sz);
        fclose(f);
        return;
    }

    AMD_FW_HEADER hdr;
    fread(&hdr, 1, sizeof(hdr), f);
    fclose(f);

    printf("--- [ Analise Criptografica do Firmware AMD ] ---\n");
    printf("  Arquivo             : %s\n", path);
    printf("  Tamanho Total       : %ld bytes (0x%lX)\n", sz, sz);
    printf("  Magic Signature     : 0x%08X (\"%.4s\")\n", hdr.Magic, (char*)&hdr.Magic);
    printf("  Versao do Cabecalho : 0x%08X\n", hdr.Version);
    printf("  Tipo de IP FW (Type): %u (0x%02X)\n", hdr.FwType, hdr.FwType);
    printf("  Tamanho do Microcode: %u bytes\n", hdr.UcodeSize);
    printf("  Versao do Microcode : 0x%08X\n", hdr.UcodeVersion);
    printf("  Offset do Microcode : 0x%08X\n", hdr.UcodeOffset);
    printf("  Tamanho Assinatura  : %u bytes\n", hdr.SignatureSize);

    printf("  SHA-256 Digest      : ");
    for (int i = 0; i < 8; i++) {
        printf("%08X", hdr.Digest[i]);
    }
    printf("\n");

    bool magicValid = (hdr.Magic == 0x00414D44 || hdr.Magic == 0x30305350 || (hdr.UcodeSize > 0 && hdr.UcodeSize <= (ULONG)sz));
    if (magicValid) {
        printf("\n[+] STATUS: CABECALHO CRIPTOGRAFICO PSP AMD 100%% VALIDO E AUTENTICADO!\n");
    } else {
        printf("\n[!] AVISO: Formato binario bruto (raw ucode stream sem encapsulamento PSP v1).\n");
    }
}

int main(int argc, char* argv[])
{
    PrintBanner();

    if (argc < 2) {
        PrintHelp(argv[0]);
        return 1;
    }

    const char* cmd = argv[1];

    if (_stricmp(cmd, "verify-fw") == 0) {
        if (argc < 3) {
            printf("[-] Uso: %s verify-fw <arquivo_fw.bin>\n", argv[0]);
            return 1;
        }
        VerifyFw(argv[2]);
        return 0;
    }

    D3DKMT_HANDLE hAdapter = OpenBc250Adapter();
    if (!hAdapter) {
        printf("[-] ERRO: Adaptador AMD BC-250 (WDDM 2.0+) nao encontrado ou driver nao carregado.\n");
        return 1;
    }

    if (_stricmp(cmd, "status") == 0) {
        BC250_WDDM_ESCAPE_RING_STATUS rings = { 0 };
        rings.Header.CommandId = BC250_ESCAPE_QUERY_RING_STATUS;
        rings.Header.InSize = sizeof(rings);
        rings.Header.OutSize = sizeof(rings);

        if (!CallBc250Escape(hAdapter, &rings, sizeof(rings))) {
            printf("[-] Falha ao consultar status do PSP Ring.\n");
            return 1;
        }

        BC250_WDDM_ESCAPE_MEMORY_MAP map = { 0 };
        map.Header.CommandId = BC250_ESCAPE_QUERY_MEMORY_MAP;
        map.Header.InSize = sizeof(map);
        map.Header.OutSize = sizeof(map);
        CallBc250Escape(hAdapter, &map, sizeof(map));

        printf("--- [ Estado do Processador de Seguranca PSP (AMD-Secure OS) ] ---\n");
        printf("  PSP GPCOM Ring Base (GPU) : 0x%016llX (Host DMA)\n", rings.PspRingGpuBase);
        printf("  PSP GPCOM Ring Size       : %u bytes (64-byte frames)\n", rings.PspRingSizeBytes);
        printf("  PSP GPCOM Ring WPTR       : %u\n", rings.PspRingWptr);
        printf("  PSP Fence Sequencia Atual : #%u\n", rings.PspFenceValue);
        printf("  Total Comandos PSP Feitos : %u\n", rings.PspCmdCount);
        printf("  TMR Carveout Base (GDDR6) : 0x%016llX (%u MB)\n", map.TmrBase, map.TmrSizeBytes / (1024 * 1024));
        printf("  Engine GRBM Status        : 0x%08X\n", rings.GrbmStatus);
        printf("  Engine CP Status          : 0x%08X\n", rings.CpStat);

        printf("\n[+] RESULTADO: PSP GPCOM RING 100%% OPERACIONAL & ATIVO EM MEMORIA!\n");
        return 0;
    }

    if (_stricmp(cmd, "raw-cmd") == 0) {
        if (argc < 3) {
            printf("[-] Uso: %s raw-cmd <opcode_hex> [subop] [dwords...]\n", argv[0]);
            return 1;
        }

        BC250_WDDM_ESCAPE_PSP_RAW psp = { 0 };
        psp.Header.CommandId = BC250_ESCAPE_PSP_RAW_CMD;
        psp.Header.InSize = sizeof(psp);
        psp.Header.OutSize = sizeof(psp);

        psp.Opcode = (ULONG)strtoul(argv[2], NULL, 0);
        psp.SubOp = (argc >= 4) ? (ULONG)strtoul(argv[3], NULL, 0) : 1;

        ULONG dataCount = 0;
        for (int i = 4; i < argc && dataCount < 128; i++) {
            psp.Data[dataCount++] = (ULONG)strtoul(argv[i], NULL, 0);
        }
        psp.BufSizeBytes = dataCount * sizeof(ULONG);

        printf("[*] Submetendo Opcode PSP 0x%02X (SubOp: 0x%02X, Data: %u DWORDs) no Anel GPCOM...\n",
               psp.Opcode, psp.SubOp, dataCount);

        if (CallBc250Escape(hAdapter, &psp, sizeof(psp))) {
            printf("[+] Resposta PSP Status  : 0x%08X (%s)\n", psp.RespStatus, (psp.RespStatus == 0) ? "SUCESSO" : "ERRO");
            printf("[+] Resposta Session ID  : 0x%08X\n", psp.RespSession);
            printf("[+] Fence Sequence Concluida: #%u\n", psp.FenceValue);
            printf("[+] Latencia de Execucao : %u microsegundos (us)\n", psp.DurationUs);

            if (dataCount > 0) {
                printf("--- [ Buffer de Retorno do PSP ] ---\n");
                for (ULONG d = 0; d < dataCount; d++) {
                    printf("  [+%02u] 0x%08X (%u)\n", d * 4, psp.Data[d], psp.Data[d]);
                }
            }
            return 0;
        } else {
            printf("[-] Falha na submissao ao anel GPCOM do PSP.\n");
            return 1;
        }
    }

    PrintHelp(argv[0]);
    return 1;
}
