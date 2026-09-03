#include "../wdk/bc250_boot_fuse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_FailedTests = 0;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FALHA no teste [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
        g_FailedTests++; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FALHA no teste [%s:%d]: %s (%lld) != %s (%lld)\n", \
            __FILE__, __LINE__, #a, (long long)(a), #b, (long long)(b)); \
        g_FailedTests++; \
    } \
} while (0)

static void TestChecksumAndValidation(void)
{
    BC250_BOOT_FUSE_RECORD record;
    memset(&record, 0, sizeof(record));

    record.Magic = BC250_FUSE_MAGIC;
    record.Version = BC250_FUSE_VERSION;
    record.State = BC250_FUSE_STATE_CLEAN;
    record.BootCount = 1;
    record.LastStatus = 0;
    record.Flags = 0;
    record.Timestamp.QuadPart = 123456789LL;
    record.Checksum = Bc250BootFuseComputeChecksum(&record);

    ASSERT_TRUE(Bc250BootFuseValidateRecord(&record));

    /* Teste 1: Magic corrompido */
    record.Magic = 0x12345678;
    ASSERT_TRUE(!Bc250BootFuseValidateRecord(&record));
    record.Magic = BC250_FUSE_MAGIC;

    /* Teste 2: Versao invalida */
    record.Version = 2;
    ASSERT_TRUE(!Bc250BootFuseValidateRecord(&record));
    record.Version = BC250_FUSE_VERSION;

    /* Teste 3: Estado invalido */
    record.State = 99;
    ASSERT_TRUE(!Bc250BootFuseValidateRecord(&record));
    record.State = BC250_FUSE_STATE_CLEAN;

    /* Teste 4: Checksum divergente (bit flip) */
    record.BootCount = 2;
    ASSERT_TRUE(!Bc250BootFuseValidateRecord(&record));
    record.Checksum = Bc250BootFuseComputeChecksum(&record);
    ASSERT_TRUE(Bc250BootFuseValidateRecord(&record));
}

static void TestStateMachineTransitions(void)
{
    BC250_BOOT_FUSE_RECORD record;
    BOOLEAN allowStart = FALSE;
    memset(&record, 0, sizeof(record));

    /*
     * Cenário 1: Primeiro boot (arquivo nao existia).
     * Deve inicializar em ARMED e autorizar StartDevice.
     */
    Bc250BootFuseProcessTransition(&record, FALSE, TRUE, 0, &allowStart);
    ASSERT_TRUE(allowStart);
    ASSERT_EQ(record.State, (ULONG)BC250_FUSE_STATE_ARMED);
    ASSERT_EQ(record.BootCount, 1u);
    ASSERT_EQ(record.Magic, BC250_FUSE_MAGIC);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);
    ASSERT_TRUE(Bc250BootFuseValidateRecord(&record));

    /*
     * Cenário 2: Desligamento limpo (StopDevice chamado com sucesso).
     * Deve transicionar de ARMED para CLEAN.
     */
    Bc250BootFuseProcessTransition(&record, TRUE, FALSE, 0, &allowStart);
    ASSERT_EQ(record.State, (ULONG)BC250_FUSE_STATE_CLEAN);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);
    ASSERT_TRUE(Bc250BootFuseValidateRecord(&record));

    /*
     * Cenário 3: Segundo boot apos desligamento limpo.
     * Deve autorizar StartDevice e transicionar de CLEAN para ARMED.
     */
    Bc250BootFuseProcessTransition(&record, TRUE, TRUE, 0, &allowStart);
    ASSERT_TRUE(allowStart);
    ASSERT_EQ(record.State, (ULONG)BC250_FUSE_STATE_ARMED);
    ASSERT_EQ(record.BootCount, 2u);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);
    ASSERT_TRUE(Bc250BootFuseValidateRecord(&record));

    /*
     * Cenário 4: SIMULAÇÃO DE CRASH / HANG / TDR / REBOOT SUJO.
     * O sistema reinicia SEM que StopDevice tenha rodado.
     * Portanto, record.State ainda está ARMED quando StartDevice entra!
     *
     * EXPECTATIVA: O fusível DISPARA (TRIPPED) e RECUSA StartDevice (allowStart == FALSE).
     */
    Bc250BootFuseProcessTransition(&record, TRUE, TRUE, 0, &allowStart);
    ASSERT_TRUE(!allowStart);
    ASSERT_EQ(record.State, (ULONG)BC250_FUSE_STATE_TRIPPED);
    ASSERT_EQ(record.BootCount, 3u);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);
    ASSERT_TRUE(Bc250BootFuseValidateRecord(&record));

    /*
     * Cenário 5: Boot subsequente com fusível já TRIPPED.
     * Deve CONTINUAR RECUSANDO e mantendo TRIPPED.
     */
    Bc250BootFuseProcessTransition(&record, TRUE, TRUE, 0, &allowStart);
    ASSERT_TRUE(!allowStart);
    ASSERT_EQ(record.State, (ULONG)BC250_FUSE_STATE_TRIPPED);
    ASSERT_EQ(record.BootCount, 4u);

    /*
     * Cenário 6: Chamada de StopDevice quando o fusível está TRIPPED.
     * NUNCA deve desarmar para CLEAN por acidente.
     */
    Bc250BootFuseProcessTransition(&record, TRUE, FALSE, 0, &allowStart);
    ASSERT_EQ(record.State, (ULONG)BC250_FUSE_STATE_TRIPPED);

    /*
     * Cenário 7: Arquivo corrompido / truncado no disco.
     * Fail-closed: deve transicionar para TRIPPED e recusar o boot.
     */
    record.Magic = 0xDEADBEEF; /* corrupcao proposital */
    Bc250BootFuseProcessTransition(&record, TRUE, TRUE, 0, &allowStart);
    ASSERT_TRUE(!allowStart);
    ASSERT_EQ(record.State, (ULONG)BC250_FUSE_STATE_TRIPPED);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);
    ASSERT_TRUE(Bc250BootFuseValidateRecord(&record));

    /*
     * Cenário 8: Reset explícito pelo operador / harness.
     * Volta para CLEAN, zerando contadores.
     */
    memset(&record, 0, sizeof(record));
    record.Magic = BC250_FUSE_MAGIC;
    record.Version = BC250_FUSE_VERSION;
    record.State = BC250_FUSE_STATE_CLEAN;
    record.BootCount = 0;
    record.LastStatus = 0;
    record.Checksum = Bc250BootFuseComputeChecksum(&record);
    ASSERT_TRUE(Bc250BootFuseValidateRecord(&record));

    /* Boot apos reset autorizado */
    Bc250BootFuseProcessTransition(&record, TRUE, TRUE, 0, &allowStart);
    ASSERT_TRUE(allowStart);
    ASSERT_EQ(record.State, (ULONG)BC250_FUSE_STATE_ARMED);
    ASSERT_EQ(record.BootCount, 1u);
}

int main(void)
{
    printf("=== BC250 BOOT FUSE UNIT TEST ===\n");
    TestChecksumAndValidation();
    TestStateMachineTransitions();

    if (g_FailedTests == 0) {
        printf("RESULTADO: TODOS OS TESTES APROVADOS (0 falhas)\n");
        return 0;
    } else {
        printf("RESULTADO: %d FALHAS DETECTADAS!\n", g_FailedTests);
        return 1;
    }
}
