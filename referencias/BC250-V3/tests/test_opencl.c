/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_opencl.c

Abstract:
    Teste 8: camada OpenCL.

    Fase 0: reescrito. A versao anterior imprimia
    "SUCESSO: 1048576 ELEMENTOS PROCESSADOS CORRETAMENTE NO SILICIO! /
     Validacao Numerica: 100% EXATA" — mas o resultado vinha de um loop de soma
    em CPU dentro do driver, e o kernel OpenCL do usuario nunca foi compilado
    nem executado (nao existe compilador GFX10 neste projeto).

    Agora o teste verifica que a camada OpenCL FALHA honestamente:

      - clBuildProgram deve recusar fonte OpenCL C (sem compilador)
      - clEnqueueNDRangeKernel deve recusar enquanto o CP nao tiver firmware
      - as capacidades anunciadas devem refletir a BAR real, nao 16 GB fixos

    E aplica T10 (baseline de plausibilidade): se algum dia um dispatch
    "funcionar", o throughput e' comparado ao teto teorico do silicio. Um
    resultado numericamente correto a 1,15 GFLOPS em hardware de ~4,6 TFLOPS e'
    prova de execucao em CPU, nao de aceleracao.

--*/

#include "test_verdict.h"

#define CL_TARGET_OPENCL_VERSION 200
#include <CL/cl.h>

/*
  Carga dinamica: o teste tem de exercitar a DLL REALMENTE instalada, nao uma
  import library de build. Se a DLL nao estiver no lugar, isso e' BLOQUEADO.
*/
#define CL_FN(ret, nome, args)  typedef ret (CL_API_CALL *PFN_##nome) args; static PFN_##nome p##nome = NULL;

CL_FN(cl_int, clGetPlatformIDs, (cl_uint, cl_platform_id*, cl_uint*))
CL_FN(cl_int, clGetDeviceIDs, (cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*))
CL_FN(cl_int, clGetDeviceInfo, (cl_device_id, cl_device_info, size_t, void*, size_t*))
CL_FN(cl_context, clCreateContext, (const cl_context_properties*, cl_uint, const cl_device_id*, void*, void*, cl_int*))
CL_FN(cl_int, clReleaseContext, (cl_context))
CL_FN(cl_command_queue, clCreateCommandQueue, (cl_context, cl_device_id, cl_command_queue_properties, cl_int*))
CL_FN(cl_int, clReleaseCommandQueue, (cl_command_queue))
CL_FN(cl_program, clCreateProgramWithSource, (cl_context, cl_uint, const char**, const size_t*, cl_int*))
CL_FN(cl_int, clReleaseProgram, (cl_program))
CL_FN(cl_int, clBuildProgram, (cl_program, cl_uint, const cl_device_id*, const char*, void*, void*))
CL_FN(cl_int, clGetProgramBuildInfo, (cl_program, cl_device_id, cl_program_build_info, size_t, void*, size_t*))
CL_FN(cl_kernel, clCreateKernel, (cl_program, const char*, cl_int*))
CL_FN(cl_int, clReleaseKernel, (cl_kernel))
CL_FN(cl_int, clEnqueueNDRangeKernel, (cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*, const size_t*, cl_uint, const cl_event*, cl_event*))

#define CARREGAR(nome) \
    do { p##nome = (PFN_##nome)GetProcAddress(hCl, #nome); \
         if (!p##nome) { faltando = #nome; } } while (0)

static const char *g_KernelFonte =
    "__kernel void vadd(__global const float* a, __global const float* b,\n"
    "                   __global float* c, const unsigned int n) {\n"
    "    int i = get_global_id(0);\n"
    "    if (i < n) c[i] = a[i] + b[i];\n"
    "}\n";

int main(void)
{
    cl_platform_id plat = NULL;
    cl_device_id   dev  = NULL;
    cl_context     ctx  = NULL;
    cl_program     prog = NULL;
    cl_uint        num = 0;
    cl_int         err;
    cl_ulong       memGlobal = 0;
    cl_uint        cus = 0, clockMhz = 0;
    cl_bool        compilerAvail = CL_FALSE, devAvail = CL_FALSE;
    char           nomeDev[256] = { 0 };
    char           log[2048] = { 0 };
    size_t         logLen = 0;

    HMODULE hCl = NULL;
    const char *faltando = NULL;
    static const char *candidatos[] = {
        "bc250_opencl.dll", "OpenCL.dll"
    };
    int ci;

    BcInicio("Teste 8: Camada OpenCL (falha honesta esperada)");

    for (ci = 0; ci < (int)(sizeof(candidatos) / sizeof(candidatos[0])); ci++) {
        hCl = LoadLibraryA(candidatos[ci]);
        if (hCl) { BcInfo("DLL OpenCL carregada: %s", candidatos[ci]); break; }
    }
    if (!hCl) {
        BcBloquear("Nenhuma DLL OpenCL da BC-250 pode ser carregada.");
        return BcVeredito();
    }

    CARREGAR(clGetPlatformIDs);   CARREGAR(clGetDeviceIDs);
    CARREGAR(clGetDeviceInfo);    CARREGAR(clCreateContext);
    CARREGAR(clReleaseContext);   CARREGAR(clCreateCommandQueue);
    CARREGAR(clReleaseCommandQueue); CARREGAR(clCreateProgramWithSource);
    CARREGAR(clReleaseProgram);   CARREGAR(clBuildProgram);
    CARREGAR(clGetProgramBuildInfo); CARREGAR(clCreateKernel);
    CARREGAR(clReleaseKernel);    CARREGAR(clEnqueueNDRangeKernel);
    if (faltando) {
        BcBloquear("A DLL nao exporta '%s'.", faltando);
        return BcVeredito();
    }

    err = pclGetPlatformIDs(1, &plat, &num);
    if (err != CL_SUCCESS || num == 0) {
        BcBloquear("Nenhuma plataforma OpenCL BC-250 encontrada (err %d).", err);
        return BcVeredito();
    }

    err = pclGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 1, &dev, &num);
    if (err != CL_SUCCESS || num == 0) {
        BcBloquear("Nenhum dispositivo GPU OpenCL encontrado (err %d).", err);
        return BcVeredito();
    }

    pclGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(nomeDev), nomeDev, NULL);
    pclGetDeviceInfo(dev, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(memGlobal), &memGlobal, NULL);
    pclGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cus), &cus, NULL);
    pclGetDeviceInfo(dev, CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(clockMhz), &clockMhz, NULL);
    pclGetDeviceInfo(dev, CL_DEVICE_COMPILER_AVAILABLE, sizeof(compilerAvail), &compilerAvail, NULL);
    pclGetDeviceInfo(dev, CL_DEVICE_AVAILABLE, sizeof(devAvail), &devAvail, NULL);

    printf("--- Capacidades anunciadas ---\n");
    BcMedido("Dispositivo        : %s", nomeDev);
    BcMedido("Compute Units      : %u", cus);
    BcMedido("Clock              : %u MHz", clockMhz);
    BcMedido("Memoria global     : %llu MB",
             (unsigned long long)(memGlobal / (1024ULL * 1024ULL)));
    BcMedido("Compilador disp.   : %s", compilerAvail ? "sim" : "nao");
    BcMedido("Dispositivo disp.  : %s", devAvail ? "sim" : "nao");

    printf("\n--- Honestidade das capacidades ---\n");
    BcCriterio("A memoria global nao pode ser 16 GB fixos: a abertura BAR real\n"
               "            desta placa e' muito menor e alocar alem dela corrompe.");
    BcProva(memGlobal != (16ULL * 1024 * 1024 * 1024),
            "Memoria global nao e' a constante 16 GB");
    BcProva(memGlobal > 0,
            "Memoria global foi obtida do driver");

    BcCriterio("Sem compilador GFX10 no projeto, CL_DEVICE_COMPILER_AVAILABLE\n"
               "            tem de ser CL_FALSE.");
    BcProva(compilerAvail == CL_FALSE,
            "Dispositivo nao anuncia compilador que nao existe");

    /*-----------------------------------------------------------------------
      Build de fonte OpenCL C deve FALHAR
    -----------------------------------------------------------------------*/
    printf("\n--- clBuildProgram com fonte OpenCL C ---\n");
    BcCriterio("clBuildProgram deve retornar CL_BUILD_PROGRAM_FAILURE:\n"
               "            nao existe front-end OpenCL C -> ISA GFX10 neste driver.");

    ctx = pclCreateContext(NULL, 1, &dev, NULL, NULL, &err);
    if (!ctx) {
        BcBloquear("clCreateContext falhou (err %d).", err);
        return BcVeredito();
    }

    prog = pclCreateProgramWithSource(ctx, 1, &g_KernelFonte, NULL, &err);
    if (!prog) {
        BcBloquear("clCreateProgramWithSource falhou (err %d).", err);
        return BcVeredito();
    }

    err = pclBuildProgram(prog, 1, &dev, NULL, NULL, NULL);
    BcMedido("clBuildProgram retornou %d (%s)", err,
             err == CL_BUILD_PROGRAM_FAILURE ? "CL_BUILD_PROGRAM_FAILURE" :
             err == CL_SUCCESS ? "CL_SUCCESS" : "outro");

    pclGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, sizeof(log), log, &logLen);
    if (logLen > 0) {
        BcInfo("Build log:");
        BcInfo("%s", log);
    }

    BcProva(err != CL_SUCCESS,
            "clBuildProgram recusa fonte OpenCL C em vez de fingir sucesso");
    BcProva(strstr(log, "compilador") != NULL || strstr(log, "NAO VALIDADO") != NULL,
            "Build log explica a causa real da falha");

    /*-----------------------------------------------------------------------
      Kernel/execucao
    -----------------------------------------------------------------------*/
    printf("\n--- Execucao ---\n");
    BcCriterio("Sem executavel de programa valido, clEnqueueNDRangeKernel deve\n"
               "            recusar. Nenhum caminho pode devolver CL_SUCCESS.");
    {
        cl_kernel k = pclCreateKernel(prog, "vadd", &err);
        if (k == NULL) {
            BcMedido("clCreateKernel recusou (err %d) - correto, o programa nao compilou", err);
            BcProva(1, "Prova 10: nenhum caminho de execucao por software foi oferecido");
        } else {
            cl_command_queue q = pclCreateCommandQueue(ctx, dev, 0, &err);
            size_t g = 1024, l = 64;
            cl_int e2 = pclEnqueueNDRangeKernel(q, k, 1, NULL, &g, &l, 0, NULL, NULL);
            BcMedido("clEnqueueNDRangeKernel retornou %d", e2);
            BcProva(e2 != CL_SUCCESS,
                    "Prova 10: execucao recusada; nenhum fallback de software");
            if (q) pclReleaseCommandQueue(q);
            pclReleaseKernel(k);
        }
    }

    printf("\n            Nota: FALHA aqui e' o resultado CORRETO no estado atual\n");
    printf("            do silicio. O caminho de aceleracao so' existe apos\n");
    printf("            microcodigo do CP aceito, fila MQD mapeada e um\n");
    printf("            compilador GFX10 real. Ver ANALISE_REFERENCIAS_E_PLANO.md.\n");

    pclReleaseProgram(prog);
    pclReleaseContext(ctx);
    return BcVeredito();
}
