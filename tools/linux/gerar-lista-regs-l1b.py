#!/usr/bin/env python3
"""Gera tools/linux/bc250-l1b-regs.txt (doc 663): registradores lidos no Linux
pela L1b, com offset em bytes na BAR de registradores e proveniencia.

Offset = (BASE[seg] + mm) * 4, BASE de inc/linux_ref/cyan_skillfish_ip_offset.h
e mm/BASE_IDX dos headers de offset do upstream (Lei 7). Nada e' escrito: a
lista so' alimenta leituras pelo debugfs amdgpu_regs.

Rodar no Windows, offline:  python tools/linux/gerar-lista-regs-l1b.py
"""
import pathlib
import re

RAIZ = pathlib.Path(__file__).resolve().parents[2]
REF = RAIZ / "inc" / "linux_ref"
SAIDA = pathlib.Path(__file__).resolve().parent / "bc250-l1b-regs.txt"

BASES = {  # cyan_skillfish_ip_offset.h, instancia 0
    "GC": [0x00001260, 0x0000A000],
    "MMHUB": [0x0001A000],
    "OSSSYS": [0x000010A0],
    "NBIO": [0x00000000, 0x00000014, 0x00000D20, 0x00010400],
}
HEADERS = {
    "GC": "gc_10_1_0_offset.h",
    "MMHUB": "mmhub_2_0_0_offset.h",
    "OSSSYS": "osssys_5_0_0_offset.h",
    "NBIO": "nbio_2_3_offset.h",
}

# Nomes exatos e padroes. Excluidos: portas de dados/indice com efeito
# colateral (*_DATA, *_IND_*, *_INDEX exceto GRBM_GFX_INDEX, FIFOs e contadores
# de desempenho), que nao se le "de passagem".
ESCOLHA = {
    "GC": [
        r"GRBM_STATUS", r"GRBM_STATUS2", r"GRBM_STATUS_SE[0-3]", r"GRBM_GFX_INDEX",
        r"GRBM_CNTL", r"GRBM_READ_TIMEOUT",
        r"CP_STAT", r"CP_CPC_STATUS", r"CP_CPF_STATUS", r"CP_CPC_BUSY_STAT", r"CP_CPF_BUSY_STAT",
        r"CP_ME_CNTL", r"CP_MEC_CNTL", r"CP_INT_CNTL_RING0",
        r"CP_RB_DOORBELL_RANGE_(LOWER|UPPER)", r"CP_MEC_DOORBELL_RANGE_(LOWER|UPPER)",
        r"CP_RB_DOORBELL_CONTROL", r"CP_RB0_CNTL", r"CP_RB0_BASE", r"CP_RB0_BASE_HI",
        r"CP_RB0_RPTR", r"CP_RB0_WPTR", r"CP_SD_CNTL", r"CP_DEBUG",
        r"RLC_CNTL", r"RLC_STAT", r"RLC_GPM_STAT", r"RLC_PG_CNTL", r"RLC_CGCG_CGLS_CTRL",
        r"RLC_CGCG_RAMP_CTRL", r"RLC_CGTT_MGCG_OVERRIDE", r"RLC_SAFE_MODE", r"RLC_SPM_MC_CNTL",
        r"RLC_PG_DELAY.*", r"RLC_AUTO_PG_CTRL", r"RLC_LB_CNTL", r"RLC_GPU_CLOCK_COUNT_LSB",
        r"SDMA[01]_(CNTL|CHICKEN_BITS|CHICKEN_BITS_2|CLK_CTRL|POWER_CNTL|UTCL1_CNTL|UTCL1_PAGE|"
        r"STATUS_REG|STATUS1_REG|STATUS2_REG|STATUS3_REG|F32_CNTL|SEM_WAIT_FAIL_TIMER_CNTL|"
        r"GFX_RB_CNTL|GFX_RB_BASE|GFX_RB_RPTR|GFX_RB_WPTR|GFX_DOORBELL|GFX_DOORBELL_OFFSET|"
        r"GFX_IB_CNTL|GFX_CONTEXT_STATUS|GFX_MINOR_PTR_UPDATE|RLC_CGCG_CTRL|PROGRAM|"
        r"PUB_DUMMY_REG[0-3]|ERROR_LOG|BA_THRESHOLD|ID|VERSION)",
        r"GCVM_L2_CNTL[2-5]?", r"GCVM_L2_PROTECTION_FAULT_(CNTL|CNTL2|STATUS|ADDR_LO32|ADDR_HI32|"
        r"DEFAULT_ADDR_LO32|DEFAULT_ADDR_HI32)",
        r"GCVM_L2_STATUS", r"GCVM_DUMMY_PAGE_FAULT_CNTL",
        r"GCVM_CONTEXT([0-9]|1[0-5])_CNTL",
        r"GCVM_CONTEXT([0-9]|1[0-5])_PAGE_TABLE_BASE_ADDR_(LO|HI)32",
        r"GCVM_CONTEXT([0-9]|1[0-5])_PAGE_TABLE_(START|END)_ADDR_(LO|HI)32",
        r"GCVM_INVALIDATE_ENG0_(REQ|ACK|SEM)",
        r"GCMC_VM_(FB_LOCATION_BASE|FB_LOCATION_TOP|FB_OFFSET|AGP_BASE|AGP_BOT|AGP_TOP|"
        r"SYSTEM_APERTURE_LOW_ADDR|SYSTEM_APERTURE_HIGH_ADDR|SYSTEM_APERTURE_DEFAULT_ADDR_(LSB|MSB)|"
        r"MX_L1_TLB_CNTL|CACHEABLE_DRAM_ADDRESS_START|CACHEABLE_DRAM_ADDRESS_END|"
        r"LOCAL_HBM_ADDRESS_START|LOCAL_HBM_ADDRESS_END|LOCAL_HBM_ADDRESS_LOCK_CNTL)",
        r"CGTT_.*", r".*_CGTT_.*CTRL.*", r"GCEA_.*CGTT.*",
        r"GE_FAST_CLKS", r"CB_HW_CONTROL_[34]", r"DB_DEBUG[34]?", r"DB_LAST_OF_BURST_CONFIG",
        r"GB_ADDR_CONFIG", r"GCR_GENERAL_CNTL", r"PA_SC_ENHANCE", r"PA_SC_BINNER_EVENT_CNTL_0",
        r"RMI_SPARE", r"SQ_ARB_CONFIG", r"TA_CNTL_AUX", r"UTCL1_CTRL", r"GL2C_CTRL[2-4]?",
        r"GL2C_ADDR_MATCH_MASK", r"GL2A_ADDR_MATCH_MASK", r"GL2_PIPE_STEER_[0-3]",
        r"GL1_PIPE_STEER", r"GL1_DRAM_BURST_CTRL", r"CH_PIPE_STEER", r"CH_DRAM_BURST_CTRL", r"CH_VC5_ENABLE",
        r"GCEA_DRAM_(RD|WR)_CAM_CNTL", r"GCEA_SDP_(ARB_FINAL|TAG_RESERVE[0-3]|VCC_RESERVE[0-1]|IO_PRIORITY|ENABLE)",
        r"GCEA_MISC", r"GCEA_LATENCY_SAMPLING", r"GCEA_PROBE_CNTL",
    ],
    "MMHUB": [
        r"MMVM_L2_CNTL[2-5]?", r"MMVM_L2_PROTECTION_FAULT_(CNTL|CNTL2|STATUS|ADDR_LO32|ADDR_HI32)",
        r"MMVM_L2_STATUS", r"MMVM_CONTEXT([0-9]|1[0-5])_CNTL",
        r"MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_(LO|HI)32",
        r"MMVM_CONTEXT0_PAGE_TABLE_(START|END)_ADDR_(LO|HI)32",
        r"MMMC_VM_(FB_LOCATION_BASE|FB_LOCATION_TOP|FB_OFFSET|AGP_BASE|AGP_BOT|AGP_TOP|"
        r"SYSTEM_APERTURE_LOW_ADDR|SYSTEM_APERTURE_HIGH_ADDR|SYSTEM_APERTURE_DEFAULT_ADDR_(LSB|MSB)|"
        r"MX_L1_TLB_CNTL|LOCAL_HBM_ADDRESS_START|LOCAL_HBM_ADDRESS_END)",
        r"MMEA0_.*CGTT.*", r"MMEA0_DRAM_(RD|WR)_CAM_CNTL", r"MMEA0_SDP_ARB_FINAL", r"MMEA0_MISC",
        r"MM_ATC_L2_CNTL", r"MM_ATC_L2_MISC_CG", r"DAGB0_CNTL_MISC2", r"DAGB0_(RD|WR)_CGTT_CLK_CTRL",
    ],
    "OSSSYS": [
        r"IH_RB_CNTL", r"IH_RB_BASE", r"IH_RB_BASE_HI", r"IH_RB_RPTR", r"IH_RB_WPTR",
        r"IH_DOORBELL_RPTR", r"IH_CNTL", r"IH_CNTL2", r"IH_STATUS", r"IH_CLK_CTRL",
        r"IH_CHICKEN", r"IH_INT_FLOOD_CNTL", r"IH_STORM_CLIENT_LIST_CNTL", r"IH_PERFMON_CNTL",
    ],
    "NBIO": [
        r"RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN", r"BIF_DOORBELL_CNTL", r"BIF_FB_EN",
        r"BIF_BX_PF_.*DOORBELL.*", r"RCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE", r"BIF_BX_PF_.*GPU_HDP_FLUSH_REQ",
    ],
}
PROIBIDO = re.compile(r"(_DATA|_IND_|_INDEX$|FIFO|PERFCOUNTER|PERFMON_SEL|_ADDR_DATA|SRBM_GFX_CNTL)")


def ler_header(nome):
    regs, idx = {}, {}
    for linha in (REF / nome).read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"#define\s+mm(\w+)\s+(0x[0-9a-fA-F]+|\d+)\s*$", linha)
        if not m:
            continue
        nome_reg, valor = m.group(1), int(m.group(2), 0)
        if nome_reg.endswith("_BASE_IDX"):
            idx[nome_reg[: -len("_BASE_IDX")]] = valor
        else:
            regs[nome_reg] = valor
    return regs, idx


def golden():
    """Os 34 golden settings (G.1), offsets ja em bytes no contrato do driver."""
    txt = (RAIZ / "driver/core/bc250_golden_settings_contract.c").read_text(encoding="utf-8")
    for m in re.finditer(r'\{\s*"(\w+)",\s*(\d)u,\s*(0x[0-9A-Fa-f]+)u', txt):
        yield m.group(1), int(m.group(3), 16)


def dcn():
    txt = (RAIZ / "driver/core/bc250_dcn_probe_contract.h").read_text(encoding="utf-8")
    for m in re.finditer(r"#define BC250_DCN_REG_(OTG0|HUBP0|HUBPREQ0)(\w*)\s+(0x[0-9A-Fa-f]+)u", txt):
        yield m.group(1) + m.group(2), int(m.group(3), 16)


def main():
    linhas, vistos = [], set()
    for ip, padroes in ESCOLHA.items():
        regs, idx = ler_header(HEADERS[ip])
        alvo = re.compile("^(" + "|".join(padroes) + ")$")
        for nome in sorted(regs):
            if not alvo.match(nome) or PROIBIDO.search(nome) and nome != "GRBM_GFX_INDEX":
                continue
            seg = idx.get(nome)
            if seg is None or seg >= len(BASES[ip]):
                continue
            off = (BASES[ip][seg] + regs[nome]) * 4
            if off in vistos:
                continue
            vistos.add(off)
            linhas.append(f"{ip}\t{nome}\t0x{off:06X}\t{HEADERS[ip]} mm{nome}=0x{regs[nome]:04x} BASE_IDX={seg}")
    for nome, off in golden():
        if off not in vistos:
            vistos.add(off)
            linhas.append(f"GOLDEN\t{nome}\t0x{off:06X}\tbc250_golden_settings_contract.c")
    for nome, off in dcn():
        if off not in vistos:
            vistos.add(off)
            linhas.append(f"DCN\t{nome}\t0x{off:06X}\tbc250_dcn_probe_contract.h (DMU_BASE seg2)")
    cab = ("# bc250-l1b-regs.txt -- gerado por gerar-lista-regs-l1b.py (doc 663). NAO EDITAR A MAO.\n"
           "# bloco<TAB>nome<TAB>offset_bytes_na_BAR<TAB>proveniencia. So' leitura (debugfs amdgpu_regs).\n")
    SAIDA.write_bytes((cab + "\n".join(linhas) + "\n").encode("utf-8"))
    por_bloco = {}
    for l in linhas:
        por_bloco[l.split("\t")[0]] = por_bloco.get(l.split("\t")[0], 0) + 1
    print(f"{len(linhas)} registradores -> {SAIDA}  {por_bloco}")


if __name__ == "__main__":
    main()
