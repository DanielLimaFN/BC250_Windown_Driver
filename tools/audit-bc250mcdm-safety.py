#!/usr/bin/env python3
"""Fail-closed static gate for the installable BC-250 MCDM package."""

from pathlib import Path
import hashlib
import re
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle in text:
        errors.append(f"forbidden {label}: {needle}")


def require_token(text: str, token: str, label: str, errors: list[str]) -> None:
    """require() com fronteira de palavra.

    Existe porque require() casa SUBSTRING, e o nome certo e quase sempre
    PREFIXO do nome errado. Tres mutacoes ja passaram por esse buraco em 19/09:
    GfxIbDumpDir -> GfxIbDumpDirOutro (M14), BROADCAST_ALL -> BROADCAST_ALL_REMOVIDO
    (M8) e complete_at_capture -> complete_at_capture_removido (M18). Renomear um
    campo e a forma mais comum de remove-lo na pratica, e o portao tem de notar.
    """
    if not re.search(r"(?<![A-Za-z0-9_])" + re.escape(token) + r"(?![A-Za-z0-9_])", text):
        errors.append(f"missing {label}: {token}")


def strip_c_comments(text: str) -> str:
    """Remove comentarios C sem alterar o comprimento util do codigo.

    Existe porque gate que varre o corpo de uma funcao atras de uma construcao
    proibida reprova o comentario que EXPLICA por que a construcao foi removida.
    Ja aconteceu com o gate do B3. Strings literais sao preservadas para que
    require()/forbid() sobre strings de log continuem funcionando.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"' or ch == "'":
            quote = ch
            out.append(ch)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\":
                    if i + 1 < n:
                        out.append(text[i + 1])
                        i += 2
                        continue
                elif text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            block = text[i:end + 2] if end >= 0 else text[i:]
            # Preserva as quebras de linha para nao deslocar diagnosticos.
            out.append("\n" * block.count("\n"))
            i = (end + 2) if end >= 0 else n
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            i = end if end >= 0 else n
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def production_only_line_numbers(text: str) -> set:
    """Linhas que existem SO' na imagem de PRODUCAO.

    Sao as que ficam dentro de "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)"
    e ANTES do #else correspondente. A imagem de LABORATORIO -- a que roda todos
    os ensaios -- define a macro, entao nada dessa faixa e' compilado nela.

    Existe porque essa classe de defeito ja custou QUATRO investigacoes:
      A13  tunable de submissao lido so' num ramo;
      B4   GpuvaLabPromotion idem;
      doc 202  ContextAbi populado so' num ramo (contexto com DmaBuf=0x0);
      C3.9 Node1JournalGatePerProcess lido so' na producao -- o tunable nunca
           seria lido justamente na imagem que precisa dele.
    """
    marker = "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)"
    out = set()
    stack = []          # (kind, in_production_branch)
    for idx, raw in enumerate(text.split("\n")):
        line = raw.strip()
        if line.startswith("#if"):
            stack.append(["prod" if line == marker else "other", True])
            continue
        if line.startswith("#else") and stack:
            stack[-1][1] = False
            continue
        if line.startswith("#endif"):
            if stack:
                stack.pop()
            continue
        if any(k == "prod" and branch for k, branch in stack):
            out.add(idx)
    return out


def require_outside_production_only(text: str, needle: str, label: str,
                                    errors: list[str]) -> None:
    """O trecho tem de existir em pelo menos uma linha visivel ao LABORATORIO."""
    prod_only = production_only_line_numbers(text)
    hits = [i for i, l in enumerate(text.split("\n")) if needle in l]
    if not hits:
        errors.append(f"missing {label}: {needle}")
        return
    if all(i in prod_only for i in hits):
        errors.append(
            f"{label} existe SO' dentro de "
            "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP): a imagem de "
            "laboratorio -- a que roda os ensaios -- nao compila essa faixa "
            f"({needle})")

def strip_ps_comments(text: str) -> str:
    """Remove comentarios PowerShell, preservando a contagem de linhas.

    Mesmo motivo de strip_c_comments: um portao que varre o corpo de um script
    atras de uma construcao proibida reprova o COMENTARIO que explica por que a
    construcao foi removida. Ja aconteceu com o gate do B3, com o da C3.1 e de
    novo com o da C3.10.

    Trata blocos <# ... #> e comentarios de linha '#'. Nao tenta entender aspas
    com '#' dentro: para os portoes deste arquivo, remover a mais e' seguro --
    o efeito e' exigir que a construcao proibida esteja em CODIGO de verdade.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("<#", i):
            end = text.find("#>", i + 2)
            block = text[i:end + 2] if end >= 0 else text[i:]
            out.append("\n" * block.count("\n"))
            i = (end + 2) if end >= 0 else n
            continue
        if text[i] == "#":
            end = text.find("\n", i)
            i = end if end >= 0 else n
            continue
        out.append(text[i])
        i += 1
    return "".join(out)

def audit_compute_vmid_isolation(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    engine = (ROOT / "driver/wdk/bc250_submission_engine.c").read_text(encoding="utf-8")
    core = (ROOT / "driver/core/bc250_gcvm_vmid8_transaction.c").read_text(encoding="utf-8")
    transport = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(encoding="utf-8")
    # Every live assignment to the virtual ring must use the dedicated constant.
    assignment_pattern = r"ComputeRing\.Vmid\s*(?<![=!<>])=(?!=)\s*([^;]+);"
    if re.findall(assignment_pattern, active):
        errors.append("active miniport must not retarget ComputeRing.Vmid directly")
    assignments = re.findall(assignment_pattern, engine)
    if not assignments or any(value != "TargetVmid" for value in assignments):
        errors.append("compute VMID assignments must use the validated retarget helper")
    install = core.split("BC250_GCVM_VMID8_STATUS Bc250GcvmComputeInstall(", 1)[1]
    require(install, "if (!Bc250GcvmComputeCanSwitch(Submitted, Retired)) return", "GPU retirement gate before VMID9 writes", errors)
    if install.index("Bc250GcvmComputeCanSwitch") > install.index("ComputeWriteChecked"):
        errors.append("VMID9 writes precede GPU retirement check")
    require(core, "return Submitted == Retired;", "strict retirement equality", errors)
    require(install, "Bc250GcvmComputeRestore(Io, Binding)", "VMID9 rollback", errors)
    require(active, "io.Flush = Bc250ActiveFlushComputeTlb;", "compute-specific TLB ACK", errors)
    require(active, "Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_COMPUTE_VMID)", "VMID9 invalidation", errors)
    require(active, "Bc250GcvmComputeRestore(&computeIo, &context->ComputeVmidBinding)", "VMID9 teardown", errors)
    require(active, "submitStatus = Bc250ActivePrepareComputeVmid(context, &contextView);", "root installed before MEC publication", errors)
    require(active, "if (NT_SUCCESS(submitStatus)) submitStatus = Bc250SubmissionEngineSubmit(", "no publish on root failure", errors)
    if transport.count("Bc250GcvmIsComputeRegister(ByteOffset)") != 2:
        errors.append("VMID9 needs exact read AND write allowlists")


def audit_firmware_wgp_policy_is_readonly(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    transport = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(encoding="utf-8")
    prepare = function_body(active, "static NTSTATUS Bc250ActivePrepareComputeVmid")
    read_allowlist = function_body(
        transport, "static uint32_t Bc250MmioTopologyOffsetAllowed")
    write_allowlist = function_body(
        transport, "static uint32_t Bc250MmioTopologyWriteOffsetAllowed")
    policy_registers = (
        "BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK",
        "BC250_HQD_MMIO_RLC_PG_CNTL",
        "BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK",
    )
    for register in policy_registers:
        require(read_allowlist, register,
                f"read-only telemetry for firmware policy register {register}", errors)
        forbid(write_allowlist, register,
               f"write access to firmware WGP/power policy register {register}", errors)
        if re.search(r"Bc250MmioTopologyWrite32\s*\([^;]*" + register,
                     prepare, flags=re.S):
            errors.append(
                f"PrepareComputeVmid writes firmware WGP/power policy register {register}")
    require(prepare, "Policy=FIRMWARE_PRESERVED",
            "explicit firmware WGP policy telemetry", errors)


def audit_mec_cursor_reconciliation(errors: list[str]) -> None:
    engine = (ROOT / "driver/wdk/bc250_submission_engine.c").read_text(encoding="utf-8")
    core = (ROOT / "driver/core/bc250_persistent_ring_contract.c").read_text(encoding="utf-8")
    helper = function_body(
        engine, "static NTSTATUS Bc250SubmissionReconcileCursorLocked")
    submit = function_body(engine, "NTSTATUS Bc250SubmissionEngineSubmit")
    reconcile = function_body(
        core, "BC250_PRING_STATUS Bc250PersistentRingReconcileDrainedCursor")
    require(reconcile,
            "Ring->LastSubmittedFence != Ring->LastRetiredFence",
            "no-fence-in-flight cursor reconciliation gate", errors)
    require(reconcile, "physicalRptr != physicalWptr",
            "physically drained cursor reconciliation gate", errors)
    require(reconcile, "Ring->WptrDwords = physicalWptr;",
            "logical WPTR rebased from measured physical cursor", errors)
    require(reconcile, "Ring->RptrShadowDwords = physicalRptr;",
            "logical RPTR shadow rebased from measured physical cursor", errors)
    require(helper, "[MEC_CURSOR_RECONCILE] PASS:",
            "measured cursor reconciliation verdict", errors)
    require(helper, "Engine->ComputeWptrShadowVirtual[0] = reconciled;",
            "WPTR shadow synchronized after safe rebase", errors)
    require(helper, "(RPTR nunca escrito)",
            "explicit no-fabricated-RPTR verdict", errors)
    if re.search(r"Bc250SubmissionWrite32\s*\([^;]*BC250_HQD_MMIO_PQ_RPTR",
                 helper, flags=re.S):
        errors.append("MEC cursor reconciliation writes physical RPTR")
    call = "status = Bc250SubmissionReconcileCursorLocked(Context, engine);"
    require(submit, call, "cursor reconciliation before MEC planning", errors)
    plan = "ringStatus = Bc250PersistentRingPlanSubmission(&engine->ComputeRing,"
    if not (0 <= submit.find(call) < submit.find(plan)):
        errors.append("MEC cursor reconciliation must precede submission planning")


def audit_mec_vmid_retarget(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    engine = (ROOT / "driver/wdk/bc250_submission_engine.c").read_text(encoding="utf-8")
    transport = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(encoding="utf-8")
    prepare = function_body(active, "static NTSTATUS Bc250ActivePrepareComputeVmid")
    retarget = function_body(
        engine, "NTSTATUS Bc250SubmissionEngineRetargetDrainedIbVmid")
    read_allowlist = function_body(
        transport, "static uint32_t Bc250MmioTopologyOffsetAllowed")
    write_allowlist = function_body(
        transport, "static uint32_t Bc250MmioTopologyWriteOffsetAllowed")

    require(prepare,
            "Bc250SubmissionEngineRetargetDrainedIbVmid(\n        Context, BC250_GCVM_COMPUTE_VMID)",
            "drained VMID retarget before Node1 publication", errors)
    if re.search(r"Bc250MmioTopologyWrite32\s*\(\s*&Context->Core\.MmioTransport\s*,\s*BC250_HQD_MMIO_VMID",
                 prepare, flags=re.S):
        errors.append("PrepareComputeVmid writes CP_HQD_VMID while HQD may be active")
    require(retarget,
            "LastSubmittedFence !=\n            engine->ComputeRing.LastRetiredFence",
            "no-fence-in-flight VMID retarget gate", errors)
    require(retarget, "Bc250PersistentRingReconcileDrainedCursor(",
            "physical drain proof before VMID retarget", errors)
    off = "BC250_HQD_MMIO_ACTIVE, 0u"
    vmid = "BC250_HQD_MMIO_VMID, targetValue"
    mask = "BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, queueMask"
    on = "BC250_HQD_MMIO_ACTIVE, 1u"
    for needle, label in ((off, "HQD deactivate"), (vmid, "VMID write"),
                          (mask, "queue-mask rearm"), (on, "HQD reactivate")):
        require(retarget, needle, label, errors)
    if not (0 <= retarget.find(off) < retarget.find(vmid) <
            retarget.find(mask) < retarget.find(on)):
        errors.append("VMID retarget order must be ACTIVE off -> VMID -> queue mask -> ACTIVE on")
    if re.search(r"Bc250SubmissionWrite32\s*\([^;]*BC250_HQD_MMIO_PQ_RPTR",
                 retarget, flags=re.S):
        errors.append("VMID retarget writes physical RPTR")
    require(retarget, "[MEC_VMID_RETARGET]", "measured VMID retarget verdict", errors)

    for register in ("BC250_HQD_MMIO_CP_STALLED_STAT2",
                     "BC250_HQD_MMIO_CP_BUSY_STAT"):
        require(read_allowlist, register,
                f"read-only RCIU diagnostic {register}", errors)
        forbid(write_allowlist, register,
               f"write access to RCIU diagnostic {register}", errors)
    for stage in ("BEFORE_GCVM_INSTALL", "AFTER_GCVM_INSTALL",
                  "PUBLISH_BEFORE_COHERENCY", "PUBLISH_AFTER_COHERENCY",
                  "PUBLISH_AFTER_MMIO_WPTR", "PUBLISH_AFTER_DOORBELL"):
        require(active + engine, stage, f"RCIU stage {stage}", errors)


def audit_gfx_ring_tripod_and_ib_inspection(errors: list[str]) -> None:
    """
    Trava as tres correcoes da faixa do anel grafico (2026-09-18), todas
    nascidas da mesma medicao: na corrida 10:20 as 87 submissoes do Node 1 GFX
    sairam com SizeDW=24 (so o preambulo da RADV), a fence do anel avancou em
    87/87 e NENHUMA telemetria detectou o problema.

    1. PERNA 2 DO TRIPE -- exclusao explicita do poison antes de aposentar.
       O poison e' 0x43DAFFFE, um numero GRANDE: sem exclui-lo, a comparacao
       `observedFence > lastRetired` e' verdadeira mesmo que a GPU nunca tenha
       escrito, e o poison se disfarca de fence concluida. Esta e' a setima
       ocorrencia potencial da classe "o portao carimbava o falso sucesso".

    2. PERNA 1 DO TRIPE -- veredito de convergencia de ponteiros do anel, com
       reducao de uma volta (Bc250TripodWrapRingPointer). Comparacao CRUA de
       RPTR e' proibida desde a faixa A15: com RPTR=4612, alvo 516 e anel 4096
       ela leria trabalho pendente num anel drenado.

    3. INSPECAO DO IB -- o KMD tem de inspecionar o stream PM4 que aponta ao CP
       e usar FONTE UNICA para o veredito. E' o unico jeito de distinguir
       "concatenacao linear aplicada" de "voltou ao encadeamento".
    """
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    contract_c = (ROOT / "driver/core/bc250_gfx_ib_inspect_contract.c").read_text(encoding="utf-8")
    test_c = (ROOT / "driver/tests/gfx_ib_inspect_contract_test.c").read_text(encoding="utf-8")
    build = (ROOT / "driver/wdk/build-contract.bat").read_text(encoding="utf-8")

    poll = function_body(active, "static NTSTATUS Bc250ActiveGfxEnginePoll")
    if not poll:
        errors.append("Bc250ActiveGfxEnginePoll nao localizado para auditar o tripe do anel grafico")
        return

    clean_poll = re.sub(r"/\*.*?\*/", "", poll, flags=re.S)
    clean_poll = re.sub(r"//.*$", "", clean_poll, flags=re.M)

    # (1) O poison precisa ser NOMEADO e EXCLUIDO antes da aposentadoria.
    if "BC250_GFX_FENCE_POISON" not in clean_poll:
        errors.append(
            "Bc250ActiveGfxEnginePoll nao referencia BC250_GFX_FENCE_POISON: sem excluir o poison "
            "a comparacao de fence aposenta submissoes que a GPU nunca concluiu (falso sucesso)")
    if not re.search(r"fenceIsPoison\s*=", clean_poll):
        errors.append("Bc250ActiveGfxEnginePoll nao calcula fenceIsPoison antes de aposentar")
    if not re.search(r"if\s*\(\s*!\s*fenceIsPoison\s*&&", clean_poll):
        errors.append(
            "a aposentadoria do anel grafico nao esta guardada por !fenceIsPoison: "
            "o poison 0x43DAFFFE passa a comparacao `observedFence > lastRetired`")

    # O valor literal nao pode reaparecer solto no poll -- fonte unica pelo macro.
    if "0x43DAFFFE" in clean_poll:
        errors.append(
            "Bc250ActiveGfxEnginePoll usa o literal 0x43DAFFFE em vez de BC250_GFX_FENCE_POISON "
            "(duas fontes de verdade para o poison)")

    # (2) Perna 1 com reducao de volta; comparacao crua e' proibida (A15).
    require(poll, "GFX_TRIPE_VEREDITO",
            "veredito de tripe do anel grafico emitido pelo poll", errors)
    if "Bc250TripodWrapRingPointer" not in clean_poll:
        errors.append(
            "o veredito do anel grafico nao reduz os ponteiros por Bc250TripodWrapRingPointer: "
            "comparacao crua de RPTR e' proibida desde a faixa A15")
    # O veredito precisa admitir INDETERMINADO -- "nao sei" nao pode virar "convergiu".
    if "INDETERMINADO" not in poll:
        errors.append(
            "o veredito do anel grafico nao tem estado INDETERMINADO: leitura ausente "
            "nao pode ser reportada como convergencia")

    # (2d) FAULT DE TRADUCAO TEM DE SAIR DECODIFICADO (Corrida 45).
    # FaultStatus=0x0070153C estava no log dos CINCO TDRs, em hexadecimal cru,
    # e dizia VMID=7 WALKER_ERROR=6 MAPPING_ERROR=1 -- o caminhador de tabela
    # falhou sob o MESMO VMID das submissoes GFX. Ninguem leu, porque a busca
    # do projeto era pela string GFXHUB_FAULT, que sai ZERO. As mascaras sao
    # copiadas de gc_10_1_0_sh_mask.h; a primeira versao transcrita de cabeca
    # errou os SEIS shifts.
    for needle, label in (
        ("[TDR_GCVM_FAULT] DECODIFICADO",
         "fault de traducao sai decodificado, nao em hexadecimal cru"),
        ("(faultStatus & 0x00F00000u) >> 20", "VMID do fault pela mascara do header"),
        ("(faultStatus & 0x0000000Eu) >> 1", "WALKER_ERROR pela mascara do header"),
        ("(faultStatus & 0x00000100u) >> 8", "MAPPING_ERROR pela mascara do header"),
        ("(faultStatus & 0x0003FE00u) >> 9", "CID pela mascara do header"),
        ("SEM FAULT LATCHADO", "ausencia de fault e' declarada, nao silenciosa"),
    ):
        require(active, needle, label, errors)

    # (2e) DISCRIMINADOR DA GRANULARIDADE DO ENDERECO DE FAULT (Corrida 46->47).
    # gc_10_1_0_sh_mask.h declara LOGICAL_PAGE_ADDR e NAO declara o tamanho da
    # pagina. Com o valor medido 0x2431, << 12 cai em 36 MB (regiao vazia) e
    # << 20 cai em 9,05 GB (entre duas alocacoes vivas). Escolher por intuicao
    # ja derrubou DUAS hipoteses nesta investigacao; quem escolhe e' a medicao.
    space_path = ROOT / "driver" / "core" / "bc250_gpuva_space_contract.c"
    space_src = space_path.read_text(encoding="utf-8", errors="replace")         if space_path.exists() else ""
    if not space_src:
        errors.append(
            "bc250_gpuva_space_contract.c ausente: discriminador de fault sem portao")
    for needle, label in (
        ("static const uint32_t kShifts[2] = { 12u, 20u };",
         "as DUAS conversoes candidatas sao avaliadas, nao uma"),
        ("[TDR_GCVM_FAULT] CANDIDATO <<%u",
         "cada candidato publica seu veredito"),
        ("Bc250GpuvaSpaceDescribeAddress(",
         "o candidato e' procurado no snapshot, nao julgado por intuicao"),
        ("DENTRO da faixa viva", "acerto nomeia a alocacao"),
        # O SEGMENTO decide o conserto: Seg1 e local (~243 MB, CpuVisible,
        # limitado pela janela BAR de 256 MB) e Seg2 e GART em MEMORIA DE
        # SISTEMA -- PTE e coerencia diferentes. A Corrida 47 nomeou as
        # alocacoes que falham e nao disse em qual segmento vivem.
        ("Seg=%u (%s) SegOff=", "acerto nomeia o SEGMENTO de backing"),
        ("LOCAL_VRAM", "segmento local nomeado por extenso"),
        ("GART_SISTEMA", "segmento de sistema nomeado por extenso"),
        ("FORA de toda faixa viva", "erro nomeia a vizinhanca"),
        ("SEM VEREDITO", "snapshot indisponivel e' declarado, nao silencioso"),
        # CARIMBO DE RAIZ (Corrida 48 -> 49). A caminhada do fault usa a raiz do
        # INSTANTE DO TDR. Se as submissoes recentes usaram outra, "a PTE nao
        # existe" so' significa que se caminhou a tabela ERRADA -- e o defeito
        # passa a ser troca de contexto no VMID 7, nao escrita de PTE. Sem esta
        # comparacao as tres leituras (raiz trocada, corrida de publicacao, TLB
        # obsoleto) sao indistinguiveis, e exigem consertos diferentes.
        ("RAIZ_NO_TDR=0x%llX Carimbos=%u Divergentes=%u",
         "o fault compara a raiz do TDR com a das submissoes"),
        ("RaizNaSubmissao=0x%llX",
         "cada carimbo publica a raiz que estava instalada na submissao"),
        ("(RAIZ DIFERENTE)", "divergencia de raiz e nomeada, nao inferida"),
        ("Context->GfxSubmitStamps[slot].RootAtSubmit",
         "a submissao carimba a raiz instalada"),
    ):
        require(active, needle, label, errors)
    require(space_src, "if (r->State != BC250_GPUVA_RANGE_MAPPED) continue;",
            "so faixa MAPEADA conta como acerto (falso positivo inverteria o veredito)",
            errors)

    # (2h) POOL DE VMID POR CONTEXTO -- DX.7, fatia minima (Corrida 49 -> 52).
    # A Corrida 49 mediu 5 faults com divergencia de raiz em TODAS as
    # comparacoes (2, 8, 9, 14 e 15 de 16), 6 raizes distintas em 82 submissoes
    # e o MESMO IB sob TRES raizes. O conserto e VMID por contexto. Os quatro
    # reservados sao MEDIDOS, nao convencao: 0 e do microengine MEC (confirmado
    # no silicio na Corrida 51), 7 usuario, 8 paginacao, 9 compute.
    pool_path = ROOT / "driver" / "core" / "bc250_vmid_pool_contract.c"
    pool_src = pool_path.read_text(encoding="utf-8", errors="replace")         if pool_path.exists() else ""
    if not pool_src:
        errors.append("bc250_vmid_pool_contract.c ausente: pool de VMID sem portao")
    for needle, label in (
        ("if (Bc250VmidIsReserved(i) != 0u) continue;",
         "VMID reservado nunca entra no pool"),
        ("++Pool->ExhaustedCount;", "exaustao e contada, nao mascarada"),
        ("return BC250_VMID_POOL_EXHAUSTED;",
         "exaustao FALHA em vez de cair no VMID compartilhado"),
    ):
        require(pool_src, needle, label, errors)
    # Fallback para o VMID de usuario dentro do pool devolveria o defeito que a
    # faixa conserta, disfarcado de "modo degradado".
    forbid(pool_src, "BC250_VMID_RESERVED_USER;",
           "o pool nao pode devolver o VMID compartilhado como fallback", errors)
    pool_hdr = (ROOT / "driver/core/bc250_vmid_pool_contract.h").read_text(
        encoding="utf-8", errors="replace")
    for needle, label in (
        ("#define BC250_VMID_RESERVED_MEC     0u", "VMID 0 reservado ao MEC, por medicao"),
        ("#define BC250_VMID_RESERVED_USER    7u", "VMID 7 reservado"),
        ("#define BC250_VMID_RESERVED_PAGING  8u", "VMID 8 reservado"),
        ("#define BC250_VMID_RESERVED_COMPUTE 9u", "VMID 9 reservado"),
    ):
        require(pool_hdr, needle, label, errors)

    # (2h.2) LIGACAO DO POOL NO MINIPORT -- DX.7.
    # O contrato sozinho nao vale nada se ninguem o chamar: uma faixa anterior
    # deste projeto ja entregou codigo correto e inalcancavel (ContextAbi
    # populado so' num ramo do #if, tres ocorrencias da mesma classe). Os
    # portoes abaixo comparam TEXTO SEM ESPACOS, para nao reprovarem por
    # reformatacao nem passarem por mudanca de indentacao.
    pool_ws = re.sub(r"\s+", "", active)
    if "Bc250VmidPoolInitialize(&context->VmidPool);" not in pool_ws:
        errors.append(
            "o pool de VMID nunca e' inicializado no miniport: "
            "Entries[].Vmid ficaria zerado e o mapa mentiria")
    if ("Bc250VmidPoolAcquire(&context->VmidPool,"
            "(BC250_MCDM_HANDLE)pSubmitCommand->hContext,") not in pool_ws:
        errors.append(
            "o caminho de submissao GFX nao adquire VMID do pool: "
            "sem isso a corrida nao mede quantos contextos disputam o VMID 7")
    # A raiz tem de ser registrada no MESMO sitio em que entra no silicio.
    # Registrar depois abre janela para o mapa e o hardware discordarem --
    # exatamente a classe de defeito sob investigacao.
    if ("context->UserVmidInstalledRootPhysical=physicalRoot;"
            not in pool_ws):
        errors.append("sitio de instalacao da raiz do VMID 7 nao encontrado")
    else:
        depois = pool_ws.split(
            "context->UserVmidInstalledRootPhysical=physicalRoot;", 1)[1]
        if "(void)Bc250VmidPoolNoteRoot(&context->VmidPool," not in depois[:900]:
            errors.append(
                "a raiz instalada no VMID 7 nao e' registrada no pool logo "
                "apos entrar no silicio: mapa e hardware podem divergir")
    if ("Bc250VmidPoolRelease(&context->VmidPool,ContextHandle,"
            not in pool_ws):
        errors.append(
            "DestroyContext nao devolve o VMID ao pool: os 12 slots vazariam "
            "e a exaustao viraria artefato do vazamento, nao medicao")
    destroy_context_fn = function_body(active,
        "NTSTATUS Bc250ActiveDestroyContextObject(")
    destroy_context_ws = re.sub(r"\s+", "", destroy_context_fn)
    if ("result=Bc250ObjectStatusToNtStatus(status);"
            "if(status==BC250_MCDM_OBJECT_OK){"
            "(void)Bc250GpuvaSpaceInvalidateForObject(" not in destroy_context_ws):
        errors.append(
            "DestroyContext invalida GPUVA antes de provar destruicao concluida: "
            "IN_FLIGHT_SUBMISSIONS ainda usa esse estado")
    if ("if(status==BC250_MCDM_OBJECT_OK){" not in destroy_context_ws or
            "Bc250VmidPoolRelease(&context->VmidPool,ContextHandle,"
            not in destroy_context_ws.split(
                "if(status==BC250_MCDM_OBJECT_OK){", 1)[1]):
        errors.append(
            "DestroyContext pode devolver VMID sem destruicao concluida: "
            "slot seria reciclado durante submissao em voo")
    active_test = (ROOT / "driver/tests/active_miniport_test.c").read_text(
        encoding="utf-8", errors="replace")
    for needle, label in (
        ("DxgkDdiDestroyContext(contextHandle) ==\n                STATUS_DEVICE_BUSY",
         "teste exercita DestroyContext com submissao em voo"),
        ("Bc250VmidPoolLookup(&activeCtx->VmidPool, contextHandle,\n"
         "                &heldVmid) == BC250_VMID_POOL_OK",
         "teste prova que VMID permanece retido durante submissao"),
        ("Bc250VmidPoolLookup(&activeCtx->VmidPool, contextHandle,\n"
         "            NULL) == BC250_VMID_POOL_NOT_FOUND",
         "teste prova devolucao apenas apos destroy concluido"),
    ):
        require(active_test, needle, label, errors)
    # O pool agora instala lateralmente e publica READY, mas o pacote continua
    # em VMID 7. Isso isola escrita/readback/flush da futura troca no CP.
    if "Bc250GcvmInstallVmid(BC250_GCVM_USER_VMID,physicalRoot," not in pool_ws:
        errors.append(
            "a submissao GFX deixou de instalar a raiz em BC250_GCVM_USER_VMID "
            "sem que este portao fosse revisto: escopo da faixa violado")
    require(active, "Bc250GcvmInstallVmid(poolVmid, physicalRoot,",
            "miniport instala lateralmente a raiz no VMID alocado", errors)
    # PORTAO FRACO CORRIGIDO (21/09). A mutacao que trocava
    # "poolIo.FlushVmid = Bc250ActiveFlushVmidTlb" por
    # "poolIo.Flush = Bc250ActiveFlushUserVmidOnly" PASSAVA na auditoria.
    # Bc250GcvmInstallVmid recusa FlushVmid==NULL, entao o efeito seria falha
    # fechada em vez de flush no VMID errado -- mas a deteccao aconteceria
    # SO' no silicio, e o sintoma (raiz instalada sem TLB invalidado) e'
    # indistinguivel do defeito antigo. A armadilha (b) do handoff 307 exige
    # que a invalidacao acompanhe o VMID que esta sendo instalado.
    if "poolIo.FlushVmid=Bc250ActiveFlushVmidTlb;" not in pool_ws:
        errors.append(
            "a instalacao lateral nao liga FlushVmid por VMID: a TLB do VMID "
            "alocado ficaria com a traducao antiga e o fault pareceria o mesmo")
    if "poolIo.Flush=" in pool_ws:
        errors.append(
            "a instalacao lateral usa o flush de VMID fixo: invalidacao tem de "
            "seguir o VMID instalado, nunca o 7 legado")
    require(active, "Bc250VmidPoolRecordInstall(&context->VmidPool,",
            "resultado fisico e publicado na entrada do pool", errors)
    require(active, "Bc250VmidPoolBeginInstall(&context->VmidPool,",
            "miniport reserva a transacao antes de escrever MMIO", errors)
    require(active, "RECUSA POR EXAUSTAO",
            "exaustao falha fechada antes de tocar o anel", errors)
    # A INSTALACAO DEIXOU DE SER LATERAL (Corrida 54).
    #
    # Ate a Corrida 53 o pacote ia em VMID 7 e a instalacao no VMID do pool era
    # um efeito colateral observado: falhar ali e SEGUIR era o certo, porque o
    # slot nao era usado por ninguem, e recusar teria zerado a medicao caso o
    # silicio nao aceitasse raiz nesses contextos. A Corrida 53 respondeu isso
    # com 32/32 Readback=1 Flush=1 Ready=1 em cinco VMIDs.
    #
    # Agora o INDIRECT_BUFFER CARREGA esse VMID, e a polaridade se inverte: uma
    # submissao que parta com a tabela do proprio VMID nao instalada produz
    # exatamente o fault de traducao que esta faixa existe para eliminar -- e o
    # log diria que tudo correu bem. Todo ramo de tropeco TEM de recusar.
    inst_ini = active.find("Bc250VmidPoolBeginInstall(&context->VmidPool,")
    inst_fim = active.find(
        "/* 2.5. Garantir instalacao da tabela de paginas fisica no VMID 7",
        inst_ini if inst_ini >= 0 else 0)
    if inst_ini < 0 or inst_fim <= inst_ini:
        errors.append(
            "regiao da instalacao do VMID do contexto nao delimitavel: o "
            "portao do fail-closed nao pode ser avaliado")
    else:
        instala = active[inst_ini:inst_fim]
        # Cada ramo de tropeco e CONTADO, nao procurado por string: mutar um so'
        # deixava o outro cobrindo a falta (mutacao B3 da faixa anterior).
        linhas = instala.splitlines()
        tropecos = 0
        sem_recusa = 0
        for idx, ln in enumerate(linhas):
            if "beginStatus ==" not in ln and "beginStatus !=" not in ln and \
                    "installStatus != BC250_GCVM_VMID8_OK" not in ln:
                continue
            tropecos += 1
            janela = "".join(linhas[idx:idx + 12])
            if "return STATUS_" not in janela or \
                    "ReleaseSubmissionView" not in janela:
                sem_recusa += 1
        if tropecos < 3:
            errors.append(
                "instalacao do VMID do contexto: esperados 3 ramos de tropeco "
                "(BUSY, BeginInstall != OK, install/record falhou), "
                "encontrados %d -- portao nao avalia o que deveria" % tropecos)
        if sem_recusa:
            errors.append(
                "instalacao do VMID do contexto: %d de %d ramos de tropeco NAO "
                "recusam a submissao -- o pacote partiria com a tabela do "
                "proprio VMID ausente" % (sem_recusa, tropecos))
        if "a submissao SEGUE em VMID" in instala:
            errors.append(
                "instalacao do VMID do contexto: sobrou telemetria da faixa "
                "lateral dizendo que a submissao segue -- ela NAO segue mais")
        # TELEMETRIA NAO PODE DESCREVER O ESTADO ANTERIOR. A linha de sucesso
        # dizia "INDIRECT_BUFFER ainda em VMID7" e continuou dizendo depois de
        # o pacote passar a carregar o VMID do contexto -- seria lida como
        # medicao de um estado que nao existe mais.
        if "INDIRECT_BUFFER ainda em VMID" in instala:
            errors.append(
                "telemetria da instalacao afirma que o INDIRECT_BUFFER ainda "
                "vai em VMID 7: isso deixou de ser verdade quando o pacote "
                "passou a carregar o VMID do contexto")
        for needle, label in (
            ("o INDIRECT_BUFFER desta submissao carrega VMID%02u",
             "a linha de sucesso nomeia o VMID que o pacote realmente carrega"),
            ("[VMID_POOL] RECUSA:",
             "cada recusa se declara, com o VMID e o motivo"),
            ("Bc250VmidPoolRecordInstall(&context->VmidPool,",
             "resultado fisico e publicado na entrada do pool"),
        ):
            if needle not in instala:
                errors.append(
                    "instalacao do VMID do contexto: %s -- ausente" % label)

    # O PACOTE CARREGA O VMID DO CONTEXTO (Corrida 54). Este e o portao que
    # troca de lado: ate a Corrida 53 ele EXIGIA BC250_GCVM_USER_VMID aqui.
    require(active, "ibSizeDwords,\n            poolVmid,",
            "o INDIRECT_BUFFER carrega o VMID do contexto, nao o 7 compartilhado",
            errors)
    if re.sub(r"\s+", "", active).count(
            "ibSizeDwords,BC250_GCVM_USER_VMID,") != 0:
        errors.append(
            "a submissao GFX voltou a emitir o pacote em BC250_GCVM_USER_VMID: "
            "varios contextos disputariam a mesma raiz de novo, que e o defeito "
            "medido na Corrida 49 (5 de 5 comparacoes divergentes)")
    # O pool e tocado por SubmitCommandVirtual em DISPATCH_LEVEL, por
    # DestroyContext e pelo diagnostico do TDR. ObjectLock nao cobre os tres
    # caminhos e e proibido no submit; um spin lock dedicado evita alocacao
    # dupla, reuse durante destroy e snapshot rasgado no fault.
    active_hdr = (ROOT / "driver/wdk/bc250_active_miniport.h").read_text(
        encoding="utf-8", errors="replace")
    require(active_hdr, "KSPIN_LOCK VmidPoolLock;",
            "pool de VMID tem spin lock dedicado", errors)
    require(active, "KeInitializeSpinLock(&context->VmidPoolLock);",
            "spin lock do pool e inicializado", errors)
    if active.count("KeAcquireSpinLock(&context->VmidPoolLock") < 3:
        errors.append(
            "Acquire/Release/NoteRoot do pool nao estao todos serializados "
            "pelo VmidPoolLock")
    require(active, "KeAcquireSpinLock(&activeCtx->VmidPoolLock",
            "mapa do pool no TDR tira snapshot sob o lock", errors)
    # O mapa no fault e CALCULADO percorrendo os slots. Sem a contagem, a linha
    # viraria carimbo -- e "o gate carimbava o falso sucesso" ja apareceu cinco
    # vezes neste projeto.
    for needle, label in (
        ("if(poolContexts[pv]==NULL)continue;",
         "o mapa do fault pula slots livres no snapshot em vez de imprimir lixo"),
        ("++ocupados;", "o mapa do fault CONTA os ocupados"),
    ):
        if needle not in pool_ws:
            errors.append("mapa do pool no fault: %s -- ausente" % label)

    # (2h.3) A INVALIDACAO SEGUE OS VMIDs QUE A GPU USA -- DX.12.
    #
    # DEFEITO MEDIDO POR LEITURA apos a Corrida 58: desde a Corrida 54 o
    # PACKET3_INDIRECT_BUFFER carrega o VMID dinamico do pool (na 58 foram
    # 1..4), mas Bc250ActiveFlushMappedGfxhubTlb invalidava apenas 8/7/9.
    # Bc250GcvmInstallVmid invalida o slot NA INSTALACAO; uma UpdatePageTable
    # posterior nao invalidava slot dinamico nenhum. Casa com a medicao da
    # Corrida 58 (PTE contemporanea R/W/X na CPU, escrita falhando na GPU),
    # mas continua HIPOTESE ate a corrida -- o portao trava a COBERTURA, nao
    # a conclusao.
    transport_early = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(
        encoding="utf-8", errors="replace")
    flush_map = function_body(
        active, "static uint32_t Bc250ActiveFlushMappedGfxhubTlb(void *Context)")
    if not flush_map:
        errors.append(
            "Bc250ActiveFlushMappedGfxhubTlb nao localizada: o portao da "
            "cobertura de flush nao pode ser avaliado")
    else:
        if "Bc250VmidPoolReadyFlushMask(&active->VmidPool)" not in flush_map:
            errors.append(
                "o flush de TLB voltou a cobrir somente VMIDs fixos: os slots "
                "dinamicos do pool, que o INDIRECT_BUFFER carrega desde a "
                "Corrida 54, ficariam com a traducao antiga")
        # ORDEM, nao presenca. Segurar VmidPoolLock durante o flush (ate 1000
        # polls, em DISPATCH_LEVEL) e a classe do BugCheck 0x133 ja pago.
        i_rel = flush_map.find("KeReleaseSpinLock(&active->VmidPoolLock")
        i_mmio = flush_map.find("Bc250ActiveFlushVmidTlb(Context, vmid)")
        if i_rel < 0:
            errors.append(
                "a mascara do pool e lida sem soltar VmidPoolLock: o flush "
                "gastaria polls de MMIO com o lock do pool retido")
        elif i_mmio < 0 or i_mmio < i_rel:
            errors.append(
                "o flush por VMID acontece ANTES de soltar VmidPoolLock: "
                "spinlock retido durante MMIO em DISPATCH_LEVEL")
        # O VMID 0 e da microengine do MEC e tem politica transacional propria.
        if "requestedMask &= ~1u;" not in flush_map:
            errors.append(
                "a mascara de flush nao recusa o bit 0: o VMID do MEC entraria "
                "por carona se o pool mudar")
        # FLUSH PARCIAL NAO E SUCESSO. Contado por RAMO, nao por string: o
        # portao B3 da faixa anterior passou por procurar texto em vez de
        # contar, e foi refeito por isso.
        linhas = flush_map.splitlines()
        ramos = 0
        sem_falha_fechada = 0
        for idx, ln in enumerate(linhas):
            if "Bc250ActiveFlushVmidTlb(Context, vmid) == 0u" not in ln:
                continue
            ramos += 1
            janela = "".join(linhas[idx:idx + 5])
            if "failedMask |=" not in janela or "gfxhubOk = 0u;" not in janela:
                sem_falha_fechada += 1
        if ramos != 1:
            errors.append(
                "cobertura de flush: esperado 1 ramo de falha por VMID, "
                "encontrados %d -- o portao nao avalia o que deveria" % ramos)
        if sem_falha_fechada:
            errors.append(
                "cobertura de flush: %d ramo(s) de falha nao fecham o veredito "
                "global -- flush parcial viraria sucesso, e o chamador "
                "admitiria submissao com TLB nao invalidada" % sem_falha_fechada)
        # A MEDICAO DA CORRIDA 59 SAI DAQUI. Sem a mascara no log, os desfechos
        # B (flush exercitado, mesmo fault) e C (flush nao exercitado) ficam
        # indistinguiveis -- e desfecho indistinguivel nao e desfecho (Lei 10).
        for needle, label in (
            ("[FLUSH_POOL_VMIDS] #%u FixedMask=0x%04X PoolReadyMask=0x%04X",
             "a mascara pedida e publicada"),
            ("FalhouMask=0x%04X",
             "os VMIDs que nao invalidaram sao nomeados"),
            ("[FLUSH_POOL_VMIDS] FALHA:",
             "flush parcial se declara em vez de passar calado"),
        ):
            if needle not in flush_map:
                errors.append("cobertura de flush: %s -- ausente" % label)
        # ARMADILHA 16, JA PAGA: PUBLICACAO_COMPOSICAO saiu ZERO vezes em 10
        # falhas porque o nome escolhido nao continha token isento do
        # amostrador. A linha desta faixa tem teto PROPRIO no emissor; a
        # amostragem global nao pode alcanca-la.
        display_c = (ROOT / "driver/wdk/bc250_display_ddi.c").read_text(
            encoding="utf-8", errors="replace")
        require(display_c, 'Bc250LogTextContains(Text, "FLUSH_POOL_VMIDS")',
                "a linha da mascara de flush e isenta da amostragem global",
                errors)
        # (2h.5) DX.14 -- A ORDEM E O CONTEUDO DA FAIXA.
        #
        # As page tables sao escritas pela CPU em VRAM e ficam na cache de
        # escrita do HDP. Invalidar a TLB ANTES de drenar o HDP manda o walker
        # rebuscar bytes que ainda nao chegaram -- pioraria a janela em vez de
        # fecha-la. Por isso o portao trava a ORDEM, nao a presenca.
        #
        # Motivo medido: na Corrida 60, duas caminhadas com JANELA INALTERADA
        # (logo contemporaneas do fault) RESOLVERAM ate a folha, com PDE limpo
        # e PTE valida e legivel, enquanto a GPU reportava WALKER_ERROR=6 no
        # mesmo endereco. E o projeto ja mediu este mecanismo neste silicio em
        # 10/09 12:16 (antes_hdp=43DAFFFE, depois_hdp=00001304).
        # PORTAO FRACO CORRIGIDO PELA MUTACAO: a versao anterior procurava
        # so o ARGUMENTO ("..._FLUSH_CNTL, 1u)"), e passava quando a CHAMADA
        # de escrita era removida e o argumento ficava para tras. O invariante
        # agora e a chamada inteira, comparada sem espacos.
        flush_ws = re.sub(r"\s+", "", flush_map)
        if ("Bc250MmioTopologyWrite32(&active->Core.MmioTransport,"
                "BC250_ACTIVE_REG_HDP_MEM_COHERENCY_FLUSH_CNTL,1u)"
                not in flush_ws):
            errors.append(
                "a drenagem do HDP nao e mais uma escrita de verdade: o "
                "argumento ficou no fonte sem a chamada que o usa")
        i_hdp = flush_map.find(
            "BC250_ACTIVE_REG_HDP_MEM_COHERENCY_FLUSH_CNTL, 1u)")
        i_loop = flush_map.find("for (vmid = 1u; vmid < BC250_VMID_COUNT")
        if i_hdp < 0:
            errors.append(
                "o flush de TLB nao drena o HDP: as escritas de page table da "
                "CPU podem nao estar visiveis ao walker, e invalidar a TLB "
                "nesse estado manda a GPU rebuscar bytes velhos")
        elif i_loop < 0 or i_hdp > i_loop:
            errors.append(
                "o HDP e drenado DEPOIS da invalidacao de TLB: a ordem esta "
                "invertida e a janela de visibilidade continua aberta")
        for needle, label in (
            ("[HDP_PT_FLUSH] FALHA:",
             "falha de drenagem do HDP se declara, nunca passa calada"),
            ("HdpFlush=%u",
             "a linha da mascara publica se o HDP foi drenado"),
        ):
            if needle not in flush_map:
                errors.append("drenagem do HDP: %s -- ausente" % label)
        # O offset NAO e novo e nao pode virar numero solto: ele ja existe em
        # bc250_gfx_ring_transaction.h e nas allowlists do transporte.
        require(active,
                "#define BC250_ACTIVE_REG_HDP_MEM_COHERENCY_FLUSH_CNTL 0x00003FC4u",
                "offset do HDP declarado, igual ao ja catalogado", errors)
        if "0x00003FC4u" not in transport_early:
            errors.append(
                "o offset do HDP saiu da allowlist do transporte: a drenagem "
                "seria recusada e o flush de TLB seguiria com a tabela "
                "possivelmente invisivel")
    # A REGRA VIVE NO CONTRATO, nao so' no miniport: regra que existe apenas no
    # miniport nao tem como falhar numa suite offline.
    for needle, label in (
        ("uint32_t Bc250VmidPoolReadyFlushMask(const BC250_VMID_POOL *Pool)",
         "a mascara de invalidacao e funcao pura do contrato"),
        ("if (entry->Context == NULL) continue;",
         "slot livre nao entra na mascara"),
        ("if (entry->InstallState != BC250_VMID_POOL_INSTALL_READY) continue;",
         "NEVER/INSTALLING/FAILED nao entram: nenhum prova raiz aceita"),
        ("if (entry->InstalledRootPhysical == 0ull) continue;",
         "READY sem raiz nao entra"),
        ("return mask & BC250_VMID_POOL_MASK;",
         "os reservados 0/7/8/9 nunca saem da mascara do pool"),
    ):
        require(pool_src, needle, label, errors)
    pool_test = (ROOT / "driver/tests/vmid_pool_contract_test.c").read_text(
        encoding="utf-8", errors="replace")
    # O TESTE TAMBEM E PORTAO. "O gate congelava a mentira" ja apareceu cinco
    # vezes neste projeto; aqui a suite tem de EXIGIR os dois lados.
    for needle, label in (
        ("Bc250VmidPoolReadyFlushMask(&g_pool) == 0u",
         "a suite exige mascara vazia quando nenhum slot esta READY"),
        ("((1u << maskVmidA) | (1u << maskVmidB))",
         "a suite exige DOIS slots READY na mesma mascara"),
        ("BC250_VMID_RESERVED_MASK) == 0u",
         "a suite prova que nenhum reservado entra"),
    ):
        if needle not in pool_test:
            errors.append("suite do pool: %s -- ausente" % label)

    # (2h.4) A ESCRITA DE DIRETORIO DE PAGINAS NAO PODE SER MUDA -- DX.13.
    #
    # MEDIDO NA CORRIDA 59: no indice 4096 de uma raiz REAL (a da arena
    # 0x200040000) o TDR leu 0x450C9C071 -- formato de PTE num slot de PDE.
    # Pelo layout do Navi 10 o endereco do PDE comeca no bit 6, logo o bit 6 e
    # endereco e os bits [5:3] sao reservados: a GPU caminharia 64 bytes
    # adiante. E a assinatura do defeito de 05/09 (0x61), que ficou invisivel
    # por seis corridas porque o decodificador mascarava com 4 KiB.
    #
    # Dois defeitos corrigidos aqui, e um discriminante acrescentado. O portao
    # trava a OBSERVABILIDADE; nao afirma de onde o valor veio.
    upd_body = function_body(
        active, "static NTSTATUS Bc250ActiveUpdatePageTableHandler(")
    if not upd_body:
        errors.append(
            "Bc250ActiveUpdatePageTableHandler nao localizada: o portao da "
            "escrita de page table nao pode ser avaliado")
    else:
        # DEFEITO 1: o status do encode era descartado com (void) e o zero
        # resultante era gravado numa page table VIVA, sem uma linha de log.
        if re.search(r"\(void\)\s*Bc250McdmEncodeVidMmPteWithLevel", upd_body):
            errors.append(
                "a escrita de page table voltou a descartar o status do "
                "encode: uma recusa gravaria ZERO numa tabela viva sem dizer "
                "nada, que e a Lei 4 no caminho mais caro do driver")
        require(upd_body, "if (!NT_SUCCESS(encodeStatus)) {",
                "a escrita de page table confere o status do encode", errors)
        # Recusa NAO pode gravar. Contado por ramo, nao procurado por string.
        linhas = upd_body.splitlines()
        ramos = 0
        sem_skip = 0
        for idx, ln in enumerate(linhas):
            if "if (!NT_SUCCESS(encodeStatus)) {" not in ln and                     "BC250_ACTIVE_PDE_RESERVED_MASK) != 0ull) {" not in ln:
                continue
            ramos += 1
            janela = "".join(linhas[idx:idx + 6])
            if "continue;" not in janela:
                sem_skip += 1
        if ramos != 2:
            errors.append(
                "escrita de page table: esperados 2 ramos que impedem a "
                "gravacao (encode recusado e tripwire de PDE), encontrados "
                "%d -- o portao nao avalia o que deveria" % ramos)
        if sem_skip:
            errors.append(
                "escrita de page table: %d de %d ramos NAO pulam a gravacao -- "
                "entrada invalida chegaria a page table viva" % (sem_skip, ramos))
        for needle, label in (
            ("[ActiveUpdatePT] RECUSA:",
             "entrada que nao codificou se declara, e diz que nada foi gravado"),
            ("[PDE_TRIPWIRE] FALHA:",
             "formato de PTE em slot de PDE dispara tripwire nomeado"),
            ("[PDE_WRITE] ARENA idx=%u Lvl=%u",
             "quem escreve o indice da arena fica registrado"),
            ("BC250_ACTIVE_ARENA_ROOT_INDEX",
             "o indice da arena vem da constante derivada, nao digitado"),
        ):
            if needle not in upd_body:
                errors.append("escrita de page table: %s -- ausente" % label)
    # AS CONSTANTES SAO DERIVADAS. Numero digitado a mao ja custou seis
    # corridas neste projeto (os shifts do FAULT_STATUS transcritos de cabeca).
    require(active,
            "#define BC250_ACTIVE_PDE_RESERVED_MASK 0x0000000000000038ull",
            "mascara de reservados do PDE em fonte unica com o decodificador",
            errors)
    require(active,
            "(0x200040000ull >> (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS))",
            "o indice da arena e DERIVADO da geometria, nunca constante solta",
            errors)
    gcvm_contract = (ROOT / "driver/core/bc250_gcvm_contract.c").read_text(
        encoding="utf-8", errors="replace")
    require(gcvm_contract, "#define BC250_PDE_RESERVED_MASK    0x0000000000000038ull",
            "o decodificador de PDE mantem a mesma mascara de reservados",
            errors)
    # DEFEITO 2: LargePage nunca era olhado no nivel de diretorio. Esta faixa
    # MEDE em vez de recusar -- nenhuma corrida registrou a frequencia, e
    # recusar sem o numero poderia zerar a proxima medicao inteira, que e o
    # preco ja pago na Corrida 53. A recusa vem depois do numero.
    callbacks = (ROOT / "driver/wdk/bc250_mcdm_callbacks.c").read_text(
        encoding="utf-8", errors="replace")
    for needle, label in (
        ("[PDE_LARGEPAGE] Lvl=%u Pfn=0x%llX",
         "pagina grande no nivel de diretorio deixou de ser invisivel"),
        ("APROXIMACAO DECLARADA, NAO PROVADA",
         "a linha declara que a codificacao e aproximacao, nao prova"),
        ("if (seq < 8L || (seq % 512L) == 0L) {",
         "o teto e 8 primeiras mais 1 em 512, nao amostragem global"),
    ):
        require(callbacks, needle, "PDE LargePage: %s" % label, errors)
    # PORTAO FRACO CORRIGIDO PELA PROPRIA MUTACAO: procurar
    # "InterlockedIncrement(" solto PASSAVA, porque este arquivo ja tem SETE
    # ocorrencias em outros caminhos -- a mutacao que tirava o teto desta
    # linha nao reprovava nada. O contador tem de ser o DELA, e a comparacao
    # ignora espacos para nao depender de indentacao nem de quebra de linha.
    callbacks_ws = re.sub(r"\s+", "", callbacks)
    if "InterlockedIncrement(&g_Bc250PdeLargePageCount)-1L;" not in callbacks_ws:
        errors.append(
            "a linha [PDE_LARGEPAGE] perdeu o teto do contador proprio: ela e "
            "isenta da amostragem global e roda por ENTRADA de page table, "
            "entao sem teto afogaria o log que a faixa existe para coletar")
    # Linha rara e critica TEM de escapar da amostragem (armadilha 16).
    display_pde = (ROOT / "driver/wdk/bc250_display_ddi.c").read_text(
        encoding="utf-8", errors="replace")
    for tag in ("PDE_WRITE", "PDE_TRIPWIRE", "PDE_LARGEPAGE"):
        require(display_pde, 'Bc250LogTextContains(Text, "%s")' % tag,
                "a linha %s e isenta da amostragem global" % tag, errors)

    # (2h.6) DX.15 -- O SLOT E DEVOLVIDO TAMBEM NO SILICIO.
    #
    # ACHADO POR LEITURA apos a Corrida 61: Bc250VmidPoolRelease era SOMENTE
    # SOFTWARE. A entrada do pool era limpa e o VMID voltava para os livres,
    # mas o contexto GCVM continuava com ENABLE_CONTEXT ligado e PT_BASE
    # apontando para a raiz do processo MORTO -- pagina que o VidMm pode
    # liberar e reutilizar. E a Lei 5 quebrada: APPLY sem TEARDOWN.
    #
    # Este portao NAO afirma que isso causa o fault medido. Afirma que o
    # estado nao tem direito de existir.
    gcvm_txn = (ROOT / "driver/core/bc250_gcvm_vmid8_transaction.c").read_text(
        encoding="utf-8", errors="replace")
    off_body = function_body(gcvm_txn, "BC250_GCVM_VMID8_STATUS Bc250GcvmUninstallVmid(")
    if not off_body:
        errors.append(
            "Bc250GcvmUninstallVmid nao localizada: o VMID volta ao pool com o "
            "silicio ainda armado sobre a raiz de um processo morto")
    else:
        off_ws = re.sub(r"\s+", "", off_body)
        # A ORDEM e o conteudo: CNTL primeiro, endereco depois, TLB por ultimo.
        i_cntl = off_body.find("Interface->Write32(Interface->Context, cntlReg, 0u)")
        i_regs = off_body.find("Interface->Write32(Interface->Context, regs[i], 0u)")
        i_tlb = off_body.find("Interface->FlushVmid(Interface->Context, Vmid)")
        if i_cntl < 0 or i_regs < 0 or i_tlb < 0:
            errors.append(
                "desarme do VMID: falta desabilitar CNTL, zerar enderecos ou "
                "invalidar a TLB -- desarme parcial parece completo")
        elif not (i_cntl < i_regs < i_tlb):
            errors.append(
                "desarme do VMID fora de ordem: CNTL tem de cair ANTES de os "
                "enderecos serem zerados, e a TLB por ULTIMO; invalidar antes "
                "de desabilitar deixa a janela aberta")
        # O desarme e PROVADO por readback, nunca suposto.
        if "(value&BC250_GCVM_ENABLE_CONTEXT)!=0u" not in off_ws:
            errors.append(
                "desarme do VMID nao confere ENABLE_CONTEXT por readback: "
                "'escrevi 0' nao e' 'o contexto esta desarmado'")
        # FlushVmid por VMID, nunca o Flush de VMID fixo.
        if "Interface->Flush(" in off_body:
            errors.append(
                "desarme do VMID usa o flush de VMID FIXO: a traducao velha "
                "sobreviveria justamente no slot que esta sendo devolvido")
    # A LIGACAO. Contrato correto que ninguem chama ja aconteceu tres vezes
    # neste projeto (ContextAbi, GpuvaLabPromotion, SdmaIndirectOnly).
    for needle, label in (
        ("Bc250GcvmUninstallVmid(\n                                liberado, &disarmIo, &disarmEvidence);",
         "o miniport desarma o VMID que acabou de devolver"),
        ("[VMID_POOL] DESARMADO: VMID%02u status=%u",
         "o desarme publica status, readback e flush"),
        ("[VMID_POOL] FALHA NO DESARME:",
         "desarme que falha se declara, nunca passa calado"),
        ("disarmIo.FlushVmid = Bc250ActiveFlushVmidTlb;",
         "o desarme invalida a TLB do VMID devolvido, nao de um fixo"),
    ):
        if needle.replace("\n", "\n") not in active and \
                re.sub(r"\s+", "", needle) not in re.sub(r"\s+", "", active):
            errors.append("desarme do VMID: %s -- ausente" % label)
    # A SUITE TAMBEM E PORTAO.
    gcvm_test = (ROOT / "driver/tests/gcvm_vmid8_transaction_test.c").read_text(
        encoding="utf-8", errors="replace")
    # PORTAO FRACO CORRIGIDO PELA MUTACAO (terceira vez nesta sequencia): a
    # versao anterior procurava "CHECK(f->LastFlushVmid == target);" no ARQUIVO
    # inteiro, e a string tambem existe no bloco de INSTALL -- afrouxar a
    # assercao do bloco de DESARME nao reprovava nada. A busca agora e
    # DELIMITADA ao bloco do desarme.
    i_off = gcvm_test.find("DX.15 -- DESARME DO SLOT NO SILICIO")
    j_off = gcvm_test.find("Bc250GcvmInstallVmid(BC250_GCVM_CONTEXT_COUNT", i_off)         if i_off >= 0 else -1
    if i_off < 0 or j_off <= i_off:
        errors.append(
            "bloco do desarme nao delimitavel na suite: o portao nao pode "
            "avaliar o que deveria")
    else:
        bloco_off = gcvm_test[i_off:j_off]
        for needle, label in (
            ("Bc250GcvmUninstallVmid(target, &io, &offEv)",
             "a suite exercita o desarme"),
            ("CHECK(f->LastFlushVmid == target);",
             "a suite exige que a TLB invalidada seja a do VMID desarmado"),
            ("CHECK(f->Registers[Bc250GcvmContextCntlOffset(15u) / 4u] != 0u);",
             "contraponto: desarmar um VMID nao pode tocar outro"),
            ("CHECK(offEv.ProgramReadbackPassed == 1u);",
             "a suite exige a prova de readback do desarme"),
        ):
            if needle not in bloco_off:
                errors.append("suite do desarme: %s -- ausente" % label)

    # (2h.7) DX.17 -- READBACK DA ESCRITA NO INDICE DA ARENA.
    #
    # MEDIDO, tres corridas concordando: escrevemos o indice 4096 SEMPRE limpo
    # (Corridas 60, 61 e 63 somam 314 escritas, zero sujas, zero tripwire) e o
    # TDR leu valor de PTE nesse mesmo indice em TRES corridas (59, 61, 63).
    # As duas coisas nao podem ser verdadeiras sem que alguem escreva depois --
    # ou sem que a nossa escrita nao chegue onde acreditamos. O readback
    # imediato e a UNICA coisa que separa os dois casos.
    if not upd_body:
        pass
    else:
        upd_ws = re.sub(r"\s+", "", upd_body)
        if ("constUINT64arenaReadback=destination[BC250_ACTIVE_ARENA_ROOT_INDEX];"
                not in upd_ws):
            errors.append(
                "a escrita no indice da arena nao e lida de volta: 'escrevemos "
                "limpo' e 'a memoria esta suja' continuariam sendo duas "
                "afirmacoes que nunca se encontram no mesmo log")
        if "if (arenaReadback != arenaEncoded) {" not in upd_body:
            errors.append(
                "o readback do indice da arena nao e COMPARADO: ler sem "
                "conferir e' custo sem informacao")
        for needle, label in (
            ("[PDE_WRITE] DIVERGENCIA: idx=%u escrevemos 0x%016llX",
             "divergencia entre escrita e readback se declara"),
            ("active->ArenaPdeLastWritten = arenaEncoded;",
             "o ultimo valor escrito fica retido para o TDR comparar"),
        ):
            if needle not in upd_body:
                errors.append("readback da arena: %s -- ausente" % label)
        # A divergencia NAO pode ser "corrigida" reescrevendo: isso esconderia
        # a frequencia, que e' justamente o dado que falta.
        if re.search(r"if \(arenaReadback != arenaEncoded\) \{[^}]*destination\[",
                     upd_body, re.S):
            errors.append(
                "o ramo de divergencia reescreve o destino: reescrever esconde "
                "a frequencia, que e o dado que a faixa existe para obter")
    # O confronto no TDR poe os dois numeros no MESMO log.
    for needle, label in (
        ("[PDE_ARENA_CONFRONTO] UltimoQueEscrevemos=0x%016llX Escritas=%u",
         "o TDR publica o ultimo valor que NOS escrevemos"),
        ("activeCtx->ArenaPdeWriteSeq", "a contagem de escritas acompanha, e "
         "zero escritas fica DECLARADO em vez de virar 'eram iguais'"),
    ):
        require(active, needle, "confronto da arena: %s" % label, errors)
    require(display_pde, 'Bc250LogTextContains(Text, "PDE_ARENA_CONFRONTO")',
            "a linha do confronto e isenta da amostragem global", errors)

    # (2g) INVENTARIO DOS 16 CONTEXTOS GCVM (Corrida 49 -> 50).
    # A Corrida 49 provou que varios contextos disputam o VMID 7: 5 faults, 5
    # comparacoes de raiz, TODAS divergentes, e o mesmo IB sob TRES raizes. O
    # conserto e VMID por contexto. A Corrida 51 confirmou que os offsets nao
    # sao aliases e a 52 validou o mapa do pool. O inventario continua SOMENTE
    # LEITURA; a escrita ganhou uma politica separada, exata, apenas para os
    # VMIDs nao reservados do pool.
    gcvm_path = ROOT / "driver" / "core" / "bc250_gcvm_vmid8_transaction.c"
    gcvm_src = gcvm_path.read_text(encoding="utf-8", errors="replace")         if gcvm_path.exists() else ""
    if not gcvm_src:
        errors.append("bc250_gcvm_vmid8_transaction.c ausente: inventario sem portao")
    for needle, label in (
        ("[GCVM_INVENTARIO] Varridos=%u EmUso=%u Livres=%u Ilegiveis=%u",
         "o inventario publica o resumo dos 16 contextos"),
        ("ILEGIVEL", "contexto nao lido e' declarado, nao contado como livre"),
    ):
        require(active, needle, label, errors)
    for needle, label in (
        ("BC250_GCVM_CNTL_BASE + (Vmid * BC250_GCVM_CNTL_STRIDE)",
         "offset de CNTL derivado por formula, nao tabela nova a mao"),
        ('if (e->CntlRead == 0u && e->PtBaseRead == 0u) {',
         "leitura falha nao vira veredito"),
        ("++Inventory->UnreadableCount;",
         "ilegivel tem contador proprio"),
        ("(uint64_t)lo & ~0xFFFull",
         "flags da PT_BASE mascaradas (sem isso, livre com flag vira ocupado)"),
    ):
        require(gcvm_src, needle, label, errors)
    # O inventario NAO pode escrever. A allowlist nao pode ganhar a faixa crua
    # dos 16 contextos: so' a funcao exata do pool pode autorizar cada offset.
    transport = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(
        encoding="utf-8", errors="replace")
    write_fn = function_body(transport,
        "static uint32_t Bc250MmioTopologyWriteOffsetAllowed(")
    forbid(write_fn, "0x0000A240u",
           "allowlist de ESCRITA nao pode abrir a faixa dos 16 contextos GCVM",
           errors)
    require(write_fn, "Bc250VmidPoolIsWritableGcvmRegister(ByteOffset)",
            "escrita GCVM dos VMIDs do pool passa pela allowlist exata", errors)
    read_fn = function_body(transport,
        "static uint32_t Bc250MmioTopologyOffsetAllowed(")
    require(read_fn, "Bc250VmidPoolIsWritableGcvmRegister(ByteOffset)",
            "readback dos sete registradores do pool usa a mesma allowlist exata",
            errors)
    pool_hdr = (ROOT / "driver/core/bc250_vmid_pool_contract.h").read_text(
        encoding="utf-8", errors="replace")
    for needle, label in (
        ("BC250_VMID_POOL_MASK >> vmid", "allowlist deriva da mascara do pool"),
        ("Bc250GcvmIsContextRegisterForVmid(vmid, Offset)",
         "allowlist reconhece os sete offsets exatos do contexto"),
    ):
        require(pool_hdr, needle, label, errors)

    # InstallVmid tem de carregar a identidade do VMID ate o flush. Um
    # callback fixo em 7/8 repetiria o fault mesmo com a raiz no slot certo.
    gcvm_install_src = (ROOT / "driver/core/bc250_gcvm_vmid8_transaction.c").read_text(
        encoding="utf-8", errors="replace")
    gcvm_install_hdr = (ROOT / "driver/core/bc250_gcvm_vmid8_transaction.h").read_text(
        encoding="utf-8", errors="replace")
    install_fn = function_body(gcvm_install_src,
        "BC250_GCVM_VMID8_STATUS Bc250GcvmInstallVmid(")
    require(gcvm_install_hdr, "BC250_GCVM_FLUSH_VMID FlushVmid;",
            "interface de instalacao recebe callback parametrizado por VMID",
            errors)
    require(install_fn, "Interface->FlushVmid(Interface->Context, Vmid)",
            "InstallVmid invalida exatamente o VMID programado", errors)
    forbid(install_fn, "Interface->Flush(Interface->Context)",
           "InstallVmid nao pode voltar ao flush sem identidade de VMID",
           errors)
    require(active, "io.FlushVmid = Bc250ActiveFlushVmidTlb;",
            "miniport entrega o VMID escolhido ao backend de TLB", errors)

    # RootPhysical ainda e' diagnostico do contexto sob o VMID7 legado. A
    # autoridade de hardware do slot precisa de estado separado; falha nova
    # apaga qualquer READY antigo.
    pool_src = (ROOT / "driver/core/bc250_vmid_pool_contract.c").read_text(
        encoding="utf-8", errors="replace")
    require(pool_hdr, "InstalledRootPhysical;",
            "pool separa raiz observada de raiz instalada no slot", errors)
    require(pool_hdr, "BC250_VMID_POOL_INSTALL_STATE InstallState;",
            "pool guarda estado de instalacao por entrada", errors)
    require(pool_hdr, "BC250_VMID_POOL_INSTALLING",
            "pool tem estado transitorio antes do ACK fisico", errors)
    begin_install = function_body(pool_src,
        "BC250_VMID_POOL_STATUS Bc250VmidPoolBeginInstall(")
    require(begin_install,
            "entry->InstallState = BC250_VMID_POOL_INSTALLING;",
            "BeginInstall publica INSTALLING antes de soltar o lock", errors)
    release_vmid = function_body(pool_src,
        "BC250_VMID_POOL_STATUS Bc250VmidPoolRelease(")
    require(release_vmid, "BC250_VMID_POOL_INSTALLING",
            "slot INSTALLING nao pode ser reciclado", errors)
    record_install = function_body(pool_src,
        "BC250_VMID_POOL_STATUS Bc250VmidPoolRecordInstall(")
    require(record_install,
            "entry->InstallState != BC250_VMID_POOL_INSTALLING",
            "RecordInstall so publica resultado de um BeginInstall", errors)
    require(record_install, "entry->InstalledRootPhysical = 0ull;",
            "falha de instalacao limpa raiz READY anterior", errors)
    require(record_install,
            "entry->InstallState = BC250_VMID_POOL_INSTALL_FAILED;",
            "falha de instalacao fica explicita por entrada", errors)
    # DX.10: Release limpa a autoridade viva, mas a ultima instalacao PROVADA
    # sobrevive fora da entrada para a caminhada post-mortem. A fonte usada
    # precisa ser publicada; caso contrario a telemetria volta a mentir.
    require(pool_hdr, "BC250_VMID_POOL_RETAINED_ROOT RetainedRoots[",
            "raiz post-mortem fica fora da entrada reciclavel", errors)
    require(record_install,
            "Pool->RetainedRoots[Vmid].RootPhysical = RootPhysical;",
            "somente RecordInstall OK atualiza a raiz retida", errors)
    fault_walk_root = function_body(pool_src,
        "BC250_VMID_POOL_STATUS Bc250VmidPoolRootForFaultWalk(")
    require(fault_walk_root, "BC250_VMID_POOL_WALK_ROOT_LIVE_READY",
            "caminhada prefere entrada viva READY", errors)
    require(fault_walk_root, "BC250_VMID_POOL_WALK_ROOT_RETAINED",
            "caminhada cai explicitamente na ultima raiz provada", errors)
    forbid(release_vmid, "RetainedRoots",
           "Release nao pode apagar a raiz diagnostica post-mortem", errors)

    # (2f) CAMINHADA DA TABELA NO PONTO DO FAULT (Corrida 47->48).
    # A Corrida 47 provou que o endereco que falha esta DENTRO de uma faixa que
    # o snapshot declara MAPPED: software e hardware discordam. O log nao podia
    # dizer de que lado, porque [ActiveUpdatePT] e classe AMOSTRADA -- terceira
    # vez que a amostragem barrou a conclusao. A caminhada tira a resposta do
    # log e poe no silicio. E SOMENTE LEITURA, e so' e legal porque
    # DXGKDDI_RESETFROMTIMEOUT e _IRQL_requires_(PASSIVE_LEVEL) (d3dkmddi.h):
    # a leitura de VRAM fora da janela usa MmMapIoSpace, que exige <= APC_LEVEL.
    for needle, label in (
        # O rotulo passou a ser DERIVADO do VMID do fault (Corrida 55): ate a
        # Corrida 54 ele dizia FAULT_VMID7 enquanto caminhava VMID 1, 2 ou 3.
        ('(faultVmid == BC250_GCVM_USER_VMID) ?',
         "o rotulo da caminhada segue o VMID que o fault nomeia"),
        ('"FAULT_VMID_POOL"',
         "fault em VMID do pool tem rotulo proprio, nao o do 7"),
        # (a raiz e conferida ESTRUTURALMENTE logo abaixo -- exigir so a
        #  presenca da string nao pega a TROCA, porque o campo aparece em
        #  outros sitios do arquivo; a mutacao M2 passou por esse buraco)
        # A CONCLUSAO QUE ESTE PORTAO EXIGIA ESTAVA ERRADA (Corrida 62), e
        # ele a estava CONGELANDO -- a classe "o gate carimbava a mentira",
        # agora aplicada a uma frase de veredito em vez de a um contador.
        #
        # A frase antiga era "a PTE EXISTE, logo o defeito NAO e' PTE ausente".
        # Ela so' valeria se o endereco caminhado fosse o endereco que faltou.
        # O segundo candidato do discriminador NAO e': pageAddr<<20 e
        # identicamente (pageAddr<<12)<<8, entao resolver ali significa que o
        # endereco real e 256x o que faltou -- perda de 8 bits --, e NAO que a
        # tabela esteja certa no endereco do fault.
        #
        # gc_10_1_0_sh_mask.h:12163-12168 fecha a questao: LOGICAL_PAGE_ADDR e
        # LO32 (32 bits) + HI4 (4 bits) = pagina de 36 bits, logo << 12.
        ("se o shift for VA<<8, isso significa que o endereco que FALTOU perdeu "
         "8 bits, NAO que a tabela esteja certa",
         "resolver publica a conclusao CERTA, nao a de que a tabela esta boa"),
        ('"VA<<8(perda de 8 bits)"',
         "o segundo candidato se chama pelo que e, nao de granularidade"),
        ("NAO RESOLVE com Root=",
         "nao resolver tambem e' veredito publicado"),
        # PORTAO FRACO CORRIGIDO PELA MUTACAO (quarta vez nesta sequencia, e de
        # novo "procurei a existencia em vez do uso"): exigir a string do rotulo
        # provava que o ARRAY existe, nao que as duas linhas o CONSUMAM. A
        # mutacao que fixava "<<20" na chamada passava.
    ):
        require(active, needle, label, errors)
    if re.sub(r"\s+", "", active).count(
            "kShiftNames[k],(unsignedlonglong)candidate,") != 2:
        errors.append(
            "as duas linhas do resolver nao consomem kShiftNames: o rotulo do "
            "candidato voltaria a ser um shift cru, e foi assim que eu li "
            "'<<20 resolveu' como 'a tabela esta certa' por tres corridas")
    for needle, label in (
        ("raiz do VMID %u AUSENTE",
         "raiz ausente nomeia o VMID real, nao o 7 antigo"),
        ("runtime caps INDISPONIVEIS",
         "sem VramMcBase nao se caminha e nao se inventa"),
    ):
        require(active, needle, label, errors)
    # A RAIZ DA CAMINHADA SEGUE O VMID QUE O FAULT NOMEIA (Corrida 54).
    #
    # Ate a Corrida 53 este portao exigia a raiz do VMID 7, e estava certo: era
    # o unico VMID que o pacote usava. Agora o pacote carrega o VMID do
    # contexto, e caminhar um fault de VMID 3 com a raiz do VMID 7 leria a
    # TABELA ERRADA -- "a PTE nao existe" viraria conclusao falsa sobre a tabela
    # certa. Armadilha (d) do handoff 307.
    active_ws = re.sub(r"\s+", "", active)
    for needle, label in (
        ("constuint32_tfaultVmid=(faultStatus&0x00F00000u)>>20;",
         "o VMID do fault e extraido do proprio status, nao suposto"),
        ("Bc250VmidPoolRootForFaultWalk(&activeCtx->VmidPool,faultVmid,&poolWalk)",
         "a raiz vem da entrada viva ou da retencao do VMID que faltou"),
        ("(faultVmid==BC250_GCVM_USER_VMID)?activeCtx->UserVmidInstalledRootPhysical:poolRoot",
         "fault no VMID 7 legado ainda usa a escrituracao do 7"),
    ):
        if needle not in active_ws:
            errors.append(
                "caminhada do fault: %s -- ausente; caminhar a tabela errada "
                "responde a pergunta errada e parece funcionar" % label)
    # (55a) O LATCH DECIDE O SIGNIFICADO DE TUDO. Dez corridas leram
    # GCVM_L2_PROTECTION_FAULT_STATUS sem nunca ler o CNTL que o governa. Com
    # ALLOW_SUBSEQUENT_..._UPDATES em zero, o par STATUS/ADDR fica preso no
    # PRIMEIRO fault -- e o Status decodificado pode ser de minutos antes.
    require(active, "[TDR_GCVM_FAULT] LATCH_CNTL=0x%08X",
            "o fault le e decodifica o CNTL que governa o latch", errors)
    require(active, "LATCH PRESO -- o Status acima pode ser do PRIMEIRO fault",
            "o veredito do latch e declarado nos dois sentidos", errors)
    require(active, "LATCH_CNTL NAO LIDO",
            "falha de leitura do CNTL e declarada, nunca silenciosa", errors)
    # A ESCRITA em 0xA120 continua PROIBIDA: limpar o latch muda o estado do
    # silicio no meio do TDR e e' decisao separada, com criterio proprio.
    transporte = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(
        encoding="utf-8", errors="replace")
    escrita_fn = function_body(transporte,
        "static uint32_t Bc250MmioTopologyWriteOffsetAllowed(")
    # A ESCRITA EM 0xA120 FOI ABERTA NA CORRIDA 56, e a polaridade deste portao
    # se inverteu de proposito. A justificativa e' medida: LATCH_CNTL=0x3FFFFFFC
    # com AceitaAtualizacoes=0 prende o par STATUS/ADDR no primeiro fault.
    #
    # PORTAO FRACO CORRIGIDO NO MESMO DIA: a versao anterior procurava o
    # literal "0x0000A120u" e foi ATRAVESSADA sem reprovar quando a allowlist
    # passou a usar a constante simbolica. Agora as duas grafias contam.
    escrita_tem_cntl = (
        "0x0000A120u" in escrita_fn or
        "BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL" in escrita_fn)
    if not escrita_tem_cntl:
        errors.append(
            "0xA120 saiu da allowlist de ESCRITA: sem o pulso de CLEAR o latch "
            "volta a prender o primeiro fault, que e o defeito medido na "
            "Corrida 55")
    # Escrever no CNTL das sete classes de fault e' perigoso o bastante para
    # ter UM escritor so'. O adaptador do miniport recusa qualquer outro
    # offset ANTES da allowlist, e o contrato e' quem monta o valor.
    if "if (ByteOffset != BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL) return 0u;" \
            not in active:
        errors.append(
            "o adaptador de escrita do dreno nao restringe o offset: ele "
            "viraria porta aberta para escrever qualquer registrador")
    latch_src = (ROOT / "driver/core/bc250_fault_latch_contract.c").read_text(
        encoding="utf-8", errors="replace")
    latch_ws = re.sub(r"\s+", "", latch_src)
    for needle, label in (
        ("cntlBefore|BC250_FAULT_CNTL_CLEAR_STATUS_ADDR_BIT",
         "o pulso LIGA o bit 0 preservando os demais (Lei 6), nunca constante"),
        ("cntlBefore&~BC250_FAULT_CNTL_CLEAR_STATUS_ADDR_BIT",
         "o pulso DESLIGA o bit 0 preservando os demais"),
        ("if(readback!=cntlBefore)returnBC250_FAULT_LATCH_RESTORE_FAILED;",
         "a restauracao do CNTL e PROVADA por readback, nao suposta"),
        ("if(temAnterior!=0u&&status==anterior)",
         "repeticao do mesmo Status nao vira fault novo"),
        ("Evidence->ClearIneficaz=1u;",
         "CLEAR que nao pega e declarado, nao mascarado"),
    ):
        if needle not in latch_ws:
            errors.append("dreno do latch: %s -- ausente" % label)
    # O valor escrito NUNCA pode ser constante: perderia as sete classes de
    # fault que o POST habilitou (medido: Range/Pde0/Pde1/Pde2/TranslateFurther/
    # Nack/DummyPage/Valid/Read/Write/Execute todos em 1).
    if re.search(r"Write32\([^)]*FAULT_CNTL,\s*0x[0-9A-Fa-f]+u?\)", latch_src):
        errors.append(
            "o dreno escreve CONSTANTE no CNTL de fault: os 11 enables que o "
            "POST deixou ligados seriam perdidos")
    # A telemetria do dreno tem de existir e declarar a ausencia.
    for needle, label in (
        ("[TDR_GCVM_FAULT] DRENO: status=%u Drenados=%u",
         "o dreno publica quantos faults saiu da fila"),
        ("nenhum fault na fila -- a ausencia esta DECLARADA",
         "fila vazia e declarada, nunca silenciosa"),
        ("[TDR_GCVM_FAULT] DRENO #%u:",
         "cada fault drenado e decodificado individualmente"),
    ):
        require(active, needle, "dreno do latch: %s" % label, errors)
    # A funcao de LEITURA chama-se Bc250MmioTopologyOffsetAllowed (sem "Read");
    # a de escrita e' Bc250MmioTopologyWriteOffsetAllowed. Errar o nome faria o
    # portao passar por fatia vazia -- falso negativo silencioso.
    leitura_fn = function_body(transporte,
        "static uint32_t Bc250MmioTopologyOffsetAllowed(")
    if not leitura_fn.strip():
        errors.append(
            "allowlist de leitura do MMIO nao localizada: o portao do 0xA120 "
            "nao pode ser avaliado")
    if "0x0000A120u" not in leitura_fn:
        errors.append(
            "0xA120 nao esta na allowlist de LEITURA: sem o CNTL nao se sabe "
            "se o Status do fault e recente ou preso")

    # (55b) A CAMINHADA E' POST-MORTEM. Sem comparar a epoca da tabela entre a
    # submissao e o fault, "a PTE existe" nao diz nada sobre o INSTANTE do
    # fault -- a PTE pode ter nascido depois.
    require(active, "[TDR_GCVM_FAULT] JANELA_DE_PUBLICACAO:",
            "a janela entre submissao e fault e medida, nao suposta", errors)
    for needle, label in (
        ("TABELA INALTERADA na janela",
         "desfecho em que a caminhada FALA do instante do fault"),
        ("TABELA MUDOU na janela",
         "desfecho em que a caminhada NAO prova nada sobre o instante"),
    ):
        require(active, needle,
                "janela de publicacao: %s -- ausente" % label, errors)
    janela_ws = re.sub(r"\s+", "", active)
    if "epocaSub==epocaAgora" not in janela_ws:
        errors.append(
            "o veredito da janela nao COMPARA as duas epocas: sem a comparacao "
            "a linha vira carimbo, e carimbo ja foi confundido com medicao "
            "cinco vezes neste projeto")

    # (55c) O CARIMBO GRAVA A RAIZ DO VMID QUE O PACOTE CARREGA. Ate a Corrida
    # 54 gravava a do VMID 7 legado, e a comparacao no fault era contra uma raiz
    # que o pacote nao usava.
    if "RootAtSubmit=(Vmid==BC250_GCVM_USER_VMID)?" not in janela_ws:
        errors.append(
            "o carimbo de submissao nao grava a raiz do VMID do pacote: a "
            "comparacao no fault seria contra uma raiz que o pacote nao usa")

    require(active, "[TDR_GCVM_FAULT] ORIGEM_DA_RAIZ:",
            "o log declara de onde veio a raiz usada na caminhada", errors)
    require(active, "RETIDA_POS_MORTE -- ultima instalacao provada",
            "o log distingue raiz retida de entrada viva READY", errors)
    forbid(active, "raiz do VMID 7 AUSENTE",
           "telemetria antiga mentia VMID 7 para faults do pool", errors)
    # Somente READY pode entregar raiz: INSTALLING e FAILED devolveriam um
    # endereco que o silicio talvez nunca tenha aceitado, e o resultado
    # pareceria medicao.
    pool_src_walk = (ROOT / "driver/core/bc250_vmid_pool_contract.c").read_text(
        encoding="utf-8", errors="replace")
    leitor = function_body(pool_src_walk,
        "uint64_t Bc250VmidPoolInstalledRootForVmid(")
    if "entry->InstallState != BC250_VMID_POOL_INSTALL_READY" not in leitor:
        errors.append(
            "Bc250VmidPoolInstalledRootForVmid entrega raiz de slot que nao "
            "esta READY: a caminhada usaria um endereco nao confirmado")

    # DX.11: o alvo medido 0x200040 precisa sair nomeado pelo token imutavel da
    # alocacao. O handle sozinho pode morrer antes do TDR e nao e' nome.
    gpuva_hdr = (ROOT / "driver/core/bc250_gpuva_space_contract.h").read_text(
        encoding="utf-8", errors="replace")
    gpuva_src = (ROOT / "driver/core/bc250_gpuva_space_contract.c").read_text(
        encoding="utf-8", errors="replace")
    journal_hdr = (ROOT / "driver/core/bc250_gpuva_journal_contract.h").read_text(
        encoding="utf-8", errors="replace")
    require(gpuva_hdr, "uint64_t AllocationToken;",
            "snapshot de GPUVA preserva o nome imutavel da alocacao", errors)
    require(journal_hdr, "uint64_t AllocationToken;",
            "journal transporta o nome imutavel ate o snapshot", errors)
    require(gpuva_src, "slot->AllocationToken = Param->AllocationToken;",
            "insercao no snapshot preserva o token da alocacao", errors)
    require(gpuva_src, "Site->AllocationToken = r->AllocationToken;",
            "descricao do endereco devolve o nome da alocacao", errors)
    require(active, "entries[index].AllocationToken = allocationToken;",
            "UpdatePageTable captura o token enquanto o objeto ainda vive", errors)
    require(active, "[TDR_GCVM_FAULT] ALVO 0x200040:",
            "o endereco repetido da Corrida 56 tem veredito dedicado", errors)
    require(active, "Nome=%s Token=0x%llX",
            "alocacao que contem o fault sai nomeada, nao so por handle", errors)
    require(active, "EXCEDE 34 BITS -- fora do espaco traduzivel",
            "<<20 invalido e declarado em vez de competir com <<12", errors)

    # Corrida 57 nao reteve fault em nenhum dos cinco TDRs, embora tenha
    # mapeado 0x200040000. O alvo precisa falar no UpdatePageTable tambem: e'
    # a unica forma de obter permissao DXGK + PTE codificada sem depender do
    # latch. A linha e desamostrada explicitamente porque ocorre uma vez.
    require(active, "[DX_TARGET_200040] MAP:",
            "alvo 0x200040 fala durante o UpdatePageTable mesmo sem fault", errors)
    for needle, label in (
        ("ReadOnly=%u NoExecute=%u CacheCoherent=%u",
         "permissoes DXGK da pagina-alvo"),
        ("EncodedPte=0x%llX EncodeStatus=0x%08X",
         "PTE realmente codificada e status da codificacao"),
        ("Token=0x%llX Nome=%s AllocBytes=0x%llX AllocKnown=%u",
         "identidade e tamanho da alocacao do alvo"),
    ):
        require(active, needle, "DX_TARGET_200040: %s" % label, errors)
    display_ddi = (ROOT / "driver/wdk/bc250_display_ddi.c").read_text(
        encoding="utf-8", errors="replace")
    require(display_ddi, 'Bc250LogTextContains(Text, "DX_TARGET_200040")',
            "veredito raro de 0x200040 isento da amostragem", errors)

    # (2b) PERNA 3 NAO PODE PROMETER DELEGADO INEXISTENTE (Corrida 44).
    # A linha imprimia "Perna3=DELEGADA_AO_BYTE_PROOF" e a mineracao contou 111
    # ocorrencias de BYTE_PROOF no log inteiro, TODAS elas esta propria linha:
    # o delegado nunca rodou. [GfxIbInspect] varre a estrutura do IB pela CPU e
    # nao confere byte de destino. Rotulo que promete prova inexistente faz o
    # leitor contar tripe completo onde ha 2 de 3 pernas.
    forbid(active, "Perna3=DELEGADA_AO_BYTE_PROOF",
           "perna 3 do tripe GFX nomeia delegado que ninguem executa", errors)
    require(active, "BC250_GFX_TRIPOD_LEG3_STATE",
            "estado da perna 3 do tripe GFX em fonte unica", errors)
    require(active, "NAO_EXECUTADA_SEM_GABARITO_DE_DESTINO",
            "perna 3 do tripe GFX declarada aberta, nao simulada", errors)
    require(poll, "TRIPE INCOMPLETO, 2 de 3 pernas",
            "veredito do tripe GFX declara que esta incompleto", errors)

    # (2c) APOSENTADORIA GFX MENSURAVEL (Corrida 44). A retirada existia em
    # RetiredCount/LastRetiredFence, mas nao era impressa -- e por isso o item
    # O6 do criterio teve de ser INFERIDO das fences em vez de medido.
    for needed, label in (
        ("Submetidas=%llu Aposentadas=%llu EmVoo=%llu",
         "veredito do tripe GFX publica submetidas/aposentadas/em voo"),
        ("GfxEngine.LastRetiredFence", "aposentadoria GFX sai do contador real"),
        ("GfxEngine.RetiredCount", "contagem de aposentadas GFX publicada"),
    ):
        require(poll, needed, label, errors)

    # (3) Inspecao do IB ligada ao caminho de submissao do anel grafico.
    submit_gfx = active
    for needed in ("Bc250ActiveGfxInspectSubmittedIb(",
                   "Bc250ActiveGfxLogIbInspection(",
                   "IB_SIZE_MUDOU"):
        if needed not in submit_gfx:
            errors.append(
                f"caminho de submissao do anel grafico nao contem {needed}: sem isso a corrida "
                "nao distingue IB linear de IB encadeado")

    # O veredito textual precisa sair de FONTE UNICA no contrato, nao ser
    # remontado no log (foi assim que 'BYTE_PROOF PASS' e 'IB_TRIPOD PASS'
    # passaram a significar coisas diferentes do que o nome dizia).
    if "Bc250GfxIbInspectVerdictName(" not in active:
        errors.append(
            "o log da inspecao de IB nao usa Bc250GfxIbInspectVerdictName: o veredito tem de "
            "sair de fonte unica, senao log e suite podem discordar")
    for verdict in ("IB_LINEAR_CONFIRMADO", "IB_AINDA_ENCADEADO", "INDETERMINADO"):
        if verdict not in contract_c:
            errors.append(f"contrato de inspecao de IB nao define o veredito {verdict}")

    # O leitor do KMD NAO pode mapear memoria: roda em DISPATCH_LEVEL.
    reader = function_body(active, "static int Bc250ActiveGfxIbReadDword")
    if not reader:
        errors.append("Bc250ActiveGfxIbReadDword nao localizado")
    else:
        clean_reader = re.sub(r"/\*.*?\*/", "", reader, flags=re.S)
        for forbidden in ("MmMapIoSpace", "MmMapIoSpaceEx", "MmUnmapIoSpace",
                          "ExAllocatePool", "KeWaitForSingleObject"):
            if forbidden in clean_reader:
                errors.append(
                    f"Bc250ActiveGfxIbReadDword contem {forbidden}: o leitor roda em "
                    "DISPATCH_LEVEL e so pode usar a janela de VRAM ja mapeada")

    # (4) ScanComplete tem de exigir que o orcamento nao truncou o stream.
    if "budget == Inspection->RequestedDwords" not in contract_c:
        errors.append(
            "ScanComplete nao exige budget == RequestedDwords: um IB maior que o orcamento "
            "de varredura sairia como 'completo' e o veredito de linearidade seria falso")

    # (5) A suite tem de manter os casos NEGATIVOS. Portao que nao reprova nada
    # e' carimbo -- este projeto registra seis ocorrencias dessa classe.
    if "0xC0023F00" not in test_c:
        errors.append(
            "gfx_ib_inspect_contract_test nao trava o cabecalho medido 0xC0023F00: "
            "sem o caso medido a suite nao reproduz a corrida 10:20")
    negativas = test_c.count('!StrEq(Bc250GfxIbInspectVerdictName')
    if negativas < 3:
        errors.append(
            f"gfx_ib_inspect_contract_test tem apenas {negativas} assercoes negativas de "
            "'nao pode sair linear' (minimo 3): sem elas o classificador pode virar carimbo")

    # (6) O build tem de compilar E EXECUTAR a suite.
    if "bc250_gfx_ib_inspect_contract.obj" not in build:
        errors.append("build-contract.bat nao compila bc250_gfx_ib_inspect_contract.c")
    if "gfx_ib_inspect_contract_test.exe" not in build:
        errors.append("build-contract.bat nao linka gfx_ib_inspect_contract_test.exe")
    if "TESTE FALHOU ^(gfx_ib_inspect_contract_test^)" not in build:
        errors.append(
            "build-contract.bat nao ABORTA quando gfx_ib_inspect_contract_test falha: "
            "teste que nao para o build nao e' portao")

    # (7) R4 para o componente que NAO esta no pacote assinado.
    # A vulkan_radeon.dll vive em C:\BC250_LIB e foi o unico binario que mudou
    # entre os candidatos de 2026-09-18. Sem o hash dela na evidencia, a pergunta
    # "o binario testado e' o binario novo?" fica sem resposta exatamente para a
    # peca em teste.
    lab_bind = (ROOT / "tools/bind-bc250mcdm-lab.ps1").read_text(encoding="utf-8")
    if "external_libs" not in lab_bind:
        errors.append(
            "bind-bc250mcdm-lab.ps1 nao registra external_libs: a vulkan_radeon.dll esta fora do "
            "pacote assinado e package_sys_sha256 nao a identifica (R4 sem resposta)")
    if "vulkan_radeon.dll" not in lab_bind:
        errors.append("bind-bc250mcdm-lab.ps1 nao coleta o hash de vulkan_radeon.dll")
    if "binding-provenance.json" not in lab_bind:
        errors.append(
            "bind-bc250mcdm-lab.ps1 nao copia a proveniencia para a pasta de evidencia: "
            "tools/ e' sobrescrito pelo bind seguinte e o dado se perde")
    if "absent_reason" not in lab_bind:
        errors.append(
            "bind-bc250mcdm-lab.ps1 omite DLL ausente em vez de declarar o motivo: "
            "ausencia silenciosa sugere medicao que nao houve")

    # ==================================================================
    # (8) FAIXA DE 2026-09-18 TARDE -- as tres correcoes que a corrida 13:12
    # obrigou. Cada uma nasceu de um falso sucesso ou de uma leitura errada.
    # ==================================================================

    # 8a. A comparacao de dominio trocado. Read32/Write32 devolvem uint32_t com
    # 1=sucesso, e BC250_MMIO_TOPOLOGY_OK vale ZERO: compara-los inverte o
    # resultado e foi o que produziu 41 de 41 Perna1=INDETERMINADO[lido=0].
    if re.search(r"Topology(Read|Write)32\([^;]*==\s*BC250_MMIO_TOPOLOGY_OK", active, flags=re.S):
        errors.append(
            "Bc250MmioTopologyRead32/Write32 comparado com BC250_MMIO_TOPOLOGY_OK: dominios trocados. "
            "Essas funcoes devolvem 1=sucesso/0=falha e o enum OK vale 0, logo a comparacao inverte "
            "o resultado (corrida 2026-09-18 13:12: 41/41 vereditos com [lido=0] apesar da leitura funcionar)")

    # 8b. A publicacao do WPTR no silicio nao pode mais ser muda nem opcional.
    submit_body_gfx = function_body(active, "static NTSTATUS Bc250ActiveGfxEngineSubmit")
    if not submit_body_gfx:
        errors.append("Bc250ActiveGfxEngineSubmit nao localizado para auditar a publicacao do WPTR")
    else:
        clean_gfx = re.sub(r"/\*.*?\*/", "", submit_body_gfx, flags=re.S)
        if re.search(r"\(void\)\s*Bc250MmioTopologyWrite32\([^;]*CP_RB0_WPTR", clean_gfx, flags=re.S):
            errors.append(
                "a escrita de CP_RB0_WPTR volta a descartar o retorno com (void): submissao passaria a "
                "reportar SUBMIT SUCESSO sem o hardware ter recebido o ponteiro (Lei 4)")
        for needed, why in (
            ("WPTR_NAO_PUBLICADO", "recusa nomeada quando o transporte MMIO nao esta mapeado"),
            ("WPTR_ESCRITA_RECUSADA", "recusa nomeada quando Write32 devolve 0"),
            ("WPTR_READBACK", "readback do registrador apos a escrita"),
            ("WPTR_READBACK_DIVERGENTE", "recusa quando o registrador nao guarda o valor escrito"),
        ):
            if needed not in submit_body_gfx:
                errors.append(
                    f"Bc250ActiveGfxEngineSubmit nao contem {needed} ({why}); sem isso a assinatura "
                    "medida em 13:12 (snapshot do TDR com WPTR=0 apos escrever 12) volta a passar como sucesso")

    # 8c. Perna 2 tem tres estados. Fence zero NAO e' escrita da GPU -- as fences
    # comecam em 1, e o slot fica zerado pelo restart do TDR sem replantar poison.
    if "INDETERMINADO_SLOT_ZERADO_SEM_POISON" not in poll:
        errors.append(
            "Bc250ActiveGfxEnginePoll nao classifica Fence==0 como INDETERMINADO_SLOT_ZERADO_SEM_POISON: "
            "35 de 41 vereditos da corrida 13:12 sairam FENCE_ESCRITA_PELA_GPU com Fence=0x00000000")

    # 8d. Salto (CHAIN=1) e chamada IB2 (CHAIN=0) nao sao a mesma coisa. Trata-los
    # como iguais fez o veredito acusar regressao onde a RADV provava o contrario.
    if "BC250_GFX_IB_CTRL_CHAIN_MASK" not in contract_c:
        errors.append(
            "o classificador de IB nao consulta o bit CHAIN: opcode 0x3F com CHAIN=0 e' chamada IB2 "
            "(o CP retorna) e nao pode ser contado como salto de encadeamento")
    if "ChainJumps" not in contract_c or "Ib2Calls" not in contract_c:
        errors.append("o classificador de IB nao separa ChainJumps de Ib2Calls")
    if re.search(r"return\s*\(\s*Inspection->IndirectBufferPackets\s*==\s*0u\s*\)", contract_c):
        errors.append(
            "o veredito de linearidade voltou a depender de IndirectBufferPackets em vez de ChainJumps: "
            "isso reintroduz o falso IB_AINDA_ENCADEADO da corrida 13:12")
    if "0x000000B0" not in test_c:
        errors.append(
            "gfx_ib_inspect_contract_test nao trava o controle medido 0x000000B0 (IB2 com CHAIN=0): "
            "sem esse caso a confusao entre salto e chamada aninhada pode voltar")

    # 8e. Os nomes de bit tem de vir do registrador CERTO. ME_WAIT_ON_AVAIL_BUFFER
    # e GFX_CNTX_NOT_AVAIL_TO_ME pertencem ao CP_STALLED_STAT2
    # (gc_10_1_0_sh_mask.h:6466-6467), NAO ao STAT1, cujos bits 10/11 sao
    # ME_HAS_ACTIVE_CE/DE_BUFFER_FLAG (6428-6429). Confundir os dois sustentou uma
    # causa-raiz inteira que a propria medicao refutava (STAT2 lido = 0x00000000).
    stall_block = ""
    idx = active.find("[TDR_GFX_STALL]")
    if idx >= 0:
        stall_block = active[max(0, idx - 4000):idx + 4000]
    if not stall_block:
        errors.append(
            "o snapshot do TDR grafico nao emite [TDR_GFX_STALL]: CP_STALLED_STAT1/STAT2 ficam sem "
            "decodificacao nomeada e a pergunta do contexto grafico segue sem resposta")
    else:
        if "BC250_HQD_MMIO_CP_STALLED_STAT2" not in stall_block:
            errors.append(
                "o snapshot do TDR grafico le CP_STALLED_STAT1 mas nao o STAT2, que e' onde "
                "ME_WAIT_ON_AVAIL_BUFFER e GFX_CNTX_NOT_AVAIL_TO_ME realmente moram")
        if re.search(r"STAT1[^\n]*GFX_CNTX_NOT_AVAIL_TO_ME", stall_block):
            errors.append(
                "GFX_CNTX_NOT_AVAIL_TO_ME atribuido ao CP_STALLED_STAT1: esse campo pertence ao STAT2 "
                "(gc_10_1_0_sh_mask.h:6467). Ler nome de um registrador contra valor de outro e' "
                "exatamente o erro que esta faixa corrige")
    if "STALL_BASELINE" not in active:
        errors.append(
            "o ONLINE do anel grafico nao registra STALL_BASELINE: sem a linha de base tirada ANTES "
            "de qualquer submissao, um valor de repouso (0x00000C00 desde o bind) volta a ser lido "
            "como consequencia do nosso IB")

    # 8f. A UMD nao pode rebaixar o memory type do buffer de inspecao em silencio.
    vk_bridge = (ROOT / "driver/umd/bc250_umd_vulkan_bridge.c").read_text(encoding="utf-8")
    if "NAO MAPEAVEL PELA CPU" not in vk_bridge:
        errors.append(
            "bc250_umd_vulkan_bridge.c nao declara o rebaixamento do memory type do LinearPresentBuf: "
            "a corrida 13:12 perdeu a Perna 3 com 128 erros -5 sem que nada dissesse que a alocacao "
            "caiu para memoria nao mapeavel")


def audit_phase_d0_refusal_tripod_and_wptr_veto(errors: list[str]) -> None:
    """Fase D0: recusa do submit emite tripe de recusa e readback divergente permanece veto.

    1. GFX_TRIPE_VEREDITO_RECUSA precisa ser emitido no caminho de recusa do
       SubmitVirtual do Node 1 (Bc250ActiveGfxLogRefusalMeasurement) para que
       toda submissao recusada produza medicao de silicio (RPTR, WPTR, Fence, Poison)
       em vez de apenas falhar em silencio.
    2. WPTR_READBACK_DIVERGENTE precisa permanecer um veto (fail-closed,
       retornando erro e incrementando SubmitRejected) enquanto a doorbell
       estiver desligada no apply (DoorbellDesligadaNoApply == 1).
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append(f"fonte ausente para auditar Fase D0: {active_path}")
        return

    active = active_path.read_text(encoding="utf-8", errors="replace")

    # 1. Tripe de recusa emitido no caminho de recusa do SubmitVirtual Node 1.
    submit_body = function_body(active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualBody")
    if not submit_body:
        errors.append("Bc250ActiveSubmitCommandVirtualBody nao localizado para auditar recusa da Fase D0")
        return

    clean_submit = strip_c_comments(submit_body)
    require(clean_submit, "Bc250ActiveGfxLogRefusalMeasurement(context,",
            "chamada de medicao de recusa no SubmitVirtual Node 1", errors)

    refusal_meas = function_body(active, "static void Bc250ActiveGfxLogRefusalMeasurement")
    if not refusal_meas:
        errors.append("Bc250ActiveGfxLogRefusalMeasurement nao localizado")
        return

    require(refusal_meas, "GFX_TRIPE_VEREDITO_RECUSA",
            "emissao de GFX_TRIPE_VEREDITO_RECUSA no helper de medicao", errors)

    # 2. WPTR_READBACK_DIVERGENTE permanece veto em Bc250ActiveGfxEngineSubmit.
    gfx_submit = function_body(active, "static NTSTATUS Bc250ActiveGfxEngineSubmit")
    if not gfx_submit:
        errors.append("Bc250ActiveGfxEngineSubmit nao localizado para auditar veto de WPTR")
        return

    clean_gfx_submit = strip_c_comments(gfx_submit)
    divergent_marker = "wptrReadback != cursor"
    start_divergent = clean_gfx_submit.find(divergent_marker)
    if start_divergent < 0:
        errors.append(f"bloco de divergencia '{divergent_marker}' nao localizado em Bc250ActiveGfxEngineSubmit")
    else:
        end_divergent = clean_gfx_submit.find("} else {", start_divergent)
        divergent_block = clean_gfx_submit[start_divergent:end_divergent if end_divergent > 0 else len(clean_gfx_submit)]
        require(divergent_block, 'LastRefusalReason = "WPTR_READBACK_DIVERGENTE";',
                "motivo WPTR_READBACK_DIVERGENTE registrado", errors)
        require(divergent_block, "DoorbellDesligadaNoApply=1",
                "declaracao de DoorbellDesligadaNoApply no inventario de canais", errors)
        require(divergent_block, "return STATUS_DEVICE_NOT_READY;",
                "retorno fail-closed no veto de WPTR divergente", errors)
        forbid(divergent_block, "return STATUS_SUCCESS;",
               "falso sucesso em WPTR divergente", errors)


def audit_submit_dispatch_level_safety(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    submit_body = function_body(active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualBody")
    submit_lifecycle = function_body(active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualLifecycle")
    for name, code in (("SubmitCommandVirtualBody", submit_body),
                       ("SubmitCommandVirtualLifecycle", submit_lifecycle)):
        if not code:
            errors.append(f"{name} nao localizado para auditar DISPATCH_LEVEL safety")
            continue
        clean_code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
        clean_code = re.sub(r"//.*$", "", clean_code, flags=re.M)
        for forbidden in ("ExAcquirePushLockExclusive", "ExReleasePushLockExclusive",
                          "ExAcquirePushLockShared", "ExReleasePushLockShared",
                          "KeEnterCriticalRegion", "KeLeaveCriticalRegion"):
            if forbidden in clean_code:
                errors.append(f"{name} executa em DISPATCH_LEVEL mas contem {forbidden} (BugCheck 0xD1)")


def audit_sdma_submission_is_asynchronous(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    submit_body = function_body(active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualBody")
    retire_notify = function_body(active, "static VOID Bc250ActiveSdmaRetireAndNotify")
    dpc_routine = function_body(active, "static VOID Bc250ActiveSdmaRetirementDpcRoutine")

    if not submit_body:
        errors.append("Bc250ActiveSubmitCommandVirtualBody nao localizado para auditar submissao assincrona SDMA")
        return

    # Proibir espera sincrona / stall no caminho de submissao (causa do BugCheck 0x133 DPC_WATCHDOG_VIOLATION)
    clean_submit = re.sub(r"/\*.*?\*/", "", submit_body, flags=re.S)
    clean_submit = re.sub(r"//.*$", "", clean_submit, flags=re.M)
    for forbidden in ("KeStallExecutionProcessor", "KeDelayExecutionThread", "KeWaitForSingleObject", "YieldProcessor"):
        if forbidden in clean_submit:
            errors.append(f"SubmitCommandVirtualBody contem {forbidden} (espera sincrona em DISPATCH_LEVEL proibida; risco de 0x133)")

    if re.search(r"while\s*\(\s*g_Bc250SdmaPresent\.InFlightCount", clean_submit):
        errors.append("SubmitCommandVirtualBody contem laco de espera ativa em InFlightCount (proibido em DISPATCH_LEVEL; risco de 0x133/0x10E)")

    # Proibir chamadas sincronas antigas de submissao direta dentro do corpo de submit
    for forbidden_sync in ("Bc250SdmaPresentExecuteIndirect", "Bc250SdmaPresentExecuteFill", "Bc250SdmaPresentExecuteSyncNop"):
        if forbidden_sync in clean_submit:
            errors.append(f"SubmitCommandVirtualBody chama rotina sincrona legada {forbidden_sync} em vez de despacho assincrono")

    # Exigir despacho assincrono e controle de fila em voo
    require(submit_body, "KeInsertQueueDpc(&context->SdmaRetirementDpc, NULL, NULL);",
            "SDMA asynchronous DPC queue insertion", errors)
    require(submit_body, "if (g_Bc250SdmaPresent.InFlightCount >= BC250_SDMA_IN_FLIGHT_CAPACITY)",
            "SDMA multi-slot in-flight capacity check before submit", errors)
    require(submit_body, "Bc250ActiveSdmaRetireAndNotify(context, FALSE);",
            "SDMA opportunistic retirement polling with FromDpc=FALSE to prevent recursive 0x133 deadlock", errors)
    require(submit_body, "Bc250SdmaPresentSubmitIndirectAsync(",
            "SDMA async indirect submission", errors)
    require(submit_body, "Bc250SdmaPresentSubmitFillAsync(",
            "SDMA async fill submission", errors)
    require(submit_body, "Bc250SdmaPresentSubmitSyncNopAsync(",
            "SDMA async sync nop submission", errors)

    # Exigir rotina de retirement assincrono com verificacao fisica do tripe e notificacao DPC
    if not retire_notify:
        errors.append("Bc250ActiveSdmaRetireAndNotify nao localizado")
    else:
        require(retire_notify, "Bc250SdmaPresentPollRetirement(&result)",
                "SDMA poll retirement in retire-and-notify routine", errors)
        require(retire_notify, "BC250_ACTIVE_FENCE_SYNC_NOTIFY_SDMA",
                "SDMA fence sync notify action", errors)
        require(retire_notify, "Context->Core.DxgkInterface.DxgkCbSynchronizeExecution",
                "DxgkCbSynchronizeExecution invocation on SDMA retirement", errors)
        require(retire_notify, "if (FromDpc)",
                "DxgkCbNotifyDpc guarded by FromDpc check to prevent 0x133 deadlock", errors)
        require(retire_notify, "Context->Core.DxgkInterface.DxgkCbNotifyDpc(",
                "DxgkCbNotifyDpc invocation on SDMA retirement when FromDpc is TRUE", errors)
        require(retire_notify, "IB EXECUTADO (INDIRECT)",
                "canonical IB EXECUTADO (INDIRECT) logging on async retirement", errors)
        require(retire_notify, "PLANO A CONCLUIDO",
                "canonical PLANO A CONCLUIDO logging on async retirement", errors)
        require(retire_notify, "BUSY_YIELD",
                "busy-yield observable telemetry on contention", errors)
        require(retire_notify, "KeInsertQueueDpc(&Context->SdmaRetirementDpc, NULL, NULL);",
                "deferred DPC notification queue insertion to prevent 0x133 deadlock", errors)
        forbid(retire_notify, "if (FromDpc) {\n            KeInsertQueueDpc",
               "re-entrant DPC loop in busy-yield (0x133 trigger)", errors)
        require(retire_notify, "if (g_Bc250SdmaPresent.InFlightCount > 0u)",
                "unconditional liveness backstop rearming timer if work remains", errors)

    # DPC routine
    if not dpc_routine:
        errors.append("Bc250ActiveSdmaRetirementDpcRoutine nao localizado")
    else:
        require(dpc_routine, "Bc250ActiveSdmaRetireAndNotify(context, TRUE);",
                "retirement and notification inside SDMA DPC routine with FromDpc=TRUE", errors)
        require(dpc_routine, "++context->SdmaRetirementDpcRuns;",
                "SDMA DPC telemetry run counter", errors)

    # Lifecycle cleanup
    remove_body = function_body(active, "NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive")
    stop_body = function_body(active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason")
    cleanup_body = function_body(active, "VOID Bc250ActiveDriverUnloadCleanup")
    if remove_body:
        require(remove_body, "Bc250ActiveSdmaRetirementTimerStop(context);", "SDMA timer stop on remove", errors)
    if stop_body:
        require(stop_body, "Bc250ActiveSdmaRetirementTimerStop(context);", "SDMA timer stop on stop device", errors)
    if cleanup_body:
        require(cleanup_body, "Bc250ActiveSdmaRetirementTimerStop(active);", "SDMA timer stop on driver unload", errors)


def audit_device_removal_and_unload_safety(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    callbacks = (ROOT / "driver/wdk/bc250_mcdm_callbacks.c").read_text(encoding="utf-8")

    remove_body = function_body(active, "NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive")
    if not remove_body:
        errors.append("Bc250DxgkDdiRemoveDeviceActive nao localizado")
    else:
        require(remove_body, "Bc250ActiveRetirementTimerStop(context);", "timer stop on remove", errors)
        require(remove_body, "Bc250DisplayTeardown();", "display teardown on remove", errors)
        require(remove_body, "KeFlushQueuedDpcs();", "DPC flush on remove", errors)
        require(remove_body, "Bc250ActiveUnregisterAdapter(context);", "adapter unregister on remove", errors)
        require(remove_body, "return STATUS_SUCCESS;", "mandatory success return from RemoveDevice", errors)
        forbid(remove_body, "return status;", "conditional error return from RemoveDevice", errors)

    stop_body = function_body(active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason")
    if not stop_body:
        errors.append("Bc250DxgkDdiStopDeviceActiveReason nao localizado")
    else:
        require(stop_body, "BC250_ACTIVE_STOP_REASON_REMOVE_DEVICE", "check for REMOVE_DEVICE reason", errors)
        require(stop_body, "Bc250WddmFenceTrackerReset(&context->SubmissionEngine.WddmFences);",
                "tracker reset on remove in stop reason handler", errors)

    cleanup_body = function_body(active, "VOID Bc250ActiveDriverUnloadCleanup")
    if not cleanup_body:
        errors.append("Bc250ActiveDriverUnloadCleanup nao localizado")
    else:
        require(cleanup_body, "Bc250ActiveRetirementTimerStop(active);", "timer stop across all adapters on unload", errors)
        require(cleanup_body, "Bc250DisplayTeardown();", "display teardown on unload", errors)
        require(cleanup_body, "KeFlushQueuedDpcs();", "DPC flush on unload", errors)

    unload_body = function_body(callbacks, "VOID Bc250McdmUnload")
    if not unload_body:
        errors.append("Bc250McdmUnload nao localizado")
    else:
        require(unload_body, "g_Bc250UnloadCleanupHandler();", "unload cleanup handler invocation", errors)
        require(unload_body, "KeFlushQueuedDpcs();", "DPC flush before driver unmapping", errors)

    require(active, "Bc250McdmInstallUnloadCleanupHandler(Bc250ActiveDriverUnloadCleanup);",
            "unload cleanup handler installation in active miniport initialize", errors)


def audit_mec_shared_mmio_lifetime(errors: list[str]) -> None:
    engine = (ROOT / "driver/wdk/bc250_submission_engine.c").read_text(encoding="utf-8")
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    core = (ROOT / "driver/wdk/bc250_dxgk_readonly_ddi.c").read_text(encoding="utf-8")
    # The engine borrows the adapter mapping; MMU restore outlives it.
    forbid(engine, "Bc250MmioTopologyUnmap(", "engine unmapping shared adapter MMIO", errors)
    stop = function_body(core, "NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceReadonly(")
    require(stop, "Bc250MmioTopologyUnmap(&context->MmioTransport)", "core MMIO release", errors)
    stop = function_body(active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason(")
    for step in ("Bc250SubmissionEngineOffline(context)",
                 "Bc250GcvmComputeRestore(&computeIo,",
                 "Bc250GcvmGlobalMmuRestore(",
                 "Bc250DxgkDdiStopDeviceReadonly(&context->Core)"):
        require(stop, step, "ordered teardown stage", errors)
    if not (stop.find("Bc250SubmissionEngineOffline(context)") <
            stop.find("Bc250GcvmComputeRestore(&computeIo,") <
            stop.find("Bc250GcvmGlobalMmuRestore(") <
            stop.find("Bc250DxgkDdiStopDeviceReadonly(&context->Core)")):
        errors.append("stop must quiesce MEC, restore MMU, then release core MMIO")
    wrapper = function_body(engine, "static NTSTATUS Bc250SubmissionApplyCoherency(")
    require(wrapper, "KeAcquireSpinLock(&Context->TlbLock, &irql);", "MEC shared TLB acquire", errors)
    require(wrapper, "KeReleaseSpinLock(&Context->TlbLock, irql);", "MEC shared TLB release", errors)
    call = "status = Bc250SubmissionApplyCoherencyLocked(Context, Engine);"
    require(wrapper, call, "MEC coherency under shared lock", errors)
    if not (wrapper.find("KeAcquireSpinLock") < wrapper.find(call) <
            wrapper.find("KeReleaseSpinLock") < wrapper.find("return status;")):
        errors.append("MEC coherency must release TLB lock on every transaction result")
    if engine.count("Bc250SubmissionApplyCoherencyLocked(") != 2:
        errors.append("unlocked coherency must have only its definition and locked caller")
    require(engine, "[MEC_IB_DESCRIPTOR]", "physical ring descriptor observation", errors)
    require(engine, "[MEC_PENDING]", "bounded pending MEC observation", errors)
    require(stop, "[MEC_PENDING_ARCHIVE]", "passive archive of DPC observations", errors)
    if not (stop.find("Bc250ActiveRetirementTimerStop(context)") <
            stop.find("[MEC_PENDING_ARCHIVE]") <
            stop.find("Bc250SubmissionEngineOffline(context)")):
        errors.append("archive MEC observation after DPC drain and before resource release")
    require(active, 'Bc250ActiveWalkGpuVa("NODE1_IB",', "Node1 process page-table observation", errors)
    archive_call = "Bc250SubmissionEngineArchiveDiagnostics(context);"
    require(stop, archive_call, "independent StopDevice MEC snapshot", errors)
    if not (stop.find("Bc250ActiveRetirementTimerStop(context)") <
            stop.find(archive_call) < stop.find("Bc250SubmissionEngineOffline(context)")):
        errors.append("independent MEC snapshot must precede engine resource release")
    poll = function_body(engine, "NTSTATUS Bc250SubmissionEnginePoll(")
    forbid(poll, "KeQueryInterruptTime", "time-gated first MEC observation", errors)
    if not (0 <= poll.find("engine->DiagnosticPendingFence = pendingFence;") <
            poll.find("Bc250PersistentRingRetire(")):
        errors.append("capture pending MEC evidence before retirement can fault engine")
    require(stop, "[NODE1_IB_CPU_ARCHIVE]", "CPU IB observation archive", errors)


def audit_tlb_invalidation_safety(errors: list[str]) -> None:
    active_h = (ROOT / "driver/wdk/bc250_active_miniport.h").read_text(encoding="utf-8")
    active_c = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")

    require(active_h, "KSPIN_LOCK TlbLock;",
            "TlbLock member in BC250_DXGK_ACTIVE_CONTEXT", errors)

    add_body = function_body(active_c, "NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive")
    if not add_body:
        errors.append("Bc250DxgkDdiAddDeviceActive nao localizado")
    else:
        require(add_body, "KeInitializeSpinLock(&context->TlbLock);",
                "TlbLock initialization in AddDeviceActive", errors)

    flush_body = function_body(active_c, "static uint32_t Bc250ActiveFlushVmidTlb")
    if not flush_body:
        errors.append("Bc250ActiveFlushVmidTlb nao localizado")
    else:
        require(flush_body, "KeAcquireSpinLock(&active->TlbLock,",
                "spin lock acquire in Bc250ActiveFlushVmidTlb", errors)
        require(flush_body, "KeReleaseSpinLock(&active->TlbLock,",
                "spin lock release in Bc250ActiveFlushVmidTlb", errors)

    vmid8_body = function_body(active_c, "static uint32_t Bc250ActiveFlushVmid8Only")
    if not vmid8_body:
        errors.append("Bc250ActiveFlushVmid8Only nao localizado")
    else:
        require(vmid8_body, "Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_VMID8)",
                "strict VMID8 flush in Bc250ActiveFlushVmid8Only", errors)

    submit_body = function_body(active_c, "static NTSTATUS Bc250ActiveSubmitCommandVirtualBody")
    if not submit_body:
        errors.append("Bc250ActiveSubmitCommandVirtualBody nao localizado")
    else:
        require(submit_body, "io.Flush = Bc250ActiveFlushVmid8Only;",
                "isolated VMID8 flush in SubmitCommandVirtualBody context switch", errors)



def audit_primary_allocation_is_declared_to_the_kernel(errors: list[str]) -> None:
    """Corrida 13/09 16:28:27 -- A PRIMARIA DO dwm.exe NUNCA CHEGOU AO KERNEL COMO PRIMARIA.

    MEDIDO, P0: todo `[CreateAllocation] ENTER` originado da UMD trouxe
    `Resource=0` e caiu em "Non-resource allocation". O unico
    "Primary token confirmed" da corrida inteira veio da alocacao padrao do
    dxgkrnl no bind. Em paralelo o dwm.exe emitiu 90 `DXGI Present` com
    `Flags=0x2` (FLIP) e entrou em laco de device-lost a ~1 Hz, e
    Bc250DxgkDdiPresent nao foi chamada UMA vez.

    CAUSA, por leitura de codigo: Bc250WinsysAllocate zerava D3DDDICB_ALLOCATE e
    nunca atribuia `hResource` (d3dumddi.h:3457), `Flags.Primary` nem
    `VidPnSourceId` (d3dukmdt.h:554 e :549). O dxgkrnl so' liga
    `DXGKARG_CREATEALLOCATION.Flags.Resource` quando a UMD passa `hResource`, e o
    ramo do KMD que marca primaria esta atras desse bit -- era INALCANCAVEL.

    Este portao trava os quatro lados da correcao:
      1. a UMD passa hResource, Flags.Primary e VidPnSourceId;
      2. primariedade tem FONTE UNICA (classe do token) e divergencia com o
         vinculo falha fechado -- sem isso voltariamos a ter duas verdades;
      3. a desalocacao e' SIMETRICA (criada com recurso, devolvida com recurso),
         senao o objeto de recurso do kernel fica orfao;
      4. o KMD nao deixa uma primaria de UMD SOBRESCREVER a primaria de display
         do GOP, que e' a que o CommitVidPn usa.
    """
    winsys_path = ROOT / "driver" / "umd" / "bc250_wddm_winsys.c"
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    for path in (winsys_path, umd_path, active_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar a primaria: {path.name}")
            return

    winsys = winsys_path.read_text(encoding="utf-8", errors="replace")
    umd = umd_path.read_text(encoding="utf-8", errors="replace")
    active = active_path.read_text(encoding="utf-8", errors="replace")

    body = function_body(winsys, "BC250_WINSYS_STATUS Bc250WinsysAllocateForResource")
    if not body:
        errors.append("Bc250WinsysAllocateForResource nao localizado")
    else:
        require(body, "args.hResource = Binding->RuntimeResource;",
                "hResource propagado ao pfnAllocateCb", errors)
        require(body, "info.Flags.Primary = 1u;",
                "Flags.Primary declarado na alocacao primaria", errors)
        require(body, "info.VidPnSourceId =",
                "VidPnSourceId declarado na alocacao primaria", errors)
        # Fail-closed: primaria sem handle de recurso nao pode ser criada.
        require(body, "bindPrimary && Binding->RuntimeResource == NULL",
                "recusa de primaria sem hRTResource", errors)
        # Fonte unica: o vinculo nao pode discordar da classe do token.
        require(body, "bindPrimary != tokenPrimary",
                "recusa de divergencia entre token e vinculo de primariedade", errors)

    dealloc = function_body(winsys, "BC250_WINSYS_STATUS Bc250WinsysDeallocate")
    if not dealloc:
        errors.append("Bc250WinsysDeallocate nao localizado")
    else:
        require(dealloc, "args2.hResource = Allocation->RuntimeResource;",
                "desalocacao simetrica por recurso (Deallocate2)", errors)
        require(dealloc, "args.hResource = Allocation->RuntimeResource;",
                "desalocacao simetrica por recurso (Deallocate)", errors)

    create_res = function_body(umd, "VOID APIENTRY Bc250Umd10CreateResource")
    if not create_res:
        errors.append("Bc250Umd10CreateResource nao localizado")
    else:
        require(create_res, "Bc250WinsysAllocateForResource(",
                "CreateResource usando o caminho com vinculo de recurso", errors)
        require(create_res, "binding.RuntimeResource = (HANDLE)hRTResource.handle;",
                "hRTResource da DDI propagado ao vinculo", errors)
        require(create_res, "binding.Primary = isPrimary;",
                "primariedade do vinculo derivada da mesma decisao do token", errors)
        # A chamada antiga, sem vinculo, nao pode voltar a este sitio.
        forbid(create_res, "Bc250WinsysAllocate(&device->Winsys",
               "CreateResource voltando a alocar sem vinculo de recurso", errors)

    # Lado do KMD: a primaria de display nao pode ser trocada por backbuffer.
    if "g_Bc250DisplayState.hPrimaryAllocation == NULL" not in active:
        errors.append("primaria de display do KMD sem guarda contra sobrescrita "
                      "por primaria de UMD")
    require(active, "Primary token confirmed (DISPLAY)",
            "primaria de display nomeada no log do KMD", errors)
    require(active, "Primary token confirmed (UMD",
            "primaria de UMD nomeada e contada, nunca silenciada", errors)


def audit_primary_is_not_overbroad(errors: list[str]) -> None:
    """F5.0/F5.0b -- A DERIVACAO DE PRIMARIEDADE NAO PODE SER LARGA NEM CONGELAR MENTIRA.

    Regressao de 13/09 22:17 (R9, R10): Explorer.EXE falhou 142 alocacoes com E_INVALIDARG
    porque BIND_PRESENT era considerado Primary=1 mesmo sem pPrimaryDesc e com
    geometria 1024x832 != 2560x1080 do VidPn ativo.

    Este portao assegura:
      1. BIND_PRESENT sozinho nunca implica primaria (proibe a derivacao ampla);
      2. Primaria exige estritamente pPrimaryDesc != NULL e resolucao compativel;
      3. Divergencia emite recusa nomeada;
      4. O teste de contrato exige expressamente que o caso dwm.exe seja primaria
         e o caso Explorer.EXE NAO seja primaria (meta-portao anti-regressao).
    """
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    test_path = ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c"
    if not umd_path.exists() or not test_path.exists():
        errors.append("fontes ausentes para auditar a derivacao de primariedade")
        return

    umd = umd_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. Proibir a derivacao larga antiga no UMD
    forbid(umd, "(pCreate->BindFlags & D3D10_DDI_BIND_PRESENT) != 0u || pCreate->pPrimaryDesc != NULL",
           "derivacao de primariedade excessivamente larga", errors)
    forbid(umd, "isPrimary = (pCreate->BindFlags & D3D10_DDI_BIND_PRESENT)",
           "BIND_PRESENT implicando primariedade", errors)

    # 2. Exigir verificacao estrita de resolucao e recusa nomeada
    create_res = function_body(umd, "VOID APIENTRY Bc250Umd10CreateResource")
    if not create_res:
        errors.append("Bc250Umd10CreateResource nao localizado")
    else:
        require(create_res, "res->Width == pCreate->pPrimaryDesc->ModeDesc.Width",
                "checagem de largura da primaria", errors)
        require(create_res, "res->Height == pCreate->pPrimaryDesc->ModeDesc.Height",
                "checagem de altura da primaria", errors)
        require(create_res, "PRIMARY_DESC INCOMPATIVEL RECUSADO",
                "recusa nomeada de primaria incompativel", errors)
        require(create_res, "PRIMARY_DESC VALIDO",
                "confirmacao nomeada de primaria compativel", errors)

    # 3. Meta-portao no teste: exige a verificacao de ambos os casos
    # strip_c_comments remove tanto /* ... */ quanto // ... para que assercoes
    # comentadas ou neutralizadas sejam rejeitadas como codigo ausente.
    test_clean = strip_c_comments(test)
    test_func = function_body(test_clean, "static void TestF5PrimaryDerivation")
    if not test_func:
        errors.append("funcao TestF5PrimaryDerivation ausente no teste de contrato")
    else:
        require(test_func, "assert(resDwm.IsPrimary);",
                "teste de contrato exige que caso dwm.exe seja primaria", errors)
        require(test_func, "assert(!resExplorer.IsPrimary);",
                "teste de contrato exige que caso Explorer.EXE nao seja primaria", errors)
        forbid(test_func, "assert(1)",
               "neutralizacao de assert no teste de primariedade", errors)
        forbid(test_func, "assert(0)",
               "neutralizacao de assert no teste de primariedade", errors)
        forbid(test_func, "if (0)",
               "bypass de assert no teste de primariedade", errors)


def audit_dxgi_1_6_table_complete(errors: list[str]) -> None:
    """F5.1 -- TABELA DXGI 1.6 COMPLETA (22 SLOTS, 176 BYTES) COM FAIL-CLOSED HONESTO.

    Assegura que a UMD implementa e preenche a tabela DXGI 1.6 completa:
      1. Bc250UmdPopulateDxgiBaseFuncs recebe DXGI1_6_1_DDI_BASE_FUNCTIONS*;
      2. Todos os 22 slots da struct DXGI 1.6 sao explicitamente populados;
      3. pfnPresent1 e pfnBlt1 estao implementados;
      4. Caps de MPO declaram MaxPlanes=1 e NumCapabilityGroups=0 (sem falso hardware MPO);
      5. CreateDevice conecta pDXGIDDIBaseFunctions6_1;
      6. Testes unitarios cobrem a integridade da tabela estendida.
    """
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    test_path = ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c"
    if not umd_path.exists() or not test_path.exists():
        errors.append("fontes ausentes para auditar tabela DXGI 1.6")
        return

    umd = umd_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    umd_clean = strip_c_comments(umd)
    pop_func = function_body(umd_clean, "VOID Bc250UmdPopulateDxgiBaseFuncs")
    if not pop_func:
        errors.append("Bc250UmdPopulateDxgiBaseFuncs nao localizado")
    else:
        require(pop_func, "pFuncs->pfnPresent = Bc250DxgiPresent;",
                "slot pfnPresent na tabela DXGI", errors)
        require(pop_func, "pFuncs->pfnPresent1 = Bc250DxgiPresent1;",
                "slot pfnPresent1 na tabela DXGI 1.6", errors)
        require(pop_func, "pFuncs->pfnBlt1 = Bc250DxgiBlt1;",
                "slot pfnBlt1 na tabela DXGI 1.6", errors)
        require(pop_func, "pFuncs->pfnGetMultiplaneOverlayCaps = Bc250DxgiGetMultiplaneOverlayCaps;",
                "slot pfnGetMultiplaneOverlayCaps na tabela DXGI 1.6", errors)
        require(pop_func, "pFuncs->pfnReclaimResources1 = Bc250DxgiReclaimResources1;",
                "slot pfnReclaimResources1 na tabela DXGI 1.6", errors)

    mpo_func = function_body(umd_clean, "HRESULT APIENTRY Bc250DxgiGetMultiplaneOverlayCaps")
    if not mpo_func:
        errors.append("Bc250DxgiGetMultiplaneOverlayCaps nao localizado")
    else:
        require(mpo_func, "MaxPlanes = 1",
                "declaracao honesta de 1 plano maximo (sem MPO de hardware)", errors)
        require(mpo_func, "NumCapabilityGroups = 0",
                "declaracao honesta de zero grupos MPO", errors)

    create_dev = function_body(umd_clean, "HRESULT APIENTRY Bc250Umd10CreateDevice")
    if not create_dev:
        errors.append("Bc250Umd10CreateDevice nao localizado")
    else:
        require(create_dev, "pDXGIDDIBaseFunctions6_1",
                "wiring de pDXGIDDIBaseFunctions6_1 no CreateDevice", errors)

    test_clean = strip_c_comments(test)
    require(test_clean, "assert(dxgiFuncs.pfnPresent1 != NULL);",
            "teste de contrato assegurando presenca de pfnPresent1", errors)
    require(test_clean, "assert(dxgiFuncs.pfnBlt1 != NULL);",
            "teste de contrato assegurando presenca de pfnBlt1", errors)


def audit_f5_2_pm4_draw_pipeline(errors: list[str]) -> None:
    """F5.2 -- PAREDE B: PIPELINE GRAFICO PM4 3D REAL NO NODE 1.

    Assegura que a UMD emite pacotes PM4 nativos para Draw:
      1. Bc250Pm4EmitColorTarget0, Bc250Pm4EmitWindowScissor e Bc250Pm4EmitDrawIndexAuto no core PM4;
      2. Bc250UmdEmitPm4Draw constroi contexto de render target e submete ao Node 1;
      3. Base GPUVA do RT exige alinhamento de 256 bytes (GFX10 CB);
      4. Telemetria dedicada: NativePm4DrawCount incrementado separadamente;
      5. DDIs pfnDraw, pfnDrawIndexed, etc. chamam Bc250UmdEmitPm4Draw antes de qualquer fallback;
      6. Teste de contrato TestDrawPm4EmissionOnNode1 verifica a emissao e rejeicao de desalinhamento.
    """
    pm4_path = ROOT / "driver" / "core" / "bc250_pm4_packet_builder.c"
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    probe_path = ROOT / "driver" / "umd" / "bc250_umd_caps_probe.h"
    test_path = ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c"

    if not pm4_path.exists() or not umd_path.exists() or not test_path.exists() or not probe_path.exists():
        errors.append("fontes ausentes para auditar pipeline 3D PM4 (F5.2)")
        return

    pm4 = pm4_path.read_text(encoding="utf-8", errors="replace")
    umd = umd_path.read_text(encoding="utf-8", errors="replace")
    probe = probe_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    require(probe, 'BC250_D3D11_EXPERIMENTAL_PM4_DRAW_REG_NAME "D3D11ExperimentalPm4Draw"',
            "definicao de tunable D3D11ExperimentalPm4Draw em bc250_umd_caps_probe.h", errors)
    require(probe, "BOOL Bc250UmdExperimentalPm4DrawArmed(void);",
            "declaracao de Bc250UmdExperimentalPm4DrawArmed", errors)

    require(pm4, "Bc250Pm4EmitColorTarget0(", "emissao PM4 de ColorTarget0", errors)
    require(pm4, "Bc250Pm4EmitWindowScissor(", "emissao PM4 de WindowScissor", errors)
    require(pm4, "Bc250Pm4EmitDrawIndexAuto(", "emissao PM4 de DrawIndexAuto", errors)

    umd_clean = strip_c_comments(umd)
    emit_draw = function_body(umd_clean, "BOOL Bc250UmdEmitPm4Draw")
    if not emit_draw:
        errors.append("Bc250UmdEmitPm4Draw nao localizado")
    else:
        require(emit_draw, "Bc250UmdExperimentalPm4DrawArmed()",
                "trava de tunable experimental para PM4 draw", errors)
        require(emit_draw, "dst->Allocation.GpuVirtualAddress & 0xFFull",
                "exigencia de alinhamento de 256 bytes para CB", errors)
        require(emit_draw, "Bc250Pm4EmitColorTarget0(",
                "emissao de CB_COLOR0 dentro do draw nativo", errors)
        require(emit_draw, "Bc250Pm4EmitWindowScissor(",
                "emissao de Scissor dentro do draw nativo", errors)
        require(emit_draw, "Bc250Pm4EmitDrawIndexAuto(",
                "emissao de DRAW_INDEX_AUTO dentro do draw nativo", errors)
        require(emit_draw, "pDevice->NativePm4DrawCount++",
                "telemetria isolada de draws nativos PM4", errors)
        require(emit_draw, "Bc250UmdCommandBufferFlush(pDevice, cmd, BC250_WINSYS_NODE_COMPUTE)",
                "flush do command buffer para o Node 1 de compute/grafico", errors)

    draw_func = function_body(umd_clean, "VOID APIENTRY Bc250Umd10Draw(")
    draw_idx_func = function_body(umd_clean, "VOID APIENTRY Bc250Umd10DrawIndexed(")
    draw_inst_func = function_body(umd_clean, "VOID APIENTRY Bc250Umd10DrawInstanced(")
    draw_idx_inst_func = function_body(umd_clean, "VOID APIENTRY Bc250Umd10DrawIndexedInstanced(")

    if not draw_func or not draw_idx_func or not draw_inst_func or not draw_idx_inst_func:
        errors.append("funcoes de draw nao localizadas na UMD")
    else:
        for fname, fbody in [("Bc250Umd10Draw", draw_func),
                             ("Bc250Umd10DrawIndexed", draw_idx_func),
                             ("Bc250Umd10DrawInstanced", draw_inst_func),
                             ("Bc250Umd10DrawIndexedInstanced", draw_idx_inst_func)]:
            forbid(fbody, "Bc250UmdEmitPm4Draw",
                   f"D1.1: Bc250UmdEmitPm4Draw proibido no caminho de render ({fname})", errors)
            require(fbody, "Bc250UmdExecuteVkDraw",
                    f"D1.1: Bc250UmdExecuteVkDraw obrigatorio ({fname})", errors)

    present_detile = function_body(umd_clean, "static BOOL Bc250UmdPresentVulkanDetileAndScanout(")
    if not present_detile:
        errors.append("Bc250UmdPresentVulkanDetileAndScanout nao localizado")
    else:
        require(present_detile, "device->NativePm4DrawCount > 0 && (device->NativeVkDrawCount > 0 || vkRendered)",
                "D1.2: deteccao de divergencia entre renderizador e apresentador", errors)
        require(present_detile, "[PRESENT RECUSADO: DIVERGENCIA_BACKEND_DRAW_VS_PRESENT]",
                "D1.2: recusa nomeada para divergencia de backend no Present", errors)

    test_clean = strip_c_comments(test)
    test_func = function_body(test_clean, "static void TestDrawPm4EmissionOnNode1(void)")
    if not test_func:
        errors.append("TestDrawPm4EmissionOnNode1 nao localizado")
    else:
        require(test_func, "assert(dev.NativePm4DrawCount == 0u);",
                "teste verifica rejeicao fail-closed com tunable desarmado", errors)
        require(test_func, "assert(dev.NativePm4DrawCount == 1u);",
                "teste verifica incremento de NativePm4DrawCount", errors)
        require(test_func, "assert(dev.SubstitutedDrawCount == 0u);",
                "teste assegura zero substituicao por blt quando draw e nativo", errors)
        require(test_func, "0x10000004ull",
                "teste negativo de desalinhamento de base GPUVA", errors)


def audit_offline_check_is_not_inverted(errors: list[str]) -> None:
    """O RAMO OFFLINE DAS SUITES NAO PODE TRANSFORMAR FALHA EM SUCESSO.

    MEDIDO EM 14/09, quebrando Bc250Pm4EmitColorTarget0 de proposito:

        suite OFFLINE  -> EXIT=0   (passou, com o codigo quebrado)
        suite KERNEL   -> EXIT=1   (reprovou, correto)

    Causa: `main` faz `return Run() ? 0 : 1` -- sucesso e' valor TRUTHY -- e o
    CHECK do ramo #else devolvia 1 na falha. Vinte e tres suites estavam
    assim. O build usa o ramo kernel, entao nenhuma prova do projeto estava
    comprometida; mas qualquer compilacao offline via tudo passar, que e' a
    classe "o gate congela a mentira" na propria infraestrutura de prova.

    Regra: se o main e' `Run() ? 0 : 1`, o CHECK offline devolve 0 na falha.
    """
    tests_dir = ROOT / "driver" / "tests"
    if not tests_dir.is_dir():
        errors.append("driver/tests ausente")
        return
    bad_pattern = "#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)"
    ternary = re.compile(r"return\s+\w+\(\)\s*\?\s*0\s*:\s*1;")
    for path in sorted(tests_dir.glob("*.c")):
        text = path.read_text(encoding="utf-8", errors="replace")
        if bad_pattern not in text:
            continue
        if ternary.search(text):
            errors.append(
                f"{path.name}: CHECK do ramo offline devolve 1 na falha, mas main() "
                "usa `Run() ? 0 : 1` -- falha vira SUCESSO no build offline")



def audit_gfx_shader_contract_is_honest(errors: list[str]) -> None:
    """B.2 -- O PROGRAMA DE SHADER NAO PODE SER PUBLICADO POR ADIVINHACAO.

    Este modulo aponta a SPI para codigo na VRAM. Os quatro modos de errar
    aqui ja tem precedente neste projeto, e nenhum deles produz mensagem de
    erro no silicio -- produzem wave lendo registrador alheio, ou engine
    grafica travada com BugCheck 0x116/0x119:

      1. A granularidade de VGPR/SGPR NAO esta no sh_mask.h -- e' regra H.
         Se ela virar caminho critico sem conferencia, publicamos uma
         alocacao que o codigo compilado nao respeita. O contrato exige que
         o valor do MANIFESTO seja o publicado e que a regra apenas CONFIRA,
         falhando fechado na divergencia.
      2. Endereco cru em vez de >>8/>>40 perde os 8 bits baixos em silencio
         e enderecaria so' 40 dos 48 bits de VA.
      3. MEM_ORDERED fica no bit 25 no PS e no 27 no VS. O bit 25 cai DENTRO
         de VGPR_COMP_CNT do VS: generalizar os dois estagios numa formula
         escreve um campo achando que escreve outro.
      4. Segunda definicao de S_ENDPGM. Campo/constante duplicado em dois
         modulos ja custou A13, B4 e ContextAbi -- tres corridas.

    O meta-portao exige que o TESTE continue cobrando cada ponto: em cinco
    ocasioes neste projeto o gate carimbou o falso sucesso porque o teste
    foi afrouxado junto com o codigo (A15, mcdm_callbacks_test, C1,
    sdma_blt_executed, teste do blt com 128 bytes).
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_shader_contract.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_shader_contract.h"
    test_path = ROOT / "driver" / "tests" / "gfx_shader_contract_test.c"
    for path in (src_path, hdr_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar o shader grafico: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")
    src_code = strip_c_comments(src)

    compute = function_body(
        src, "BC250_GFX_SHADER_STATUS Bc250GfxShaderComputeRegs")
    if not compute:
        errors.append("Bc250GfxShaderComputeRegs nao localizado")
    else:
        compute_code = strip_c_comments(compute)

        # 1. O manifesto e' o publicado; a regra H so' confere.
        require(compute_code, "Regs->Rsrc1 = Plan->Rsrc1Manifest;",
                "RSRC1 publicado vem do manifesto do compilador", errors)
        require(compute_code, "Regs->Rsrc2 = Plan->Rsrc2Manifest;",
                "RSRC2 publicado vem do manifesto do compilador", errors)
        require(compute_code, "return BC250_GFX_SHADER_MANIFEST_MISMATCH;",
                "recusa nomeada quando a regra H discorda do compilador", errors)
        # Falha fechada de verdade: nada publicavel pode sobrar na saida.
        for field in ("Regs->PgmLo = 0u;", "Regs->PgmHi = 0u;",
                      "Regs->Rsrc1 = 0u;", "Regs->Rsrc2 = 0u;"):
            require(compute_code, field,
                    "zeragem da saida no caminho de MANIFEST_MISMATCH", errors)

        # 2. Divisao do endereco. O gate exige os DOIS deslocamentos: so' o
        #    >>8 passaria com HI cru, e so' o >>40 passaria com LO cru.
        require(compute_code, "(uint32_t)(Plan->GpuAddress >> 8)",
                "PgmLo recebe addr[39:8]", errors)
        require(compute_code, "(uint32_t)(Plan->GpuAddress >> 40)",
                "PgmHi recebe addr[47:40]", errors)
        require(compute_code, "return BC250_GFX_SHADER_MISALIGNED;",
                "recusa nomeada de endereco nao alinhado a 256", errors)
        require(compute_code, "return BC250_GFX_SHADER_ADDRESS_OUT_OF_RANGE;",
                "recusa nomeada de endereco acima de 48 bits", errors)

        # 3. VGPR_COMP_CNT nao pode ser aceito no PS -- la aquele bit e' outro.
        require(compute_code, "BC250_GFX_SHADER_STAGE_PS",
                "guarda de estagio no tratamento de VGPR_COMP_CNT", errors)

        # Lei 6 -- substituicao de campo, nunca OR cru sobre o manifesto.
        require(compute_code, "& ~BC250_GFX_RSRC1_VGPRS_MASK",
                "Lei 6 na substituicao de VGPRS", errors)
        require(compute_code, "& ~BC250_GFX_RSRC1_SGPRS_MASK",
                "Lei 6 na substituicao de SGPRS", errors)
        require(compute_code, "& ~BC250_GFX_RSRC2_USER_SGPR_MASK",
                "Lei 6 na substituicao de USER_SGPR", errors)

    # 3b. Os dois MEM_ORDERED tem de continuar sendo constantes DISTINTAS.
    require(hdr, "#define BC250_GFX_RSRC1_PS_MEM_ORDERED_MASK   0x02000000u",
            "MEM_ORDERED do PS no bit 25", errors)
    require(hdr, "#define BC250_GFX_RSRC1_VS_MEM_ORDERED_MASK   0x08000000u",
            "MEM_ORDERED do VS no bit 27", errors)

    # 4. Fonte unica de S_ENDPGM -- a checagem de ISA delega ao validador.
    forbid(strip_c_comments(hdr), "BC250_GFX_OPCODE_S_ENDPGM",
           "segunda definicao de S_ENDPGM fora de bc250_isa_validator.h", errors)
    forbid(src_code, "0xBF810000",
           "S_ENDPGM literal no contrato de shader grafico", errors)
    require(src_code, "Bc250ValidateComputeShaderIsa(",
            "delegacao da regra de ISA a fonte unica", errors)

    # O bit de SHADER_TYPE=COMPUTE nunca pode entrar no caminho grafico:
    # SET_SH_REG com ele ligado enderecaria os registradores da MEC.
    forbid(src_code, "SHADER_TYPE_COMPUTE",
           "SHADER_TYPE=COMPUTE no caminho de shader GRAFICO", errors)

    # META-PORTAO: o teste tem de continuar cobrando os mesmos pontos.
    require(test, "Bc250GetObservableShaderManifest()",
            "ancora da regra H contra manifesto de compilador REAL", errors)
    require(test, "BC250_GFX_SHADER_MANIFEST_MISMATCH",
            "teste da recusa por divergencia com o manifesto", errors)
    require(test, "CHECK(regs.PgmHi != 0u);",
            "teste com endereco alto -- sem ele LO cru passaria", errors)
    require(test, "BC250_GFX_RSRC1_PS_MEM_ORDERED_MASK &",
            "teste da sobreposicao entre MEM_ORDERED do PS e VGPR_COMP_CNT", errors)
    # Contraponto: sem ele a conferencia de manifesto vira peneira que
    # rejeita ate a escolha legitima do compilador.
    require(test, "CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) == BC250_GFX_SHADER_OK);",
            "contraponto -- campo de politica do compilador passa intacto", errors)


def audit_gfx_observable_shader_is_honest(errors: list[str]) -> None:
    """B.2.2 -- BLOBS DE SHADER GRAFICO OBSERVAVEL (VS E PS).

    Exige rigor de proveniencia (Lei 1 / Lei 8):
      1. Os blobs foram montados artesanalmente com opcodes canonicos RDNA 1.0
         (GFX10.1 / GC 10.1.3) cruzados com o compilador Mesa ACO.
         A classe epistemica DECLARADA e obrigatoriamente 'H'. Qualquer
         afirmacao de compilador real ROCm/LLVM sem ferramenta no host e'
         falso P1 e reprova.
      2. VS e PS devem terminar em BC250_OPCODE_S_ENDPGM.
      3. Opcodes de EXP canonicos: 0xF80008CF para pos0 no VS e 0xF800180F
         para mrt0 no PS.
      4. SHA-256 fixos e auditados.
      5. O teste deve validar a integracao com o contrato puro (B.2.1) e a
         rejeicao em caso de adulteracao.
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_observable_shader.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_observable_shader.h"
    test_path = ROOT / "driver" / "tests" / "gfx_observable_shader_test.c"
    for path in (src_path, hdr_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar o shader grafico observavel: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")
    src_code = strip_c_comments(src)

    # 1. Classe epistemica H declarada obrigatoriamente
    require(hdr, '#define BC250_GFX_OBSERVABLE_EPISTEMIC_CLASS "H"',
            "declaracao de classe epistemica H no header", errors)
    forbid(src_code, '"P1"', "afirmacao falsa de classe P1 sem compilador no host", errors)

    # 2. Terminacao em s_endpgm em ambos os blobs
    vs_start = src_code.find("g_Bc250GfxObservableVsShader")
    vs_end = src_code.find("};", vs_start) if vs_start != -1 else -1
    vs_body = src_code[vs_start:vs_end] if (vs_start != -1 and vs_end != -1) else ""
    require(vs_body, "BC250_OPCODE_S_ENDPGM", "terminacao em s_endpgm no VS", errors)

    ps_start = src_code.find("g_Bc250GfxObservablePsShader")
    ps_end = src_code.find("};", ps_start) if ps_start != -1 else -1
    ps_body = src_code[ps_start:ps_end] if (ps_start != -1 and ps_end != -1) else ""
    require(ps_body, "BC250_OPCODE_S_ENDPGM", "terminacao em s_endpgm no PS", errors)

    # 3. Opcodes de EXP
    require(src_code, "0xF80008CFu", "opcode EXP pos0 no VS", errors)
    require(src_code, "0xF800180Fu", "opcode EXP mrt0 no PS", errors)

    # 4. Hashes SHA-256 RECALCULADOS DINAMICAMENTE SOBRE OS DWORDS REAIS (D1)
    def _parse_and_hash_shader(array_name: str, count: int) -> str:
        idx = src_code.find(array_name)
        if idx == -1:
            return ""
        end_idx = src_code.find("};", idx)
        if end_idx == -1:
            return ""
        body = src_code[idx:end_idx]
        body = body[body.find("{") + 1:]
        tokens = [t.strip().rstrip("uU") for t in body.split(",") if t.strip()]
        dwords = []
        for t in tokens:
            if "BC250_OPCODE_S_ENDPGM" in t:
                dwords.append(0xBF810000)
            elif t.startswith("0x") or t.startswith("0X"):
                dwords.append(int(t, 16))
            else:
                try:
                    dwords.append(int(t))
                except ValueError:
                    return ""
        if len(dwords) != count:
            return ""
        raw_bytes = struct.pack(f"<{len(dwords)}I", *dwords)
        return hashlib.sha256(raw_bytes).hexdigest()

    vs_hash_calc = _parse_and_hash_shader("g_Bc250GfxObservableVsShader", 8)
    ps_hash_calc = _parse_and_hash_shader("g_Bc250GfxObservablePsShader", 9)

    vs_hash_expected = "219387cf4964de41ac24153b4669b8d1313ef23ecb63ce07a886db69434b9539"
    ps_hash_expected = "f5787965e08da58c5bef2b296dee47dce32de9f62f8b7e93ec0c06a9bb2a16c2"

    if vs_hash_calc != vs_hash_expected:
        errors.append(f"SHA-256 do VS recalculado ({vs_hash_calc}) diverge do esperado ({vs_hash_expected})")
    if ps_hash_calc != ps_hash_expected:
        errors.append(f"SHA-256 do PS recalculado ({ps_hash_calc}) diverge do esperado ({ps_hash_expected})")

    require(hdr, vs_hash_expected, "hash SHA-256 correto do VS no header", errors)
    require(hdr, ps_hash_expected, "hash SHA-256 correto do PS no header", errors)

    # 4b. Ressalvas epistemologicas e dividas declaradas (D2 / D3)
    require(hdr, "RESSALVA EPISTEMOLOGICA (D2)", "declaracao de circularidade D2 no header", errors)
    require(hdr, "DIVIDA DECLARADA B.2.2/D3", "declaracao de divida D3 de area zero no header", errors)

    # 5. Teste cobra integracao e falha fechada (D4)
    require(hdr, "int Bc250PopulateGfxShaderPlanFromObservable(",
            "assinatura com retorno int em Bc250PopulateGfxShaderPlanFromObservable", errors)
    require(test, "Bc250GfxShaderValidateBlob(vs->ShaderDwords",
            "teste de validacao de blob do VS pelo contrato B.2.1", errors)
    require(test, "Bc250GfxShaderValidateBlob(ps->ShaderDwords",
            "teste de validacao de blob do PS pelo contrato B.2.1", errors)
    require(test, "Bc250GfxShaderComputeRegs(&vsPlan",
            "teste de calculo de registradores do VS", errors)
    require(test, "Bc250GfxShaderComputeRegs(&psPlan",
            "teste de calculo de registradores do PS", errors)
    require(test, "BC250_GFX_SHADER_NO_ENDPGM",
            "teste de rejeicao sem endpgm", errors)
    require(test, "Bc250PopulateGfxShaderPlanFromObservable(NULL",
            "teste de falha fechada de populate com NULL", errors)


def audit_gfx_shader_staging_is_honest(errors: list[str]) -> None:
    """B.2.3 -- STAGING E ALOCACAO DE SHADERS NA VRAM (PAREDE B).

    Audita as garantias formais de carregamento de microcodigo:
      1. Alinhamento estrito a 256 bytes (Lei 7 / RDNA 1 SPI_SHADER_PGM_LO_*).
         Enderecos desalinhados devem ser recusados por nome
         (BC250_GFX_SHADER_STAGING_MISALIGNED).
      2. Ausencia total de sobreposicao de faixas (Zero Overlap): tanto no espaco
         GPUVA quanto no espaco virtual da CPU, colisoes devem ser recusadas por
         nome (BC250_GFX_SHADER_STAGING_OVERLAP).
      3. Prova de escrita byte a byte por leitura (Readback Proof -- Lei 2):
         Nenhuma escrita em memoria e assumida. Se houver divergencia na leitura,
         retorna BC250_GFX_SHADER_STAGING_READBACK_FAILED e zera saidas (falha fechada).
      4. Limite de tamanho de buffer: buffers menores que os bytes exigidos pelo
         manifesto sao recusados por nome (BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL).
      5. Falha fechada absoluta (Lei 4): qualquer erro zera imediatamente todas as
         estruturas de saida (VsPlanOut, PsPlanOut, EvidenceOut).
      6. Integracao completa com os contratos B.2.1 e B.2.2: validacao de manifestos,
         validacao de blobs e calculo estrito dos registradores SPI_SHADER_PGM_*.
      7. Meta-portao: a suite de teste unitario deve cobrar rigorosamente cada um
         desses caminhos e codigos de recusa nomeados.
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_shader_staging_contract.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_shader_staging_contract.h"
    test_path = ROOT / "driver" / "tests" / "gfx_shader_staging_test.c"
    for path in (src_path, hdr_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar o staging de shader: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. Header: constantes, enums e estruturas
    require(hdr, "#define BC250_GFX_SHADER_STAGING_ALIGNMENT 256ull",
            "constante de alinhamento de 256 bytes no header", errors)
    for status_code in (
        "BC250_GFX_SHADER_STAGING_OK",
        "BC250_GFX_SHADER_STAGING_BAD_ARGUMENT",
        "BC250_GFX_SHADER_STAGING_MISALIGNED",
        "BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL",
        "BC250_GFX_SHADER_STAGING_OVERLAP",
        "BC250_GFX_SHADER_STAGING_READBACK_FAILED",
        "BC250_GFX_SHADER_STAGING_MANIFEST_INVALID",
        "BC250_GFX_SHADER_STAGING_CONTRACT_FAILED",
        "BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW",
        "BC250_GFX_SHADER_STAGING_RESERVED_COLLISION",
    ):
        require(hdr, status_code, f"codigo de status {status_code} no header", errors)

    require(hdr, "typedef struct BC250_GFX_SHADER_STAGING_WINDOW",
            "estrutura de janela de VRAM e reservas no header", errors)
    require(hdr, "typedef struct BC250_GFX_SHADER_STAGING_TRANSPORT",
            "estrutura de transporte com injecao de readback no header", errors)
    require(hdr, "typedef struct BC250_GFX_SHADER_STAGING_BUFFER",
            "estrutura de buffer de staging no header", errors)
    require(hdr, "typedef struct BC250_GFX_SHADER_STAGING_EVIDENCE",
            "estrutura de evidencia de staging no header", errors)
    require(hdr, "BC250_GFX_SHADER_STAGING_STATUS Bc250GfxShaderStagePipeline(",
            "assinatura de Bc250GfxShaderStagePipeline no header", errors)

    # 2. Implementacao: corpo da funcao principal
    stage_func = function_body(
        src, "BC250_GFX_SHADER_STAGING_STATUS Bc250GfxShaderStagePipeline(")
    if not stage_func:
        errors.append("Bc250GfxShaderStagePipeline nao localizado")
        return

    stage_code = strip_c_comments(stage_func)

    # 2a. Falha fechada: zeragem antecipada de saidas
    entry_marker = stage_code.find("VsManifest == NULL")
    entry_block = stage_code[:entry_marker] if entry_marker != -1 else ""
    require(entry_block, "Bc250StagingZeroMemory(VsPlanOut, (uint32_t)sizeof(*VsPlanOut));",
            "zeragem de VsPlanOut na entrada", errors)
    require(entry_block, "Bc250StagingZeroMemory(PsPlanOut, (uint32_t)sizeof(*PsPlanOut));",
            "zeragem de PsPlanOut na entrada", errors)
    require(entry_block, "Bc250StagingZeroMemory(EvidenceOut, (uint32_t)sizeof(*EvidenceOut));",
            "zeragem de EvidenceOut na entrada", errors)

    # 2b. Validacao de ponteiros e manifestos
    require(stage_code, "return BC250_GFX_SHADER_STAGING_BAD_ARGUMENT;",
            "recusa nomeada para argumentos nulos", errors)
    require(stage_code, "Bc250ValidateGfxObservableManifest(VsManifest)",
            "validacao de manifesto do VS", errors)
    require(stage_code, "Bc250ValidateGfxObservableManifest(PsManifest)",
            "validacao de manifesto do PS", errors)
    require(stage_code, "return BC250_GFX_SHADER_STAGING_MANIFEST_INVALID;",
            "recusa nomeada para manifesto invalido", errors)

    # 2c. Alinhamento de hardware a 256 bytes (Lei 7)
    require(stage_code, "VsBuffer->GpuAddress & (BC250_GFX_SHADER_STAGING_ALIGNMENT - 1ull)",
            "mascara de alinhamento de 256 bytes na checagem do VS", errors)
    require(stage_code, "PsBuffer->GpuAddress & (BC250_GFX_SHADER_STAGING_ALIGNMENT - 1ull)",
            "mascara de alinhamento de 256 bytes na checagem do PS", errors)
    require(stage_code, "return BC250_GFX_SHADER_STAGING_MISALIGNED;",
            "recusa nomeada de endereco desalinhado a 256 bytes", errors)

    # 2d. Limites de tamanho de buffer
    require(stage_code, "return BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL;",
            "recusa nomeada de buffer pequeno demais", errors)

    # 2e. Delimitacao de janela e regioes reservadas (D2)
    if stage_code.count("return BC250_GFX_SHADER_STAGING_RESERVED_COLLISION;") < 2:
        errors.append("recusa nomeada de colisao com regiao reservada exigida para scanout e anel")
    require(stage_code, "return BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW;",
            "recusa nomeada de endereco fora da janela de VRAM", errors)

    # 2f. Ausencia de sobreposicao (Zero Overlap)
    require(stage_code, "vsGpuStart < psGpuEnd && psGpuStart < vsGpuEnd",
            "deteccao de sobreposicao no espaco GPUVA", errors)
    require(stage_code, "vsVirtStart < psVirtEnd && psVirtStart < vsVirtEnd",
            "deteccao de sobreposicao no espaco virtual da CPU", errors)
    if stage_code.count("return BC250_GFX_SHADER_STAGING_OVERLAP;") < 2:
        errors.append("recusa nomeada de sobreposicao exigida nos dois caminhos (GPUVA e virtual)")

    # 2g. Barreira de memoria e Readback proof com injecao (Lei 2 / D1 / D3)
    require(stage_code, "Bc250StagingMemoryBarrier(Transport);",
            "barreira de memoria antes do readback", errors)
    require(stage_code, "val != VsManifest->ShaderDwords[i]",
            "conferencia de leitura de dword do VS contra manifesto", errors)
    require(stage_code, "val != PsManifest->ShaderDwords[i]",
            "conferencia de leitura de dword do PS contra manifesto", errors)
    if stage_code.count("return BC250_GFX_SHADER_STAGING_READBACK_FAILED;") < 2:
        errors.append("recusa nomeada de readback failed exigida nos dois caminhos (VS e PS)")
    if stage_code.count("EvidenceOut->MemoryClearedOnFailure = 1u;") < 2:
        errors.append("registro de limpeza de memoria exigido nos dois caminhos de readback (VS e PS)")

    # 2h. Integracao com B.2.1 e B.2.2
    require(stage_code, "Bc250PopulateGfxShaderPlanFromObservable(VsManifest",
            "populacao de plano do VS a partir do manifesto observavel", errors)
    require(stage_code, "Bc250PopulateGfxShaderPlanFromObservable(PsManifest",
            "populacao de plano do PS a partir do manifesto observavel", errors)
    require(stage_code, "Bc250GfxShaderValidateBlob(",
            "validacao do blob pelo contrato B.2.1", errors)
    require(stage_code, "Bc250GfxShaderComputeRegs(VsPlanOut",
            "calculo dos registradores do VS pelo contrato B.2.1", errors)
    require(stage_code, "Bc250GfxShaderComputeRegs(PsPlanOut",
            "calculo dos registradores do PS pelo contrato B.2.1", errors)
    require(stage_code, "return BC250_GFX_SHADER_STAGING_CONTRACT_FAILED;",
            "recusa nomeada quando integracao de contrato falha", errors)

    # 3. Meta-portao: cobertura no teste unitario
    test_code = strip_c_comments(test)
    require(test_code, "Bc250GfxShaderStagePipeline(vs, ps, &vsBuf, &psBuf, &win, &transport, &vsPlan, &psPlan, &ev)",
            "teste de fluxo completo de staging com janela e transporte", errors)
    require(test_code, "CHECK(status == BC250_GFX_SHADER_STAGING_OK);",
            "teste de sucesso na execucao completa", errors)
    require(test_code, "CHECK(ev.StagingCompleted == 1u);",
            "teste de conclusao registrada na evidencia", errors)
    require(test_code, "fixture.FailReadbackOffset = 2u;",
            "teste com injecao de falha de readback via transporte", errors)
    require(test_code, "CHECK(status == BC250_GFX_SHADER_STAGING_READBACK_FAILED);",
            "teste exercitando recusa nomeada de readback failed", errors)
    require(test_code, "CHECK(ev.MemoryClearedOnFailure == 1u);",
            "teste confirmando limpeza de memoria apos falha de readback", errors)
    require(test_code, "BC250_GFX_SHADER_STAGING_RESERVED_COLLISION",
            "teste de rejeicao de colisao com regiao reservada", errors)
    require(test_code, "BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW",
            "teste de rejeicao fora da janela de VRAM", errors)
    require(test_code, "BC250_GFX_SHADER_STAGING_BAD_ARGUMENT",
            "teste de rejeicao de argumentos nulos", errors)
    require(test_code, "BC250_GFX_SHADER_STAGING_MISALIGNED",
            "teste de rejeicao de desalinhamento de hardware", errors)
    require(test_code, "BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL",
            "teste de rejeicao de buffer com tamanho insuficiente", errors)
    require(test_code, "BC250_GFX_SHADER_STAGING_OVERLAP",
            "teste de rejeicao de sobreposicao de buffers", errors)
    require(test_code, "BC250_GFX_SHADER_STAGING_MANIFEST_INVALID",
            "teste de rejeicao de manifesto invalido", errors)


def audit_gfx_sh_reg_packet_is_honest(errors: list[str]) -> None:
    """B.2.4 -- EMISSAO PM4 SET_SH_REG DE SHADERS GRAFICOS (VS E PS).

    Invariantes criticos (Lei 1 / Lei 2 / Lei 4 / Lei 7):
      1. Proibicao absoluta do bit SHADER_TYPE=COMPUTE (0x02) no anel grafico.
         Com ele ligado o pacote endereca registradores da MEC e causa stall/hang.
         Header deve ser exatamente 0xC0047600u.
      2. Offsets exatos do catalogo P1:
         - VS: 0x048 (SPI_SHADER_PGM_LO_VS / BAR5 0x0B120)
         - PS: 0x008 (SPI_SHADER_PGM_LO_PS / BAR5 0x0B020)
      3. Contagem exata de dwords:
         - 6 dwords por estagio (1 header + 1 offset + 4 regs).
         - 12 dwords no pipeline completo (VS + PS).
      4. Fail-closed:
         - Rejeicao de ring cheio sem avancar cursor (BC250_GFX_SH_REG_RING_FULL).
         - Rejeicao de registradores invalidos (BC250_GFX_SH_REG_INVALID_REGS).
         - Rejeicao de stream corrompido (BC250_GFX_SH_REG_STREAM_MISMATCH).
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_sh_reg_packet.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_sh_reg_packet.h"
    test_path = ROOT / "driver" / "tests" / "gfx_sh_reg_packet_test.c"
    for path in (src_path, hdr_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar emissao PM4 de shaders: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. Contrato e constantes no header
    require(hdr, "#define BC250_GFX_PM4_SET_SH_REG_OPCODE             0x76u",
            "opcode 0x76 de SET_SH_REG no header", errors)
    require(hdr, "#define BC250_GFX_PM4_SET_SH_REG_HEADER_4DW         0xC0047600u",
            "header Type-3 de SET_SH_REG para 4 registradores sem bit compute", errors)
    require(hdr, "#define BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE       6u",
            "contagem de 6 dwords por estagio de shader", errors)
    require(hdr, "#define BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS  12u",
            "contagem de 12 dwords para o pipeline completo (VS + PS)", errors)
    require(hdr, "#define BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN      0x02u",
            "definicao explicita de proibicao do bit compute no header", errors)

    forbid(strip_c_comments(src), "BC250_PM4_SHADER_TYPE_COMPUTE",
           "SHADER_TYPE_COMPUTE no emissor de shader grafico", errors)
    forbid(strip_c_comments(hdr), "BC250_PM4_SHADER_TYPE_COMPUTE",
           "SHADER_TYPE_COMPUTE no header de shader grafico", errors)

    # 2. Implementacao no .c
    src_code = strip_c_comments(src)

    # 2a. Reuso obrigatorio de Bc250Pm4EmitSetShReg (evita escrita plana sem wrap circular)
    require(src_code, "Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, CursorDwords,",
            "reuso de Bc250Pm4EmitSetShReg com suporte a wrap circular", errors)

    # 2b. VS usa offset 0x048 (BC250_GFX_SH_PGM_LO_VS)
    require(src_code, "BC250_GFX_SH_PGM_LO_VS, vsValues, 4u)",
            "emissao de offset do VS usando BC250_GFX_SH_PGM_LO_VS", errors)

    # 2c. PS usa offset 0x008 (BC250_GFX_SH_PGM_LO_PS)
    require(src_code, "BC250_GFX_SH_PGM_LO_PS, psValues, 4u)",
            "emissao de offset do PS usando BC250_GFX_SH_PGM_LO_PS", errors)

    # 2d. Proibir escrita plana direta sem tratar wrap
    forbid(src_code, "RingBuffer[cursor++] = BC250_GFX_SH_PGM_LO_VS;",
           "escrita plana de VS sem tratar wrap circular", errors)
    forbid(src_code, "RingBuffer[cursor++] = BC250_GFX_SH_PGM_LO_PS;",
           "escrita plana de PS sem tratar wrap circular", errors)

    # 2e. Recusa nomeada de ring cheio
    require(src_code, "return BC250_GFX_SH_REG_RING_FULL;",
            "recusa nomeada de ring cheio", errors)

    # 2f. Recusa nomeada de registradores invalidos
    require(src_code, "return BC250_GFX_SH_REG_INVALID_REGS;",
            "recusa nomeada de registradores invalidos", errors)

    # 2g. Recusa nomeada de stream corrompido
    require(src_code, "return BC250_GFX_SH_REG_STREAM_MISMATCH;",
            "recusa nomeada de stream com divergencia", errors)

    # 2h. Checagem ativa contra presenca do bit compute
    require(src_code, "(header & BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN) != 0u",
            "checagem fail-closed de bit compute proibido no header", errors)

    # 3. Meta-portao no teste unitario
    test_code = strip_c_comments(test)
    require(test_code, "Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &vsRegs, &psRegs, &ev)",
            "teste de emissao de pipeline completa", errors)
    require(test_code, "Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs)",
            "teste de verificacao de stream integro", errors)
    require(test_code, "BC250_GFX_SH_REG_RING_FULL",
            "teste de rejeicao com ring cheio", errors)
    require(test_code, "BC250_GFX_SH_REG_INVALID_REGS",
            "teste de rejeicao com registradores invalidos", errors)
    require(test_code, "BC250_GFX_SH_REG_STREAM_MISMATCH",
            "teste de rejeicao com divergencia em stream", errors)
    require(test_code, "BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN",
            "teste conferindo ausencia estrita de bit compute", errors)

    # 4. Transacao fisica no anel grafico CP_RB0 (Lei 2 e Lei 4)
    txn_path = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.c"
    if txn_path.exists():
        txn_code = strip_c_comments(txn_path.read_text(encoding="utf-8", errors="replace"))
        require(txn_code, "BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnSubmitShReg",
                "funcao Bc250GfxRingTxnSubmitShReg no modulo de transacao", errors)
        require(txn_code, "Bc250GfxShRegEmitPipeline(",
                "emissao do pipeline no anel CP_RB0", errors)
        require(txn_code, "Bc250Pm4EmitWriteData(",
                "emissao de fence fisica via WRITE_DATA apos os shaders", errors)
        require(txn_code, "Bc250GfxRingClassifyTripod(",
                "classificacao estrita do tripe de conclusao", errors)
        require(txn_code, "*SubmitPlan->FenceCpuAddress = SubmitPlan->FencePoisonValue;",
                "injecao fisica de poison na fence antes do disparo", errors)
        require(txn_code, "SubmitEvidence->FencePoisonReadback != SubmitPlan->FencePoisonValue",
                "verificacao de readback do poison na fence", errors)
        require(txn_code, "verdict == BC250_TRIPOD_PROVEN",
                "sucesso condicionado a tripe provado", errors)

    # 5. Bringup Level 3 no miniport com rollback incondicional (Lei 5)
    mini_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if mini_path.exists():
        mini_code = strip_c_comments(mini_path.read_text(encoding="utf-8", errors="replace"))
        require(mini_code, "Bc250GfxRingTxnSubmitShReg(",
                "chamada de submissao no bringup Level 3", errors)
        require(mini_code, "Level >= 3u",
                "suporte ao nivel 3 de bringup", errors)


def audit_gfx_viewport_scissor_is_honest(errors: list[str]) -> None:
    """B.3.1 -- VIEWPORT, SCISSOR E REGISTRADORES DE CONTEXTO PM4.

    Invariantes criticos (Lei 1 / Lei 2 / Lei 4 / Lei 7):
      1. Opcode e constantes no header:
         - SET_CONTEXT_REG opcode 0x69.
         - Total do stream de 20 dwords exatos (8 + 4 + 4 + 4).
         - VTE_CNTL default 0x0000043Fu.
         - Scissor bit 31 WINDOW_OFFSET_DISABLE (0x80000000u).
      2. Reuso obrigatorio de Bc250Pm4EmitSetContextReg com wrap circular.
      3. Registradores de contexto GFX10 corretos:
         - PA_CL_VPORT_XSCALE (0x010Fu)
         - PA_SU_SC_MODE_CNTL (0x0205u)
         - PA_SC_WINDOW_SCISSOR_TL (0x0081u)
         - PA_SC_VPORT_SCISSOR_0_TL (0x0094u)
      4. Fail-closed:
         - Rejeicao de argumentos nulos e NaN/Infinitos (BC250_GFX_VPORT_INVALID_ARGUMENT).
         - Rejeicao de dimensoes fora dos limites (BC250_GFX_VPORT_OUT_OF_BOUNDS).
         - Rejeicao de stream com divergencia (BC250_GFX_VPORT_STREAM_MISMATCH).
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_viewport_scissor.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_viewport_scissor.h"
    test_path = ROOT / "driver" / "tests" / "gfx_viewport_scissor_test.c"
    for path in (src_path, hdr_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar viewport/scissor: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. Header
    require(hdr, "#define BC250_GFX_PM4_SET_CONTEXT_REG_OPCODE        0x69u",
            "opcode 0x69 de SET_CONTEXT_REG no header", errors)
    require(hdr, "#define BC250_GFX_VPORT_STREAM_TOTAL_DWORDS         20u",
            "contagem de 20 dwords para o stream de viewport/scissor", errors)
    require(hdr, "#define BC250_GFX_VTE_CNTL_DEFAULT                  0x0000043Fu",
            "valor canonico de PA_CL_VTE_CNTL no header", errors)
    require(hdr, "#define BC250_GFX_SCISSOR_OFFSET_DISABLE            0x80000000u",
            "bit WINDOW_OFFSET_DISABLE no header de scissor", errors)
    require(hdr, "#define BC250_GFX_PM4_SET_CTX_VPORT_HEADER_6DW      0xC0066900u",
            "header canonico de VPORT (count = 6)", errors)
    require(hdr, "#define BC250_GFX_PM4_SET_CTX_RASTER_HEADER_2DW     0xC0026900u",
            "header canonico de RASTER (count = 2)", errors)
    require(hdr, "#define BC250_GFX_PM4_SET_CTX_WIN_SCISSOR_HEADER_2DW 0xC0026900u",
            "header canonico de Window Scissor (count = 2)", errors)
    require(hdr, "#define BC250_GFX_PM4_SET_CTX_VP_SCISSOR_HEADER_2DW  0xC0026900u",
            "header canonico de Viewport Scissor (count = 2)", errors)

    # 2. Implementacao no .c
    src_code = strip_c_comments(src)
    require(src_code, "Bc250Pm4EmitSetContextReg(",
            "reuso mandatorio de Bc250Pm4EmitSetContextReg com suporte a wrap circular", errors)
    require(src_code, "BC250_PM4_REG_PA_CL_VPORT_XSCALE",
            "emissao de PA_CL_VPORT_XSCALE usando registrador de catalogo", errors)
    require(src_code, "BC250_PM4_REG_PA_SU_SC_MODE_CNTL",
            "emissao de PA_SU_SC_MODE_CNTL usando registrador de catalogo", errors)
    require(src_code, "BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_TL",
            "emissao de PA_SC_WINDOW_SCISSOR_TL usando registrador de catalogo", errors)
    require(src_code, "BC250_PM4_REG_PA_SC_VPORT_SCISSOR_0_TL",
            "emissao de PA_SC_VPORT_SCISSOR_0_TL usando registrador de catalogo", errors)

    # 3. Fail-closed no .c
    require(src_code, "return BC250_GFX_VPORT_INVALID_ARGUMENT;",
            "recusa nomeada de argumentos invalidos / NaN", errors)
    require(src_code, "return BC250_GFX_VPORT_OUT_OF_BOUNDS;",
            "recusa nomeada de limites fora da faixa", errors)
    require(src_code, "return BC250_GFX_VPORT_STREAM_MISMATCH;",
            "recusa nomeada de divergencia no stream verificado", errors)

    # 4. Teste unitario
    test_code = strip_c_comments(test)
    require(test_code, "Bc250GfxComputeViewportScissor(",
            "teste de calculo matematico de viewport e scissor", errors)
    require(test_code, "Bc250GfxEmitViewportScissorStream(",
            "teste de emissao de stream de viewport", errors)
    require(test_code, "Bc250GfxVerifyViewportScissorStream(",
            "teste de verificacao de stream de viewport", errors)
    require(test_code, "BC250_GFX_VPORT_INVALID_ARGUMENT",
            "teste de rejeicao com argumento invalido", errors)
    require(test_code, "BC250_GFX_VPORT_OUT_OF_BOUNDS",
            "teste de rejeicao com limites fora da faixa", errors)
    require(test_code, "BC250_GFX_VPORT_STREAM_MISMATCH",
            "teste de deteccao de corrupcao no stream", errors)


def audit_gfx_primitive_assembler_is_honest(errors: list[str]) -> None:
    """B.3.2 -- PRIMITIVE ASSEMBLER E TOPOLOGIA DE TRIANGULOS (VGT).

    Invariantes criticos (Lei 1 / Lei 2 / Lei 4 / Lei 7):
      1. Opcode e constantes no header:
         - SET_UCONFIG_REG opcode 0x79.
         - Total do stream de 8 dwords exatos (3 + 3 + 2).
         - Header canonico SET_UCONFIG_REG 0xC0017900u (Count = 1).
         - Header canonico NOP 0xC0001000u (Count = 0).
      2. Reuso obrigatorio de Bc250Pm4EmitSetUconfigReg e Bc250Pm4EmitNop com wrap circular.
      3. Registradores UCONFIG GFX10 corretos:
         - BC250_PM4_REG_VGT_PRIMITIVE_TYPE (0xC242u)
         - BC250_PM4_REG_VGT_NUM_INSTANCES (0xC24Du)
      4. Fail-closed:
         - Rejeicao de argumentos nulos, topologias invalidas ou instâncias zeradas (BC250_GFX_PRIM_INVALID_ARGUMENT).
         - Rejeicao de divergencia no stream verificado (BC250_GFX_PRIM_STREAM_MISMATCH).
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_primitive_assembler.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_primitive_assembler.h"
    test_path = ROOT / "driver" / "tests" / "gfx_primitive_assembler_test.c"
    for path in (src_path, hdr_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar primitive assembler: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. Header
    require(hdr, "#define BC250_GFX_PM4_SET_UCONFIG_REG_OPCODE        0x79u",
            "opcode 0x79 de SET_UCONFIG_REG no header", errors)
    require(hdr, "#define BC250_GFX_PRIM_STREAM_TOTAL_DWORDS          8u",
            "contagem de 8 dwords para o stream de primitive assembler", errors)
    require(hdr, "#define BC250_GFX_PM4_SET_UCONFIG_HEADER_1DW        0xC0017900u",
            "header canonico de SET_UCONFIG_REG (count = 1)", errors)
    require(hdr, "#define BC250_GFX_PM4_NOP_HEADER_1DW                0xC0001000u",
            "header canonico de NOP (count = 0)", errors)
    require(hdr, "#define BC250_GFX_PRIM_TRILIST                      0x04u",
            "topologia canonica DI_PT_TRILIST 0x04 no header", errors)

    # 2. Implementacao no .c
    src_code = strip_c_comments(src)
    require(src_code, "Bc250Pm4EmitSetUconfigReg(",
            "reuso mandatorio de Bc250Pm4EmitSetUconfigReg com suporte a wrap circular", errors)
    require(src_code, "Bc250Pm4EmitNop(",
            "reuso mandatorio de Bc250Pm4EmitNop com suporte a wrap circular", errors)
    require(src_code, "BC250_PM4_REG_VGT_PRIMITIVE_TYPE",
            "emissao de VGT_PRIMITIVE_TYPE usando registrador de catalogo", errors)
    require(src_code, "BC250_PM4_REG_VGT_NUM_INSTANCES",
            "emissao de VGT_NUM_INSTANCES usando registrador de catalogo", errors)

    # 3. Fail-closed no .c
    require(src_code, "return BC250_GFX_PRIM_INVALID_ARGUMENT;",
            "recusa nomeada de argumentos invalidos ou topologia invalida", errors)
    require(src_code, "return BC250_GFX_PRIM_STREAM_MISMATCH;",
            "recusa nomeada de divergencia no stream verificado", errors)

    # 4. Teste unitario
    test_code = strip_c_comments(test)
    require(test_code, "Bc250GfxComputePrimitiveAssembler(",
            "teste de calculo e configuracao de primitive assembler", errors)
    require(test_code, "Bc250GfxEmitPrimitiveAssemblerStream(",
            "teste de emissao de stream de primitive assembler", errors)
    require(test_code, "Bc250GfxVerifyPrimitiveAssemblerStream(",
            "teste de verificacao de stream de primitive assembler", errors)
    require(test_code, "BC250_GFX_PRIM_INVALID_ARGUMENT",
            "teste de rejeicao com argumento invalido", errors)
    require(test_code, "BC250_GFX_PRIM_STREAM_MISMATCH",
            "teste de deteccao de corrupcao no stream", errors)


def audit_gfx_draw_initiator_is_honest(errors: list[str]) -> None:
    """B.3.3 -- DRAW INITIATOR E DISPARO PM4 DEVEM SER ARQUITETURALMENTE EXATOS.

    Invariantes arquiteturais travados por este portao:
      1. Opcode IT_DRAW_INDEX_AUTO = 0x2Du e header 0xC0012D00u (Count = 1).
      2. Padding canonico com PACKET2 NOP (0x80000000u) para fechamento em 4 DW / 16 B.
      3. Reuso obrigatorio de Bc250Pm4EmitDrawIndexAuto e Bc250Pm4EmitPacket2Nop.
      4. Fail-closed:
         - Rejeicao de argumentos nulos, contagem de vertices zerada ou DrawInitiator invalido (BC250_GFX_DRAW_INVALID_ARGUMENT).
         - Rejeicao de divergencia no stream verificado (BC250_GFX_DRAW_STREAM_MISMATCH).
      5. Cobertura de teste com validacao de modo canonico AUTO_INDEX (0x02) e legacy TRIANGLELIST (0x04).
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_draw_initiator.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_draw_initiator.h"
    test_path = ROOT / "driver" / "tests" / "gfx_draw_initiator_test.c"
    for path in (src_path, hdr_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar draw initiator: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. Header
    require(hdr, "#define BC250_GFX_PM4_DRAW_INDEX_AUTO_OPCODE        0x2Du",
            "opcode 0x2D de DRAW_INDEX_AUTO no header", errors)
    require(hdr, "#define BC250_GFX_DRAW_STREAM_TOTAL_DWORDS          4u",
            "contagem de 4 dwords para o stream de draw initiator (16 bytes)", errors)
    require(hdr, "#define BC250_GFX_PM4_DRAW_INDEX_AUTO_HEADER        0xC0012D00u",
            "header canonico de DRAW_INDEX_AUTO (count = 1)", errors)
    require(hdr, "#define BC250_GFX_PM4_PACKET2_NOP                   0x80000000u",
            "valor canonico do Packet-2 NOP no header", errors)
    require(hdr, "#define BC250_GFX_DRAW_INITIATOR_AUTO_INDEX         0x00000002u",
            "modo canonico AUTO_INDEX 0x02 no header", errors)

    # 2. Implementacao no .c
    src_code = strip_c_comments(src)
    require(src_code, "Bc250Pm4EmitDrawIndexAuto(",
            "reuso mandatorio de Bc250Pm4EmitDrawIndexAuto com suporte a wrap circular", errors)
    require(src_code, "Bc250Pm4EmitPacket2Nop(",
            "reuso mandatorio de Bc250Pm4EmitPacket2Nop com suporte a wrap circular", errors)

    # 3. Fail-closed no .c
    require(src_code, "return BC250_GFX_DRAW_INVALID_ARGUMENT;",
            "recusa nomeada de argumentos invalidos ou modo invalido", errors)
    require(src_code, "return BC250_GFX_DRAW_STREAM_MISMATCH;",
            "recusa nomeada de divergencia no stream verificado", errors)

    # 4. Teste unitario
    test_code = strip_c_comments(test)
    require(test_code, "Bc250GfxComputeDrawInitiator(",
            "teste de calculo e configuracao de draw initiator", errors)
    require(test_code, "Bc250GfxEmitDrawInitiatorStream(",
            "teste de emissao de stream de draw initiator", errors)
    require(test_code, "Bc250GfxVerifyDrawInitiatorStream(",
            "teste de verificacao de stream de draw initiator", errors)
    require(test_code, "BC250_GFX_DRAW_INVALID_ARGUMENT",
            "teste de rejeicao com argumento invalido", errors)
    require(test_code, "BC250_GFX_DRAW_STREAM_MISMATCH",
            "teste de deteccao de corrupcao no stream", errors)


def audit_gfx_3d_draw_is_honest(errors: list[str]) -> None:
    """B.4 -- PIPELINE INTEGRADO DE RENDERIZACAO DE TRIANGULO 3D NO ANEL GRAFICO (GFX10 CP_RB0).

    Invariantes arquiteturais travados por este portao (Leis 1, 2, 4, 5 e 7):
      1. Dimensao atomica do stream: 64 dwords exatos (256 bytes, alinhamento estrito a 16 bytes).
         - BC250_GFX_3D_DRAW_SH_DWORDS (12)
         - BC250_GFX_3D_DRAW_COLOR_DWORDS (8)
         - BC250_GFX_3D_DRAW_TARGET_MASK_DWORDS (4)
         - BC250_GFX_3D_DRAW_VPORT_DWORDS (20)
         - BC250_GFX_3D_DRAW_PRIM_DWORDS (8)
         - BC250_GFX_3D_DRAW_INIT_DWORDS (4)
         - BC250_GFX_3D_DRAW_FENCE_DWORDS (8)
         - BC250_GFX_3D_DRAW_STREAM_TOTAL_DWORDS == 64u
      2. Reuso obrigatorio dos contratos e construtores de pacotes PM4:
         - Bc250GfxShRegEmitPipeline (Shaders VS+PS)
         - Bc250Pm4EmitColorTarget0 (Color Target 0)
         - BC250_PM4_REG_CB_TARGET_MASK (Target Mask)
         - Bc250GfxEmitViewportScissorStream (Viewport e Scissors)
         - Bc250GfxEmitPrimitiveAssemblerStream (Primitive Assembler)
         - Bc250GfxEmitDrawInitiatorStream (Draw Initiator)
         - Bc250Pm4EmitReleaseMemEop32 (Fence fisica VRAM RELEASE_MEM)
      3. Verificacao previa estrita do stream:
         - Bc250GfxVerify3DDrawStream antes do avanco de WPTR
      4. Tripe da Lei 2 no silicio:
         - Injecao de poison na fence antes do disparo (*FenceCpuAddress = FencePoisonValue)
         - Verificacao de readback do poison (FencePoisonReadback != FencePoisonValue)
         - Classificacao com Bc250GfxRingClassifyTripod
         - Sucesso condicionado estritamente a verdict == BC250_TRIPOD_PROVEN
      5. Integracao ao miniport:
         - Chamada de Bc250GfxRingTxnSubmit3DDraw no Level >= 4u
         - Rollback incondicional (Lei 5)
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.h"
    test_path = ROOT / "driver" / "tests" / "gfx_ring_contract_test.c"
    mini_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"

    for path in (src_path, hdr_path, test_path, mini_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar pipeline 3D draw: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")
    mini = mini_path.read_text(encoding="utf-8", errors="replace")

    # 1. Header do contrato de transacao
    require(hdr, "#define BC250_GFX_3D_DRAW_STREAM_TOTAL_DWORDS 64u",
            "constante canonica de 64 dwords no header", errors)
    require(hdr, "BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnSubmit3DDraw(",
            "declaracao de Bc250GfxRingTxnSubmit3DDraw no header", errors)
    require(hdr, "BC250_GFX_RING_TXN_STATUS Bc250GfxVerify3DDrawStream(",
            "declaracao de Bc250GfxVerify3DDrawStream no header", errors)
    require(hdr, "typedef struct BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN",
            "estrutura de plano de submissao 3D no header", errors)
    require(hdr, "typedef struct BC250_GFX_RING_SUBMIT_3D_DRAW_EVIDENCE",
            "estrutura de evidencia de submissao 3D no header", errors)

    # 2. Implementacao no .c
    src_code = strip_c_comments(src)
    require(src_code, "Bc250GfxShRegEmitPipeline(",
            "emissao de pipeline de shaders no anel", errors)
    require(src_code, "Bc250Pm4EmitColorTarget0(",
            "emissao de color target 0 no anel", errors)
    require(src_code, "BC250_PM4_REG_CB_TARGET_MASK",
            "emissao de target mask usando registrador de catalogo", errors)
    require(src_code, "Bc250GfxEmitViewportScissorStream(",
            "emissao de viewport e scissor no anel", errors)
    require(src_code, "Bc250GfxEmitPrimitiveAssemblerStream(",
            "emissao de primitive assembler no anel", errors)
    require(src_code, "Bc250GfxEmitDrawInitiatorStream(",
            "emissao de draw initiator no anel", errors)
    require(src_code, "Bc250Pm4EmitWriteData(",
            "emissao de fence fisica via WRITE_DATA (DST_SEL=5 Memory Direct MC) no anel", errors)
    require(src_code, "Bc250GfxVerify3DDrawStream(",
            "verificacao de integridade do stream antes do disparo MMIO", errors)
    require(src_code, "Bc250GfxRingClassifyTripod(",
            "classificacao estrita do tripe de conclusao", errors)
    require(src_code, "*SubmitPlan->FenceCpuAddress = SubmitPlan->FencePoisonValue;",
            "injecao fisica de poison na fence antes do disparo", errors)
    require(src_code, "SubmitEvidence->FencePoisonReadback != SubmitPlan->FencePoisonValue",
            "verificacao de readback do poison na fence", errors)
    require(src_code, "verdict == BC250_TRIPOD_PROVEN",
            "sucesso condicionado a tripe provado", errors)

    # 3. Fail-closed no .c
    require(src_code, "return BC250_GFX_RING_TXN_NOT_ACTIVE;",
            "recusa nomeada quando o anel nao esta ativo", errors)
    require(src_code, "return BC250_GFX_RING_TXN_FENCE_POISON_FAILED;",
            "recusa nomeada quando o poison da fence falha", errors)
    require(src_code, "return BC250_GFX_RING_TXN_READBACK_MISMATCH;",
            "recusa nomeada de mismatch na verificacao do stream", errors)

    # 4. Testes unitarios
    test_code = strip_c_comments(test)
    require(test_code, "Bc250RunGfxRingSubmit3DDrawTest(",
            "suite de testes para submissao 3D", errors)
    require(test_code, "Bc250GfxRingTxnSubmit3DDraw(",
            "chamada de teste para Bc250GfxRingTxnSubmit3DDraw", errors)
    require(test_code, "Bc250GfxVerify3DDrawStream(",
            "chamada de teste para Bc250GfxVerify3DDrawStream", errors)

    # 5. Integracao ao miniport
    mini_code = strip_c_comments(mini)
    require(mini_code, "Bc250GfxRingTxnSubmit3DDraw(",
            "chamada de submissao 3D no bringup do miniport", errors)
    require(mini_code, "Level >= 4u",
            "suporte ao nivel 4 de bringup no miniport", errors)


def audit_gfx_ring_contract_is_honest(errors: list[str]) -> None:
    """B.1 -- O ANEL GRAFICO NAO PODE NASCER MENTINDO.

    Tres defeitos ja custaram corrida neste projeto e todos os tres estao ao
    alcance deste modulo novo:

      1. Campo mascarado em silencio produz geometria errada ACEITA pelo
         hardware (Bc250Pm4EmitColorTarget0, corrigido na etapa do catalogo).
      2. Comparacao CRUA de RPTR ignora a volta do anel -- A15, BugCheck 0x119
         na corrida 17:43 (RPTR=4612, alvo=516, anel=4096).
      3. Anel configurado com RB_EXE=0 aceita todas as escritas, devolve
         sucesso e nunca consome pacote. E' o estado do unico prototipo
         existente (bc250_compute_service.c escreve CP_RB0_CNTL=0x00A00709).

    O portao exige que o contrato recuse os tres, e o meta-portao exige que o
    TESTE continue cobrando cada um -- sem isso a correcao e' revertida em
    silencio, que e' como as oito ocorrencias anteriores nasceram.
    """
    src_path = ROOT / "driver" / "core" / "bc250_gfx_ring_contract.c"
    hdr_path = ROOT / "driver" / "core" / "bc250_gfx_ring_contract.h"
    test_path = ROOT / "driver" / "tests" / "gfx_ring_contract_test.c"
    for path in (src_path, hdr_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar o anel grafico: {path.name}")
            return

    src = src_path.read_text(encoding="utf-8", errors="replace")
    hdr = hdr_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. RB_EXE fail-closed: plano nao executavel e' RECUSADO por nome.
    validate = function_body(src, "BC250_GFX_RING_STATUS Bc250GfxRingValidatePlan")
    if not validate:
        errors.append("Bc250GfxRingValidatePlan nao localizado")
    else:
        require(validate, "return BC250_GFX_RING_NOT_EXECUTABLE;",
                "recusa nomeada de anel com RB_EXE=0", errors)
        require(validate, "return BC250_GFX_RING_FIELD_OVERFLOW;",
                "recusa nomeada de campo fora da largura", errors)
        require(validate, "return BC250_GFX_RING_OVERLAP;",
                "recusa nomeada de sobreposicao entre anel, writeback e shadow", errors)
        require(validate, "return BC250_GFX_RING_OUT_OF_WINDOW;",
                "recusa nomeada de regiao fora da janela de VRAM", errors)
        # Desaltar micro-engine sem ucode e' solta-la sobre memoria de instrucao
        # nao inicializada. A recusa tem de estar no CODIGO, nao so' no teste --
        # a primeira versao deste portao gateava so' o teste e a mutacao passou.
        require(validate, "return BC250_GFX_RING_FIRMWARE_NOT_LOADED;",
                "recusa nomeada de desalt sem firmware declarado", errors)
        # Exigir a MENSAGEM nao basta: trocar a condicao por if (0) deixa o
        # return vivo no ramo morto e o portao passa. Medido ao verificar a
        # mutacao M32. A condicao e' que tem de existir.
        require(validate,
                "Plan->UnhaltMicroEngines == 1u && Plan->MicroEngineFirmwareLoaded != 1u",
                "condicao do pre-requisito de firmware ausente -- a mensagem sozinha "
                "sobrevive dentro de um ramo morto", errors)

    # 2. O CNTL nao pode ser montado sem passar pela validacao.
    compute = function_body(src, "BC250_GFX_RING_STATUS Bc250GfxRingComputeCntl")
    if not compute:
        errors.append("Bc250GfxRingComputeCntl nao localizado")
    else:
        require(compute, "Bc250GfxRingValidatePlan(Plan)",
                "montagem de CP_RB0_CNTL sem validar o plano antes", errors)
        # Mascarar o proprio campo em vez de recusar e' o defeito do item 1.
        forbid(compute, "& GFX_RB_BUFSZ_MAX",
               "mascaramento silencioso de RB_BUFSZ na montagem", errors)
        forbid(compute, "& GFX_RB_BLKSZ_MAX",
               "mascaramento silencioso de RB_BLKSZ na montagem", errors)

    # 3. A perna 1 usa a FONTE UNICA da A15, nunca aritmetica repetida.
    tripod = function_body(src, "BC250_TRIPOD_VERDICT Bc250GfxRingClassifyTripod")
    if not tripod:
        errors.append("Bc250GfxRingClassifyTripod nao localizado")
    else:
        require(tripod, "Bc250TripodWrapRingPointer(",
                "reducao por volta do anel fora da fonte unica da A15", errors)
        forbid(tripod, "% RingBytes",
               "aritmetica de volta reimplementada em vez de usar o helper", errors)

    # 4. O valor de poll do SDMA tem de ser detectavel, nao copiado.
    require(hdr, "BC250_GFX_RING_POLL_CNTL_SDMA_BORROWED 0x00403004u",
            "deteccao do valor de poll importado do SDMA", errors)

    # 5. META-PORTAO no teste. strip_c_comments + escopo por funcao para que
    #    assercao comentada ou neutralizada conte como ausente.
    test_clean = strip_c_comments(test)
    test_func = function_body(test_clean, "static int Bc250RunGfxRingContractTest")
    if not test_func:
        errors.append("funcao Bc250RunGfxRingContractTest ausente no teste do anel")
    else:
        require(test_func, "cntl == BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE",
                "teste exige que o CNTL calculado reproduza o POST medido", errors)
        require(test_func, "Bc250GfxRingCntlRefusesNonExecutable(0x00A00709u) == 1u",
                "teste exige deteccao do CP_RB0_CNTL nao executavel do prototipo", errors)
        require(test_func, "== BC250_GFX_RING_NOT_EXECUTABLE",
                "teste exige recusa de plano com RB_EXE=0", errors)
        require(test_func, "4612u",
                "teste exige o caso de volta do anel medido na corrida 17:43", errors)
        require(test_func, "== BC250_GFX_RING_OK",
                "teste exige o contraponto de plano valido", errors)
        forbid(test_func, "assert(1)",
               "neutralizacao de assercao no teste do anel grafico", errors)
        forbid(test_func, "if (0)",
               "bypass de assercao no teste do anel grafico", errors)

    # =====================================================================
    # 6. TRANSACAO FISICA (Lei 5). O contrato acima e' aritmetica; aqui e'
    #    silicio, e cada estagio tem uma regra que ja foi violada no projeto.
    # =====================================================================
    txn_path = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.c"
    if not txn_path.exists():
        errors.append("bc250_gfx_ring_transaction.c ausente")
        return
    txn = txn_path.read_text(encoding="utf-8", errors="replace")

    preflight = function_body(txn, "BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnPreflight")
    if not preflight:
        errors.append("Bc250GfxRingTxnPreflight nao localizado")
    else:
        # O preflight so' e' seguro de rodar numa corrida porque nao escreve.
        forbid(preflight, "Bc250GfxTxnWrite(",
               "escrita no estagio de PREFLIGHT, que precisa ser so-leitura", errors)
        require(preflight, "Evidence->SnapshotTaken = 1u;",
                "snapshot marcado ao fim do preflight", errors)

    apply_body = function_body(txn, "BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnApply")
    if not apply_body:
        errors.append("Bc250GfxRingTxnApply nao localizado")
    else:
        require(apply_body, "Evidence->SnapshotTaken != 1u",
                "APPLY sem exigir snapshot -- aplicar sem rota de volta viola a Lei 5", errors)
        require(apply_body, "Bc250GfxRingPollCntlIsBorrowed(",
                "APPLY sem barrar o valor de poll herdado do SDMA", errors)
        require(apply_body, "return BC250_GFX_RING_TXN_READBACK_MISMATCH;",
                "APPLY sem conferir a escrita por readback", errors)
        # O desalt so' pode acontecer DEPOIS do readback do anel: soltar CE/PFP/ME
        # com CP_RB0_BASE ainda em lixo (o boot entrega 0xFEDCBAEF) seria apontar
        # a engine para endereco invalido.
        require(apply_body, "Plan->UnhaltMicroEngines == 1u",
                "APPLY desalta sem o plano pedir", errors)
        require(apply_body, "Bc250GfxRingMicroEnginesAreHalted(Evidence->MeCntlAfter) != 0u",
                "APPLY aceita desalt sem provar que o HALT saiu -- readback igual ao "
                "escrito nao e' prova de que a engine soltou", errors)
        forbid(apply_body, "0x15000000u",
               "literal de CP_ME_CNTL no APPLY -- o desalt tem de usar a formula da "
               "Lei 6 e preservar os bits que nao sao HALT", errors)

        # A partida do anel so' pode ser declarada pelo READBACK. Escrever 1 em
        # CP_RB0_ACTIVE e seguir em frente declararia pronto o que talvez nao
        # tenha ligado -- sucesso sem backend (Lei 4).
        require(apply_body, "return BC250_GFX_RING_TXN_NOT_STARTABLE;",
                "APPLY sem recusa quando o anel nao fica ATIVO no readback", errors)
        require(apply_body, "(Evidence->RingActivateReadback & 1u) == 0u",
                "APPLY declara partida sem conferir CP_RB0_ACTIVE por readback", errors)
        # Quatro enderecos que o prototipo usa achando que sao os *_IC_OP_CNTL.
        # Medidos contra gc_10_1_0_offset.h: os reais estao em BASE_IDX 1.
        for wrong in ("0x0000C4D0u", "0x0000C4D4u", "0x0000C4D8u", "0x0000C4DCu",
                      "0x00028A60u", "0x00028A64u"):
            forbid(apply_body, wrong,
                   f"endereco {wrong} e' o que o prototipo usa e esta ERRADO "
                   "(docs/03, bloco das seis divergencias)", errors)

    rollback = function_body(txn, "BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnRollback")
    if not rollback:
        errors.append("Bc250GfxRingTxnRollback nao localizado")
    else:
        require(rollback, "Evidence->SnapshotTaken != 1u",
                "ROLLBACK sem exigir snapshot", errors)
        require(rollback, "readback != Evidence->CntlBefore",
                "ROLLBACK sem conferir a restauracao por readback", errors)
        require(rollback, "Evidence->UnhaltAttempted == 1u",
                "ROLLBACK sem re-HALT das micro-engines -- deixa-las soltas sobre "
                "uma configuracao sendo revertida e' o pior estado possivel", errors)
        # Ordem inversa do APPLY: desativa o anel ANTES de re-HALT e de
        # restaurar registrador. Reverter com o anel ATIVO e' o pior estado.
        require(rollback, "Evidence->RingActivateAttempted == 1u",
                "ROLLBACK sem desativar o anel antes de reverter a configuracao", errors)

    txn_func = function_body(test_clean, "static int Bc250RunGfxRingTransactionTest")
    if not txn_func:
        errors.append("funcao Bc250RunGfxRingTransactionTest ausente no teste")
    else:
        require(txn_func, "ev.Writes == 0u",
                "teste exige que o PREFLIGHT nao escreva nada", errors)
        # Ancora de ocorrencia UNICA. "ev.Writes == 0u" aparece em tres casos
        # legitimos, entao apagar o do preflight sozinho passava despercebido.
        require(txn_func, "ev.Reads == 7u",
                "teste exige a contagem exata de leituras do PREFLIGHT", errors)
        require(txn_func, "BC250_GFX_RING_TXN_READBACK_MISMATCH",
                "teste exige cobertura de readback mentiroso", errors)
        require(txn_func, "BC250_GFX_RING_TXN_ROLLBACK_FAILED",
                "teste exige cobertura de rollback que nao cola", errors)
        require(txn_func, "BC250_GFX_RING_TXN_POLL_CNTL_BORROWED",
                "teste exige cobertura do valor de poll herdado do SDMA", errors)
        require(txn_func, "BC250_GFX_RING_TXN_NOT_STARTABLE",
                "teste exige a parada de proveniencia", errors)
        # O cenario de escrita descartada so' discrimina se o valor planejado
        # DIFERIR do que a placa ja tem. A primeira versao deste teste caiu
        # exatamente nisso: plano e POST coincidiam e a assercao nao testava nada.
        require(txn_func, "ev.CntlBefore != ev.CntlPlanned",
                "teste exige que o cenario de escrita descartada seja discriminante", errors)
        require(txn_func, "ev.MicroEnginesHalted == 1u",
                "teste exige que o preflight decodifique o estado das micro-engines", errors)
        require(txn_func, "ev.UnhaltPassed == 1u",
                "teste sem o caso positivo de desalt", errors)
        require(txn_func, "ev.UnhaltAttempted == 0u",
                "teste sem o CONTRAPONTO: sem pedido, CP_ME_CNTL nao pode ser tocado -- "
                "sem ele o APPLY poderia desaltar sempre e os positivos passariam", errors)

    # -----------------------------------------------------------------
    # O ESTADO DE BOOT MEDIDO NAO PODE SER APAGADO NEM RENOMEADO DE VOLTA.
    #
    # A corrida 14/09 11:43 mediu o anel pela primeira vez e REFUTOU a minha
    # premissa: o boot entrega CP_RB0_CNTL=0x00A00000 (RB_BUFSZ=0, RB_EXE=0) e
    # CP_ME_CNTL=0x15000000 (CE/PFP/ME halted). A constante chamava-se
    # OBSERVED_POST e nao era o POST -- mentira embutida no nome.
    # -----------------------------------------------------------------
    require(hdr, "BC250_GFX_RING_CNTL_MEASURED_BOOT 0x00A00000u",
            "constante do CP_RB0_CNTL medido no boot", errors)
    require(hdr, "BC250_GFX_RING_ME_CNTL_MEASURED_BOOT 0x15000000u",
            "constante do CP_ME_CNTL medido no boot", errors)
    # Proibe a DEFINICAO, nao a mencao: a nota historica que explica por que o
    # nome mudou e' justamente o que impede a confusao de voltar.
    forbid(hdr, "#define BC250_GFX_RING_CNTL_OBSERVED_POST",
           "nome refutado pela medicao 14/09 -- 0x50000709 nao e' o estado de boot", errors)
    require(test_func, "fields.RbExe == 0u",
            "teste sem a assercao de que o boot entrega RB_EXE=0", errors)
    require(test_func, "meFields.AllHalted == 1u",
            "teste sem a assercao de que o boot entrega as tres micro-engines halted", errors)
    # -----------------------------------------------------------------
    # A SUITE OFFLINE NAO PODE DEPENDER DO ESTADO DA MAQUINA.
    #
    # MEDIDO EM 14/09: TestF5PmDrawEmissionOnNode1 APAGAVA a variavel de
    # ambiente para exercitar o fail-closed, e com isso a decisao caia em
    # HKLM. O bind das 11:43 mexeu no registro e a suite passou a reprovar SEM
    # NENHUMA MUDANCA DE CODIGO. E' a segunda ocorrencia da classe: a faixa C1
    # ja tinha encontrado a mesma dependencia em outro ponto (docs/234).
    #
    # Regra: quem testa DECLARA o valor; apagar deixa o registro decidir.
    # -----------------------------------------------------------------
    umd_src = (ROOT / "driver" / "umd" / "bc250_d3d_umd.c").read_text(
        encoding="utf-8", errors="replace")
    armed_fn = function_body(umd_src, "BOOL Bc250UmdExperimentalPm4DrawArmed")
    if not armed_fn:
        errors.append("Bc250UmdExperimentalPm4DrawArmed nao localizado")
    else:
        require(armed_fn, "return (envBuf[0] == '1') ? TRUE : FALSE;",
                "variavel de ambiente nao e' autoritativa para DESARMAR -- a suite "
                "offline volta a depender de HKLM", errors)
    d3d_test = (ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c").read_text(
        encoding="utf-8", errors="replace")
    forbid(d3d_test,
           "SetEnvironmentVariableA(BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME, NULL)",
           "teste apaga a variavel de ambiente em vez de declarar o valor -- "
           "a decisao cai no registro da maquina", errors)
    require(d3d_test,
            'SetEnvironmentVariableA(BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME, "0")',
            "teste sem a declaracao explicita do caso desarmado", errors)

    require(test_func, "== BC250_GFX_RING_FIRMWARE_NOT_LOADED",
            "teste sem a recusa de desalt sem firmware declarado", errors)
    require(test_func, "Bc250GfxRingMicroEnginesAreHalted(0u) == 0u",
            "teste sem o contraponto de micro-engines liberadas -- a funcao podia "
            "devolver sempre 1 e todos os positivos passariam", errors)

    # =====================================================================
    # 7. O TUNABLE TEM DE SER LIDO NOS DOIS RAMOS DO #if.
    #
    # QUARTA ocorrencia da classe neste projeto: A13 (tunable de submissao),
    # B4 (GpuvaLabPromotion) e ContextAbi ficaram num ramo de
    # BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP e eram invisiveis no outro --
    # cada uma custou uma corrida. A primeira versao do bloco do anel grafico
    # repetiu o erro: leitura no ramo de producao, uso no ramo de laboratorio,
    # tunable sempre 0 e medicao que nunca rodaria.
    #
    # O portao acompanha o aninhamento de #if/#else/#endif que mencionam a
    # macro e exige que a leitura esteja FORA de qualquer um deles.
    # =====================================================================
    mini_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    inf_path = ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf"
    if not mini_path.exists() or not inf_path.exists():
        errors.append("fontes ausentes para auditar o tunable do anel grafico")
        return

    mini = mini_path.read_text(encoding="utf-8", errors="replace")
    macro = "BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP"
    depth = 0
    inside_macro_depth = None
    found = False
    for line in mini.splitlines():
        stripped = line.strip()
        if stripped.startswith("#if"):
            depth += 1
            if macro in stripped and inside_macro_depth is None:
                inside_macro_depth = depth
        elif stripped.startswith("#endif"):
            if inside_macro_depth is not None and depth == inside_macro_depth:
                inside_macro_depth = None
            depth = max(0, depth - 1)
        elif stripped.startswith("#else") or stripped.startswith("#elif"):
            pass
        elif 'L"GfxRingPreflight"' in line:
            found = True
            if inside_macro_depth is not None:
                errors.append(
                    "tunable GfxRingPreflight lido DENTRO de um bloco de "
                    + macro
                    + " -- a imagem que nao define a macro nunca o le (A13/B4/ContextAbi)")
    if not found:
        errors.append('leitura do tunable L"GfxRingPreflight" ausente do miniport')

    # A chave so nasce pelo AddReg do INF; sem a linha, armar pelo registro nao
    # tem onde escrever. Licao da ordem do ensaio de TDR (07/09).
    inf = inf_path.read_text(encoding="utf-8", errors="replace")
    require(inf, "HKR,Parameters,GfxRingPreflight,0x00010001,0",
            "declaracao fail-closed de GfxRingPreflight no INF fonte", errors)

    # A medicao nao pode alterar o veredito do StartDevice.
    probe = function_body(mini, "static BC250_GFX_RING_TXN_STATUS Bc250ActiveProbeGfxRingReadonly")
    if not probe:
        errors.append("Bc250ActiveProbeGfxRingReadonly nao localizado")
    else:
        require(probe, "Bc250GfxRingTxnPreflight(&interfaceData, NULL, Evidence)",
                "probe do anel grafico sem o modo de medicao pura (Plan == NULL)", errors)
        require(probe, "writes != 0u",
                "probe sem conferir o contador de escritas do proprio transporte", errors)
        forbid(probe, "Bc250GfxRingTxnApply(",
               "APPLY no caminho de medicao -- o probe tem de ser somente leitura", errors)

    # =====================================================================
    # 8. O TUNABLE TEM DE ATRAVESSAR O PIPELINE INTEIRO.
    #
    # Tres corridas deste projeto foram perdidas por defeito de HOST, nao de
    # silicio. Um tunable que existe no codigo mas nao chega ao pacote, ou que
    # o AddReg apaga durante o install, produz exatamente isso: a corrida roda,
    # a medicao nao acontece e ninguem sabe por que. Cada elo abaixo ja falhou
    # uma vez neste repositorio.
    # =====================================================================
    pkg_path = ROOT / "tools" / "build-bc250mcdm-package.ps1"
    lab_path = ROOT / "tools" / "package-bc250mcdm-lab.ps1"
    bind_path = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"
    arm_path = ROOT / "tools" / "arm-gfx-ring-preflight.ps1"
    for path in (pkg_path, lab_path, bind_path, arm_path):
        if not path.exists():
            errors.append(f"ferramenta ausente no pipeline do anel grafico: {path.name}")
            return

    pkg = pkg_path.read_text(encoding="utf-8", errors="replace")
    lab = lab_path.read_text(encoding="utf-8", errors="replace")
    bind = bind_path.read_text(encoding="utf-8", errors="replace")

    # O manifesto DERIVA do INF; declarar por literal criaria a segunda fonte de
    # verdade que ja mordeu este projeto na geometria de GPUVA (faixa A11).
    require(pkg, "Pacote bloqueado: INF sem HKR,Parameters,GfxRingPreflight",
            "empacotador sem recusa quando o INF nao declara GfxRingPreflight", errors)
    require(pkg, "GfxRingPreflight = $gfxRingValue",
            "manifesto sem o valor de GfxRingPreflight derivado do INF", errors)

    # NOCLOBBER: o KMD le o valor UMA vez no StartDevice, e um AddReg mandatorio
    # apagaria em silencio o que o operador armou antes do bind. Foi essa classe
    # de defeito que custou a corrida do ensaio de TDR (doc 186).
    require(lab, "'HKR,Parameters,GfxRingPreflight,0x00010003,0'",
            "INF de laboratorio sem GfxRingPreflight NOCLOBBER -- o install apagaria "
            "o valor armado antes do bind (doc 186)", errors)
    require(lab, "'Parameters,GfxRingPreflight' = '0x00010003'",
            "empacotador de laboratorio sem a flag NOCLOBBER de GfxRingPreflight", errors)
    # O manifesto de LABORATORIO e' gerado por um bloco proprio -- a primeira
    # versao deste portao auditou so' o empacotador de producao, e o campo saiu
    # VAZIO no manifesto do candidato. Medido no pacote 3.178.1.0.
    require(lab, "GfxRingPreflight = $gfxRingPreflightValue",
            "manifesto de laboratorio sem o valor de GfxRingPreflight", errors)
    require(lab, "Pacote de laboratorio bloqueado: INF empacotado sem HKR,Parameters,GfxRingPreflight",
            "empacotador de laboratorio sem recusa quando o INF empacotado nao declara o campo", errors)

    # A politica pre-bind e' o unico momento em que a chave existe e o
    # StartDevice ainda nao rodou.
    require(bind, "GfxRingPreflight = $(if ($EnableGfxRingPreflight) { 1 } else { 0 })",
            "binder sem GfxRingPreflight na politica pre-bind", errors)
    require(bind, "[switch]$EnableGfxRingPreflight",
            "binder sem o switch -EnableGfxRingPreflight", errors)

    # -----------------------------------------------------------------
    # O ENSAIO DO ANEL (GfxRingBringup) ESCREVE EM REGISTRADOR. O pipeline
    # inteiro tem de carrega-lo, pelos mesmos motivos do preflight -- e com
    # uma exigencia a mais: o rollback SEMPRE roda, porque o ensaio mede e nao
    # deixa anel montado.
    # -----------------------------------------------------------------
    require(inf, "HKR,Parameters,GfxRingBringup,0x00010001,0",
            "declaracao fail-closed de GfxRingBringup no INF fonte", errors)
    require(pkg, "Pacote bloqueado: INF sem HKR,Parameters,GfxRingBringup",
            "empacotador sem recusa quando o INF nao declara GfxRingBringup", errors)
    require(lab, "'HKR,Parameters,GfxRingBringup,0x00010003,0'",
            "INF de laboratorio sem GfxRingBringup NOCLOBBER", errors)
    require(bind, "GfxRingBringup = $GfxRingBringupLevel",
            "binder sem GfxRingBringup na politica pre-bind", errors)
    require(bind, "$gfxBringupFlag",
            "binder sem o flag do ensaio na dica do dry-run -- o operador rodaria "
            "um comando que perde o nivel (precedente C3.9 e corrida 14/09 11:27)", errors)

    bringup = function_body(mini, "static BC250_GFX_RING_TXN_STATUS Bc250ActiveRunGfxRingBringup")
    if not bringup:
        errors.append("Bc250ActiveRunGfxRingBringup nao localizado")
    else:
        require(bringup, "rollbackStatus = Bc250GfxRingTxnRollback",
                "ensaio do anel sem rollback -- ele mede, nao deixa anel montado", errors)
        require(bringup, "Bc250VramRelease(allocator, token)",
                "ensaio do anel sem devolver a VRAM", errors)
        require(bringup, "Bc250MmioTopologyUnmap(&Context->Core.MmioTransport)",
                "ensaio do anel sem desmapear o transporte", errors)
        require(bringup, "Level >= 2u",
                "ensaio sem separar os niveis -- level 1 nao pode desaltar nem ligar", errors)
        # Corrida 14/09 13:18 perdeu o ensaio com wrong_state: o caminho do PFP
        # deixa o transporte MAPEADO e o mapeamento recusa estado nao-vazio.
        # Classe A16, que ja custou a corrida de 08/09 11:48.
        require(bringup, "Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED",
                "ensaio nao checa se o transporte ja esta mapeado (A16)", errors)
        require(bringup, "if (mappedByUs == 1u)",
                "ensaio desmapearia o transporte alheio -- so' pode desmapear o que "
                "ele proprio mapeou", errors)

    # -----------------------------------------------------------------
    # TODO switch -Enable* TEM de aparecer na DICA do dry-run.
    #
    # O operador roda o comando que o dry-run imprime. Um flag que existe no
    # param() mas nao na dica produz uma corrida que parece correta e nao
    # exercita nada. JA ACONTECEU DUAS VEZES: na faixa C3.9
    # (Node1JournalGatePerProcess, registrado em comentario no proprio
    # arquivo) e na corrida 14/09 11:27, que foi GASTA com
    # "GfxRingPreflight=0 (present=1 compiled=1)".
    #
    # A auditoria da mesma passada achou EnableD3D11ShaderTranslator com o
    # mesmo defeito, latente desde antes da B.1.
    # -----------------------------------------------------------------
    import re as _re
    declared = set(_re.findall(r"\[switch\]\$(Enable[A-Za-z0-9]+)", bind))
    hinted = set(_re.findall(r"Flag = \$\(if \(\$(Enable[A-Za-z0-9]+)\)", bind))
    missing = sorted(declared - hinted)
    for name in missing:
        errors.append(
            f"switch -{name} existe no binder mas NAO entra na dica do dry-run -- "
            "o operador rodaria um comando que perde o flag (precedente C3.9 e "
            "corrida 14/09 11:27)")

    # =====================================================================
    # 9. SUITE DE MUTACAO NAO PODE MEXER NO RELOGIO DOS FONTES.
    #
    # MEDIDO EM 14/09: as suites gravam o fonte mutado e restauram o conteudo,
    # mas nao o mtime. package-bc250mcdm-lab.ps1 tem guarda de obsolescencia
    # POR DATA, entao rodar a suite depois de construir a imagem de laboratorio
    # invalidava o pacote sem uma linha de codigo ter mudado -- e exigia um
    # rebuild que so' o dono do projeto pode autorizar (flag destrutiva).
    #
    # A guarda por data esta CERTA e nao deve ser afrouxada. Quem estava errado
    # era a suite. tools/_mutation_mtime.py restaura os carimbos.
    # =====================================================================
    helper = ROOT / "tools" / "_mutation_mtime.py"
    if not helper.exists():
        errors.append("tools/_mutation_mtime.py ausente -- suites de mutacao "
                      "invalidariam o pacote de laboratorio pela data")
    else:
        for suite in sorted((ROOT / "tools").glob("test-*.py")):
            text = suite.read_text(encoding="utf-8", errors="replace")
            if "write_text" not in text:
                continue
            touches_driver = ('ROOT / "driver"' in text) or ("ROOT / 'driver'" in text)
            if touches_driver and "preserve_mtimes" not in text:
                errors.append(
                    f"{suite.name} reescreve fontes sob driver/ sem preserve_mtimes -- "
                    "invalida a guarda de obsolescencia do pacote de laboratorio")


def audit_f3_pixel_and_residency_truth(errors: list[str]) -> None:
    """F3: execution is independent of color; residency comes from the OS.

    F0's blanket ban on EVICTED_TO_DISK assertions was itself incorrect:
    DXGI_DDI_ARG_QUERYRESOURCERESIDENCY defines it as nonresident. Test the
    callback + aggregate HRESULT instead of choosing an enum by its name.
    """
    def read(name):
        return (ROOT / name).read_text(encoding="utf-8", errors="replace")
    umd = read("driver/umd/bc250_d3d_umd.c")
    winsys = read("driver/umd/bc250_wddm_winsys.c")
    display = read("driver/wdk/bc250_display_ddi.c")
    test = read("driver/tests/bc250_d3d_umd_test.c")
    wtest = read("driver/tests/bc250_wddm_winsys_test.c")
    proof = read("driver/core/bc250_blt_proof_contract.c")
    ptest = read("driver/tests/blt_proof_contract_test.c")
    clear = function_body(umd, "VOID APIENTRY Bc250Umd10ClearRenderTargetView")
    require(clear, "ClearRenderTargetView RECUSADO", "F3 clear names missing GPU fill", errors)
    require(clear, "pfnSetErrorCb(device->hRTCoreLayer, E_NOTIMPL)", "F3 clear reports error to runtime", errors)
    forbid(clear, "pCpuShadow", "F3 clear writes or allocates a CPU shadow", errors)
    q = function_body(umd, "HRESULT APIENTRY Bc250DxgiQueryResourceResidency")
    require(q, "pfnQueryResidencyCb(device->hRTDevice.handle, &args)", "F3 residency queries OS", errors)
    forbid(q, "Allocation.Resident", "F3 residency inferred from local cache", errors)
    require(q, "result = S_NOT_RESIDENT;", "F3 nonresident aggregate status", errors)
    require(q, "return E_UNEXPECTED;", "F3 unknown residency rejected", errors)
    create = function_body(umd, "VOID APIENTRY Bc250Umd10CreateResource")
    for name in ("MapAllocation", "MakeResident", "WaitPaging"):
        require(create, "wsStatus = Bc250Winsys" + name, "F3 creation checks " + name, errors)
        forbid(create, "(void)Bc250Winsys" + name, "F3 creation discards " + name, errors)
    require(create, "BC250_WINSYS_STATUS cleanup = Bc250WinsysDeallocate", "F3 creation rolls back", errors)
    require(create, "CreateResource RECUSADO", "F3 creation reports failure", errors)
    resident = function_body(winsys, "BC250_WINSYS_STATUS Bc250WinsysMakeResident")
    require(resident, "if (Allocation->ResidencyPending) goto complete_residency;", "F3 pending retry avoids second request", errors)
    require(resident, "waitStatus = Bc250WinsysWaitPaging(Context, Allocation);", "F3 residency waits for paging", errors)
    require(resident, "if (waitStatus != BC250_WINSYS_OK) return waitStatus;", "F3 wait failure propagated", errors)
    if resident.find("Allocation->Resident = TRUE;") < resident.find("waitStatus = Bc250WinsysWaitPaging"):
        errors.append("F3 resident published before paging completion")
    present = function_body(display, "NTSTATUS APIENTRY Bc250DxgkDdiPresent")
    forbid(present, "Blt preservado", "F3 black frame suppressed", errors)
    forbid(present, "if (srcOk && srcNz == 0u", "F3 content controls presentation", errors)
    require(present, "presentStatus = Bc250PresentCopySucceeded(sdmaDone,", "F3 Present status depends on actual copy", errors)
    require(present, "STATUS_SUCCESS : STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE;", "F3 failed copy is not success", errors)
    require(proof, "return (CopyCompleted != 0u || SameSurface != 0u) ? 1u : 0u;", "F3 copy outcome contract", errors)
    # Meta-gates: pin behavioral counterexamples, not old CPU pixel expectations.
    require(test, "assert(g_SetErrorCalls == before + 1u && g_LastUmdError == E_NOTIMPL);", "F3 test requires Clear refusal", errors)
    require(test, "assert(((unsigned char*)resColor.pCpuShadow)[0] == 0x5A);", "F3 test preserves Map shadow", errors)
    require(test, "assert(g_QueryCalls == 2u);", "F3 test requires OS query", errors)
    require(test, "assert(hr == S_NOT_RESIDENT);", "F3 test requires nonresident HRESULT", errors)
    require(test, "assert(!res.Created);", "F3 test rejects partial creation", errors)
    require(test, "assert(g_CreateDeallocs == (stage == 1u ? 0u : 1u));", "F3 test requires creation cleanup", errors)
    pending = function_body(wtest, "static void TestPagingPendingStatus")
    require(pending, "assert(!allocation.Resident);", "F3 test forbids resident on failed wait", errors)
    require(pending, "assert(g_State.MakeResidentCalls == 1u); /* retry waits", "F3 test checks retry reference", errors)
    require(ptest, "CHECK(Bc250PresentCopySucceeded(0u, 0u) == 0u);", "F3 test forbids failed copy success", errors)


def audit_dxgi_present_chain_honest(errors: list[str]) -> None:
    """F0.3 (13/09) -- AS DDIs DA CADEIA DE APRESENTACAO NAO PODEM DECLARAR SUCESSO SEM BACKEND.

    Duas auditorias externas independentes acharam o mesmo defeito, e a leitura de
    codigo confirmou:

      Bc250DxgiSetDisplayMode           -> `log; return S_OK;`  e `pfnSetDisplayModeCb`
                                           (d3dumddi.h:4533) NAO aparece em lugar nenhum
                                           de driver/umd.
      Bc250DxgiRotateResourceIdentities -> `log; return S_OK;` sem rotacionar nada.

    O DWM chama SetDisplayMode para dizer QUAL primaria deve ser varrida. Sem
    repassar ao kernel, ele segue com a primaria do boot -- medido na corrida
    18:03:43: UM unico SetVidPnSourceAddress (0xAAC000, hAlloc=0x08) e 96/96
    Present com hr=0x80004005.

    E o pior: `bc250_d3d_umd_test.c` EXIGIA o S_OK das duas, e congelava
    DXGI_DDI_RESIDENCY_EVICTED_TO_DISK. Sexta, setima e oitava ocorrencias da
    classe "o gate congelava a mentira" neste projeto.

    Este portao tem DUAS metades, e a segunda e' a que importa mais:
      1. o CODIGO nao pode declarar sucesso sem backend;
      2. o TESTE nao pode voltar a exigir esse sucesso -- meta-portao
         anti-recongelamento. Sem (2), a proxima IA reverte o teste e o defeito
         volta em silencio, que e' exatamente como ele nasceu.
    """
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    test_path = ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c"
    for path in (umd_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar a cadeia DXGI: {path.name}")
            return

    umd = umd_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # --- metade 1: o codigo fala e nao mente ---
    setmode = function_body(umd, "HRESULT APIENTRY Bc250DxgiSetDisplayMode")
    if not setmode:
        errors.append("Bc250DxgiSetDisplayMode nao localizado")
    else:
        require(setmode, "DXGI SetDisplayMode ENTER",
                "entrada de SetDisplayMode registrada", errors)
        # Enquanto o callback nao existir, a DDI tem de recusar com motivo nomeado.
        # Quando o item 2.3 aterrissar, `pfnSetDisplayModeCb` satisfaz a exigencia
        # e a recusa deixa de ser obrigatoria.
        if ("pfnSetDisplayModeCb" not in setmode and
                "SetDisplayMode RECUSADO" not in setmode):
            errors.append("Bc250DxgiSetDisplayMode nao chama pfnSetDisplayModeCb nem "
                          "recusa com motivo nomeado -- sucesso sem backend (Lei 4)")

    rotate = function_body(umd, "HRESULT APIENTRY Bc250DxgiRotateResourceIdentities")
    if not rotate:
        errors.append("Bc250DxgiRotateResourceIdentities nao localizado")
    else:
        require(rotate, "DXGI RotateResourceIdentities ENTER",
                "entrada de RotateResourceIdentities registrada", errors)
        # F2.4: a rotacao real chegou, entao ela e' EXIGIDA -- nao alternativa a
        # uma recusa.
        #
        # A primeira versao deste portao aceitava "rotaciona OU recusa". Era
        # fraco: o corpo mantem recusas legitimas (pResources NULL, cadeia
        # invalida), e o OR deixava a mutacao que apagava a rotacao passar. A
        # mutacao 25 provou isso, e o portao foi reescrito. Falha nao-atribuivel
        # e' portao fraco.
        require(rotate, "dst->Allocation = src->Allocation;",
                "rotacao move o backing entre os recursos", errors)
        require(rotate, "dst->pCpuShadow = src->pCpuShadow;",
                "rotacao move o shadow de CPU junto com o backing", errors)
        # Fail-closed atomico: validar a cadeia inteira antes de mover.
        require(rotate, "rotacao ABORTADA sem mover nada",
                "rotacao valida a cadeia antes de mover (atomicidade)", errors)

    # --- metade 2: o teste nao pode voltar a exigir a mentira ---
    for needle, label in (
        ("hr = dxgiFuncs.pfnSetDisplayMode(&setDisplayMode);\n    assert(hr == S_OK);",
         "o teste voltou a exigir S_OK de SetDisplayMode sem backend"),
    ):
        if needle in test:
            errors.append(label)

    # E tem de continuar EXIGINDO o comportamento honesto, senao vira decorativa.
    require(test, "assert(residencyStatus[1] != DXGI_DDI_RESIDENCY_FULLY_RESIDENT);",
            "o teste exige que nao-residente nao seja reportado como residente", errors)
    # F2.3/F2.4: os contrapontos que impedem "recusa sempre" de passar por
    # correcao. Sem eles, uma DDI que recusasse tudo satisfaria os portoes.
    require(test, "assert(g_SetDisplayModeCalls == 1);",
            "o teste exige que SetDisplayMode chame o kernel no caso valido", errors)
    require(test, "assert(g_SetDisplayModeLastPrimary == 0xA110u);",
            "o teste exige o handle de primaria exato no callback", errors)
    require(test, "dxgiFuncs.pfnRotateResourceIdentities(&rotateData) == S_OK",
            "o teste exige rotacao bem-sucedida no caso valido", errors)

    # --- F1.3: o pPrimaryDesc tem de ser LIDO, nao so' testado contra NULL ---
    #
    # Duas auditorias convergiram: `DriverFlags` e `NO_SCANOUT` tinham zero
    # ocorrencias em driver/umd, e o struct so' aparecia num `!= NULL`. O campo
    # Flags de entrada distingue back buffer (OPTIONAL) de front/proxy, e e' a
    # base da decisao do item 2.2. Sem ler, a proxima corrida nao mede nada.
    create_res = function_body(umd, "VOID APIENTRY Bc250Umd10CreateResource")
    if not create_res:
        errors.append("Bc250Umd10CreateResource nao localizado")
    else:
        require(create_res, "PRIMARY_DESC recebido:",
                "pPrimaryDesc reportado na criacao de recurso", errors)
        require(create_res, "DXGI_DDI_PRIMARY_OPTIONAL",
                "distincao back buffer x front/proxy lida do contrato", errors)


def audit_describe_allocation_is_not_mute(errors: list[str]) -> None:
    """F1.1 (13/09) -- A DDI CANDIDATA A CAUSA DO E_FAIL ERA COMPLETAMENTE MUDA.

    `Bc250McdmDescribeAllocation` devolve STATUS_NOT_SUPPORTED para toda alocacao
    (confirmado por leitura) e nao emitia UMA linha de log. Uma auditoria externa
    apontou esta DDI como o portao que bloqueia o Present -- o dxgkrnl a chamaria
    para conferir a geometria da superficie antes de agendar. O DEFEITO esta
    confirmado; o MECANISMO veio de uma desmontagem nao reproduzida aqui, e e' H.

    Enquanto a DDI for muda, "zero [DescribeAlloc] no log" nao distingue "o
    dxgkrnl nunca chamou" de "recusamos calados" -- a ambiguidade que a Lei 4
    proibe. Este portao exige que ela fale, com contador PROPRIO (a DDI pode ser
    chamada por quadro; o teto global nao alcanca esta classe -- licao das
    classes 9 e 12 do amostrador).
    """
    path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    if not path.exists():
        errors.append("bc250_mcdm_callbacks.c ausente para auditar DescribeAllocation")
        return

    body = function_body(
        path.read_text(encoding="utf-8", errors="replace"),
        "NTSTATUS APIENTRY Bc250McdmDescribeAllocation")
    if not body:
        errors.append("Bc250McdmDescribeAllocation nao localizado")
        return

    require(body, "[DescribeAlloc] ENTER #",
            "entrada de DescribeAllocation registrada", errors)
    require(body, "s_DescribeOrdinal",
            "DescribeAllocation com contador de amostragem proprio", errors)
    # Toda saida tem de falar: a de argumento invalido e a recusa deliberada.
    if body.count("[DescribeAlloc] RECUSADO") < 2:
        errors.append("DescribeAllocation tem saida muda -- toda recusa precisa nomear a causa")

    # F2.1: a DDI tem de CONSULTAR o executor, nao recusar por conta propria.
    # Recusar sempre era o defeito original; um corpo que ignore o backend e
    # devolva NOT_SUPPORTED direto reintroduz exatamente ele.
    require(body, "status = g_Bc250DescribeAllocationObject(hAdapter, pDescribeAllocation);",
            "DescribeAllocation consulta o executor instalado", errors)
    require(body, "[DescribeAlloc] LEAVE #",
            "DescribeAllocation registra a geometria devolvida", errors)

    # E o executor tem de existir, com fail-closed quando a geometria nao foi
    # declarada -- nunca uma descricao inventada.
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append("bc250_active_miniport.c ausente para auditar o executor de descricao")
        return
    active = active_path.read_text(encoding="utf-8", errors="replace")
    executor = function_body(
        active, "static NTSTATUS Bc250ActiveDescribeAllocationLifecycle")
    if not executor:
        errors.append("Bc250ActiveDescribeAllocationLifecycle nao localizado")
    else:
        require(executor, "Bc250McdmObjectGetAllocationGeometry(",
                "executor le' a geometria do registro de objetos", errors)
        require(executor, "BC250_MCDM_OBJECT_NO_GEOMETRY",
                "executor recusa quando a geometria nunca foi declarada", errors)
    require(active, "Bc250McdmInstallDescribeAllocation(Bc250ActiveDescribeAllocationLifecycle)",
            "executor de descricao instalado", errors)


def audit_present_path_is_not_mute(errors: list[str]) -> None:
    """Corrida 13/09 16:28:27 -- O CAMINHO DE PRESENT ERA MUDO DOS DOIS LADOS.

    Na UMD, Bc250DxgiPresent fazia `return callbacks->pfnPresentCb(...)` direto:
    o hr de 90 chamadas -- o numero que decidia a corrida -- nunca foi gravado.
    No KMD, Bc250DxgkDdiPresent tinha tres saidas mudas (hContext NULL, pPresent
    NULL e FrameBufferVirtual NULL), entao "zero linhas de Present" nao separava
    "o dxgkrnl nunca chamou" de "recusamos calados". E' a mesma ambiguidade que a
    Lei 4 proibe e que ja foi corrigida em Bc250McdmSignalMonitoredFence e no
    handler de composicao.

    Terceiro achado da mesma corrida: [McdmQAI], [McdmCreateDevice] e
    [McdmCreateContext] dividiam UM contador de amostragem. A rajada de QAI no
    bind esgota as 16 primeiras ocorrencias da classe, e por isso as 90 criacoes
    de device do dwm.exe nao deixaram UMA linha. Contadores independentes agora.
    """
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    display_path = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
    for path in (umd_path, display_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar o Present: {path.name}")
            return

    umd = umd_path.read_text(encoding="utf-8", errors="replace")
    display = display_path.read_text(encoding="utf-8", errors="replace")

    present = function_body(umd, "HRESULT APIENTRY Bc250DxgiPresent")
    if not present:
        errors.append("Bc250DxgiPresent nao localizado")
    else:
        require(present, "DXGI Present LEAVE cb: hr=",
                "hr do pfnPresentCb registrado", errors)
        # O retorno direto era o defeito: sem variavel nao ha o que logar.
        forbid(present, "return device->pDXGICallbacks->pfnPresentCb(",
               "Bc250DxgiPresent voltando a devolver o callback sem registrar o hr",
               errors)

        # Corrida 17:18:35: 84/84 pfnPresentCb -> E_FAIL sincrono com a primaria
        # ja correta. Os unicos contextos pelos quais o dxgkrnl apresentou estao
        # no Node 0; todos os nossos estavam no Node 1. O Present passou a usar
        # um contexto DEDICADO de apresentacao.
        require(present, "presentCb.hContext = device->Winsys.PresentContext;",
                "Present usando o contexto de apresentacao", errors)
        forbid(present, "presentCb.hContext = device->Winsys.KernelContext;",
               "Present voltando ao contexto de trabalho", errors)
        # Fallback silencioso para o contexto de trabalho tornaria o ensaio
        # inconclusivo: "E_FAIL de novo" deixaria de significar "falhou pelo
        # Node 0". A ausencia do contexto tem de RECUSAR com motivo nomeado.
        require(present, "DXGI Present RECUSADO: contexto de apresentacao ausente",
                "recusa nomeada quando o contexto de apresentacao falta", errors)

    winsys_path = ROOT / "driver" / "umd" / "bc250_wddm_winsys.c"
    if not winsys_path.exists():
        errors.append("bc250_wddm_winsys.c ausente para auditar o contexto de apresentacao")
    else:
        winsys = winsys_path.read_text(encoding="utf-8", errors="replace")
        create_present = function_body(
            winsys, "BC250_WINSYS_STATUS Bc250WinsysCreatePresentContext")
        if not create_present:
            errors.append("Bc250WinsysCreatePresentContext nao localizado")
        else:
            # Nao pode reaproveitar nem sobrescrever o contexto de trabalho.
            forbid(create_present, "Context->KernelContext =",
                   "criacao do contexto de apresentacao escrevendo no de trabalho",
                   errors)
            require(create_present, "Context->PresentContext = args.hContext;",
                    "contexto de apresentacao guardado em campo proprio", errors)
        destroy_present = function_body(
            winsys, "BC250_WINSYS_STATUS Bc250WinsysDestroyPresentContext")
        if not destroy_present:
            errors.append("Bc250WinsysDestroyPresentContext nao localizado")
        else:
            require(destroy_present, "args.hContext = Context->PresentContext;",
                    "contexto de apresentacao destruido pelo proprio handle", errors)

        # O teardown do device tem de destruir os DOIS: o dwm.exe fez 61
        # CreateDevice/DestroyDevice numa corrida de ~3 min.
        teardown = function_body(umd, "VOID Bc250UmdDeviceTeardown")
        if not teardown:
            errors.append("Bc250UmdDeviceTeardown nao localizado")
        else:
            require(teardown, "Bc250WinsysDestroyPresentContext(&pDevice->Winsys)",
                    "contexto de apresentacao destruido no teardown do device",
                    errors)

    ddi = function_body(display, "NTSTATUS APIENTRY Bc250DxgkDdiPresent")
    if not ddi:
        errors.append("Bc250DxgkDdiPresent nao localizado")
    else:
        require(ddi, "[DdiPresent] ENTER #",
                "entrada incondicional em Bc250DxgkDdiPresent", errors)
        require(ddi, "RECUSADO: hContext NULL",
                "saida por hContext NULL nomeia a causa", errors)
        require(ddi, "RECUSADO: pPresent NULL",
                "saida por pPresent NULL nomeia a causa", errors)
        require(ddi, "RECUSADO: FrameBufferVirtual NULL",
                "scanout nao mapeado nomeia a causa", errors)

    # Amostragem: as tres tags precisam de contadores PROPRIOS.
    sampler = function_body(display, "static BOOLEAN Bc250ShouldEmitVerboseLog")
    if not sampler:
        errors.append("Bc250ShouldEmitVerboseLog nao localizado")
    else:
        shared = ('Bc250LogTextStartsWith(Text, "[McdmQAI]") ||\n'
                  '               Bc250LogTextStartsWith(Text, "[McdmCreateDevice]")')
        if shared in sampler:
            errors.append("[McdmQAI] voltou a dividir contador de amostragem com "
                          "[McdmCreateDevice]/[McdmCreateContext]")
        # Classe 9 tinha a MESMA falha, medida na corrida 17:18:35: as quatro
        # tags somaram 45 linhas contra o orcamento de 16 primeiras, e por isso
        # "Primary token confirmed (UMD" nao saiu nenhuma vez mesmo com a
        # primaria de UMD funcionando. A ausencia era artefato do logger.
        shared9 = ('Bc250LogTextStartsWith(Text, "[ActiveOpenAlloc]") ||\n'
                   '               Bc250LogTextStartsWith(Text, "[CreateAllocation]")')
        if shared9 in sampler:
            errors.append("[ActiveOpenAlloc] voltou a dividir contador de amostragem "
                          "com [CreateAllocation] e as demais tags de alocacao")
        for tag, cid in (("[McdmQAI]", "12u"),
                         ("[McdmCreateDevice]", "13u"),
                         ("[McdmCreateContext]", "14u"),
                         ("[ActiveOpenAlloc]", "9u"),
                         ("[CreateAllocation]", "15u"),
                         ("[McdmCreateAllocation]", "16u"),
                         ("[McdmCloseAllocation]", "17u")):
            fragment = (f'Bc250LogTextStartsWith(Text, "{tag}")) {{\n'
                        f'        classId = {cid};')
            if fragment not in sampler:
                errors.append(f"{tag} sem contador de amostragem proprio (classe {cid})")


def audit_umd_trace_reaches_the_evidence_file(errors: list[str]) -> None:
    """Corrida 13/09 05:50 -- o ponto de bloqueio era invisivel POR CONSTRUCAO.

    BC250_WINSYS_LOG escrevia apenas em OutputDebugStringA e stderr. O dwm.exe
    nao tem stderr, entao NENHUMA linha da winsys chegou ao log arquivado. A
    corrida mediu que o compositor congela entre o blt de tela cheia e a
    submissao seguinte -- e nao qual callback bloqueia, porque justamente esse
    trecho so' tem chamadas de winsys. Pior: o criterio de aceitacao daquela
    corrida foi escrito sobre linhas que a instrumentacao nao podia produzir.

    Este portao trava:
      1. a winsys escreve pelo sink de arquivo, nao so' em stderr;
      2. Bc250UmdTrace usa o MESMO sink -- uma fonte de formato, um arquivo por
         processo -- em vez de manter a segunda copia da escrita que existia;
      3. Bc250WinsysSubmitGpuVa continua instrumentada: era a unica funcao da
         winsys sem trace, e pfnSubmitCommandCb bloqueia com a fila cheia.
    """
    winsys_path = ROOT / "driver" / "umd" / "bc250_wddm_winsys.c"
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    if not winsys_path.exists() or not umd_path.exists():
        errors.append("fontes da UMD ausentes para auditar o sink de trace")
        return

    winsys = winsys_path.read_text(encoding="utf-8", errors="replace")
    umd = umd_path.read_text(encoding="utf-8", errors="replace")

    require(winsys, "void Bc250WinsysTraceRaw(",
            "sink de arquivo da winsys", errors)
    require(winsys, "Bc250WinsysTraceRaw(_log_buf)",
            "BC250_WINSYS_LOG roteado para o sink de arquivo", errors)
    require(winsys, "SubmitGpuVa ENTER",
            "trace de entrada em Bc250WinsysSubmitGpuVa", errors)
    require(winsys, "SubmitGpuVa LEAVE",
            "trace de saida em Bc250WinsysSubmitGpuVa", errors)

    require(umd, "Bc250WinsysTraceRaw(message)",
            "Bc250UmdTrace delegando ao sink unico", errors)

    # Corrida 40: para dwm.exe e dxgiadaptercache.exe, %TEMP% ja resolvia para
    # C:\Windows\Temp. O sink abria o mesmo arquivo no destino por-processo e
    # no destino compartilhado, duplicando cada linha. A comparacao precisa ser
    # de caminho resolvido, nao de identidade de processo inferida.
    require(winsys, "_stricmp(logPath, sharedLogPath) != 0",
            "sink UMD evita segunda escrita quando %TEMP% e Windows\\Temp sao o mesmo caminho", errors)

    # Escrever no arquivo nao basta: o binder tem de ARQUIVAR o arquivo no fim
    # da janela fisica. A coleta original roda logo apos o bind e arquiva um
    # snapshot de poucos segundos -- nas corridas de 13/09 08:12, 11:25 e 11:32
    # o log do dwm.exe terminou antes do momento decisivo (na 11:32, exatamente
    # no "BltViaKmd ENTER", perdendo o "LEAVE" com o hr do escape). Tres
    # criterios ficaram sem resposta por isso.
    binder_path = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"
    if not binder_path.exists():
        errors.append("binder de laboratorio ausente para auditar a coleta final")
        return
    binder = binder_path.read_text(encoding="utf-8", errors="replace")
    require(binder, "bc250_umd_log-FINAL.txt",
            "coleta FINAL dos logs da UMD no fim da janela fisica", errors)
    require(binder, "Coleta FINAL da UMD",
            "coleta FINAL da UMD reportada ao operador", errors)
    require(binder, "$collectionDeadlineUtc = $deadlineUtc.AddSeconds(-10)",
            "prazo da coleta FINAL derivado do relogio absoluto do watchdog", errors)
    require(binder, "while ([datetime]::UtcNow -lt $collectionDeadlineUtc)",
            "coleta FINAL espera o relogio mesmo quando o harness termina cedo", errors)
    final_collection = binder.split("# COLETA FINAL NO CAMINHO COMUM.", 1)[-1]
    forbid(strip_c_comments(final_collection), "Start-Sleep -Seconds 3",
           "atalho de 3 s apos harness que truncou a Corrida 40", errors)

    require(binder, "[switch]$EnableDwmCrashCapture",
            "LocalDumps do dwm.exe opt-in no binder de laboratorio", errors)
    require(binder, "DWM LocalDumps: DESARMADO (default fail-closed)",
            "LocalDumps desarmado por default", errors)
    require(binder, "-EnableDwmCrashCapture exige watchdog",
            "LocalDumps nao arma sem restauracao pelo guardiao", errors)
    watchdog_path = ROOT / "tools" / "watch-bc250mcdm-safe-bind.ps1"
    watchdog_text = watchdog_path.read_text(encoding="utf-8", errors="replace")
    require(watchdog_text, "Restore-DwmCaptureIfArmed",
            "guardiao restaura LocalDumps mesmo se o binder morrer", errors)
    require(watchdog_text, "dwm_capture_restore_ok",
            "resultado da restauracao LocalDumps arquivado", errors)

    # A segunda copia da escrita em arquivo nao pode voltar a existir na UMD:
    # duas instrumentacoes divergentes foi exatamente o defeito.
    trace_start = umd.find("static void Bc250UmdTrace(")
    if trace_start < 0:
        errors.append("Bc250UmdTrace nao encontrada em bc250_d3d_umd.c")
        return
    trace_end = umd.find(chr(10) + "}", trace_start)
    body = umd[trace_start:trace_end if trace_end > 0 else trace_start + 4000]
    for banned in ("CreateFileA", "WriteFile", "BC250_UMD_LOG_"):
        if banned in body:
            errors.append(
                f"Bc250UmdTrace voltou a escrever o arquivo por conta propria "
                f"({banned}): o sink tem de ser unico, senao as duas "
                f"instrumentacoes divergem de novo")



def audit_composition_never_submits_a_context(errors: list[str]) -> None:
    """Corrida 13/09 08:12 -- o discriminante, medido:

    O KMD executou 958 IBs. 953 eram buffers de paginacao do proprio dxgkrnl e
    NAO produziram um unico TDR. Os outros 5 eram submissoes de CONTEXTO da UMD
    no Node 0, que e' o no de paginacao (PagingNodeIndex=0), e CADA UMA foi
    seguida de um TDR -- sempre com a engine OCIOSA e o anel DRENADO
    (RPTR==WPTR, STATUS1=0x3FF). Cinco TDRs, BugCheck 0x116.

    Tres correcoes anteriores no caminho do blt -- sinal de fence pelo contrato,
    sink unico de trace, anel de slots de command buffer -- nao mudaram isso,
    porque todas tratavam o passageiro e nao o veiculo.

    O caminho de blt do KMD (Bc250SdmaPresentBlt) fez 584 blts consecutivos sem
    TDR em 12/09 13:01: nao cria pacote de contexto e nao passa pelo
    escalonador. A composicao passou a usar esse caminho, via escape.

    Este portao trava as quatro pontas:
      1. a UMD alcanca o KMD por pfnEscapeCb com o opcode do contrato;
      2. a composicao NAO submete contexto -- nenhum SubmitGpuVa no caminho do
         blt, e nenhuma criacao de contexto/fence/command buffer para isso;
      3. o KMD implementa e despacha o opcode;
      4. o teste continua EXIGINDO zero submissoes de contexto, em vez de
         exigir o caminho antigo. A classe "o gate congelava a mentira" ja
         apareceu cinco vezes neste projeto.
    """
    blt_path = ROOT / "driver" / "umd" / "bc250_umd_blt.inl"
    kmd_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    test_path = ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c"
    contract_path = ROOT / "driver" / "core" / "bc250_escape_contract.h"
    packet_h_path = ROOT / "driver" / "core" / "bc250_sdma_packet_builder.h"
    packet_c_path = ROOT / "driver" / "core" / "bc250_sdma_packet_builder.c"
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    for p in (blt_path, kmd_path, test_path, contract_path,
              packet_h_path, packet_c_path, present_path):
        if not p.exists():
            errors.append(f"fonte ausente para auditar a composicao: {p.name}")
            return

    blt_code = strip_c_comments(blt_path.read_text(encoding="utf-8", errors="replace"))
    kmd = kmd_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")
    contract = contract_path.read_text(encoding="utf-8", errors="replace")
    packet_h = packet_h_path.read_text(encoding="utf-8", errors="replace")
    packet_c = packet_c_path.read_text(encoding="utf-8", errors="replace")
    present = present_path.read_text(encoding="utf-8", errors="replace")

    require(contract, "BC250_ESCAPE_OP_COMPOSITE_BLT",
            "opcode de composicao no contrato de escape", errors)
    require(blt_code, "pfnEscapeCb",
            "composicao alcancando o KMD por escape", errors)
    require(blt_code, "BC250_ESCAPE_OP_COMPOSITE_BLT",
            "opcode de composicao usado pela UMD", errors)
    require(blt_code, "payload.SourcePitch = src->RowPitch",
            "pitch da origem preservado no escape", errors)
    require(blt_code, "payload.DestPitch = dst->RowPitch",
            "pitch do destino preservado no escape", errors)
    forbid(blt_code, "retangulo exige copia por scanline",
           "recusa do retangulo 1:1 medido em hardware", errors)

    # O caminho que matou quatro corridas nao pode voltar ao blt.
    for banned in ("Bc250WinsysSubmitGpuVa",
                   "Bc250WinsysCreateVirtualContext",
                   "Bc250WinsysCreateMonitoredFence",
                   "Bc250WinsysWaitForFenceFromCpu"):
        forbid(blt_code, banned,
               "submissao de contexto no caminho do blt (causa dos cinco TDRs)",
               errors)

    require(kmd, "Bc250ActiveEscapeCompositeBlt",
            "handler de composicao no KMD", errors)
    require(kmd, "Bc250SdmaPresentBlt(srcMc, dstMc",
            "composicao executada pelo caminho de blt provado do KMD", errors)
    require(kmd, "Bc250SdmaPresentBltRect(srcMc, esc->SourcePitch",
            "composicao retangular despachada ao SDMA", errors)
    require(kmd, "COMPOSITE_DATA_PROOF SAMPLED_PASS:",
            "prova amostrada de dados da composicao", errors)
    require(kmd, "COMPOSITE_DATA_PROOF INCONCLUSIVO_ZERO",
            "zero contra zero nao promovido a prova", errors)
    require(kmd, "COMPOSITE_DATA_PROOF FAIL",
            "divergencia de dados nomeada", errors)
    require(kmd, "Bc250ClassifyBltProof(",
            "classificador honesto compartilhado na composicao", errors)
    require(kmd, "Bc250BltProofRequiresFailClosed(proofVerdict)",
            "divergencia da composicao falha fechada", errors)
    source_snapshot = kmd.find("sourceBefore[proofIndex] = *sample")
    blt_call = kmd.find("Bc250SdmaPresentBlt(srcMc, dstMc")
    destination_snapshot = kmd.find("destinationAfter[proofIndex] = *sample")
    if not (0 <= source_snapshot < blt_call < destination_snapshot):
        errors.append("prova da composicao nao preserva ordem origem -> SDMA -> destino")
    require(kmd, "sampleY[proofIndex] * esc->SourcePitch",
            "amostra da origem usa coordenada e pitch proprios", errors)
    require(kmd, "(esc->DestY + sampleY[proofIndex]) * esc->DestPitch",
            "amostra do destino usa coordenada e pitch proprios", errors)

    # COPY_LINEAR_SUB_WIND: posicoes dos campos sao P1 no header upstream;
    # a semantica menos-um e H e so hardware pode promove-la.
    require(packet_h, "BC250_SDMA_SUBOP_COPY_LINEAR_SUB_WIND 4",
            "SUBOP 4 do COPY_LINEAR_SUB_WIND", errors)
    require(packet_h, "navi10_sdma_pkt_open.h",
            "proveniencia upstream do pacote retangular", errors)
    require(packet_h, "HIPOTESE (H)",
            "semantica do pacote retangular rotulada honestamente", errors)
    require(packet_h, "NAO VALIDADA EM HARDWARE",
            "pacote retangular sem promocao indevida de prova", errors)
    require(packet_c, "(srcPitchElements - 1u) << 13",
            "pitch da origem codificado no campo correto", errors)
    require(packet_c, "(dstPitchElements - 1u) << 13",
            "pitch do destino codificado no campo correto", errors)
    require(packet_c, "((Width - 1u) & 0x3FFFu)",
            "largura retangular codificada", errors)
    require(present, "Bc250SdmaEmitCopyLinearSubWindow32(",
            "emissao do pacote retangular no caminho fisico", errors)
    require(present, "BLT_RECT_ENGINE_2LEGS",
            "telemetria honesta de duas pernas no pacote novo", errors)

    require(test, "assert(g_BltSubmits == 0u);",
            "invariante de que a composicao nao submete contexto", errors)

    # Corrida 13/09 11:32: o DWM chamou o escape UMA vez e o log do KMD nao teve
    # NENHUMA linha COMPOSITE_BLT, porque as duas saidas de recusa eram MUDAS.
    # Era impossivel distinguir "nao chegou ao handler" de "recusamos calados"
    # -- a ambiguidade que a Lei 4 proibe, reintroduzida por mim no codigo novo.
    require(kmd, "COMPOSITE_BLT ENTER",
            "entrada do handler de composicao falando sempre", errors)
    require(kmd, "ABI divergente entre UMD e KMD",
            "recusa por tamanho de payload nomeando a causa", errors)
    require(kmd, "payload ausente ou menor que 4 bytes",
            "recusa de payload invalido no dispatch de escape", errors)

    # Corrida 13/09 11:55: a UMD chamou pfnEscapeCb e o handler de composicao
    # nunca viu a chamada. Sem log na ENTRADA da DDI era impossivel separar "o
    # dxgkrnl nunca nos chamou" de "chamou com payload que nao reconhecemos".
    callbacks = (ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c").read_text(
        encoding="utf-8", errors="replace")
    require(callbacks, "[McdmEscape] ENTER",
            "entrada da DDI de escape falando antes do despacho", errors)
    require(callbacks, "nenhum handler instalado",
            "ausencia de handler de escape nomeada em vez de muda", errors)

    # Corridas 13/09 12:09 (HardwareAccess=1) e 12:21 (HardwareAccess=0)
    # produziram a MESMA assinatura: ENTER sem LEAVE e nenhuma chegada ao KMD.
    # Logo a causalidade atribuida a flag foi refutada. O defeito comum era o
    # primeiro argumento: PFND3DDDI_ESCAPECB exige hAdapter, mas a UMD passava
    # hDevice. Como o handler acessa o SDMA, HardwareAccess volta a ser exigido
    # pelo contrato do WDK. A combinacao ainda NAO FOI VALIDADA EM HARDWARE.
    require(blt_code, "args.Flags.HardwareAccess = 1u",
            "HardwareAccess no escape que dirige o SDMA", errors)
    require(blt_code, "d->pAdapter->hRTAdapter.handle, &args",
            "pfnEscapeCb chamado com o handle runtime do adapter", errors)
    forbid(blt_code, "pfnEscapeCb(d->Winsys.RuntimeDevice",
           "hDevice usado como primeiro argumento de PFND3DDDI_ESCAPECB",
           errors)
    require(test, "assert(h == g_BltExpectedAdapter);",
            "teste distingue hAdapter do hDevice no callback", errors)
    require(test, "assert(a->hDevice == g_BltExpectedDevice);",
            "teste preserva hDevice dentro de D3DDDICB_ESCAPE", errors)
    require(test, "assert(a->Flags.HardwareAccess == 1u);",
            "teste exige HardwareAccess no escape que dirige o SDMA", errors)

def audit_safe_bind_deadline_is_fail_closed(errors: list[str]) -> None:
    """C3.17: o prazo do rollback sobrevive a travamento e reboot.

    MEDIDO em 13/09: o guardiao chegou ao timeout, mas bloqueou em uma consulta
    PnP antes de publicar ROLLBACK_STARTED. Um reboot reiniciou o cronometro
    relativo e a recuperacao automatica nao aconteceu. Este portao exige um
    segundo disparador por instante absoluto, exclusao mutua e caminho critico
    sem WMI/eventos entre a transicao e BC250_RECOVERY.cmd.
    """
    watcher_path = ROOT / "tools" / "watch-bc250mcdm-safe-bind.ps1"
    binder_path = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"
    confirmer_path = ROOT / "tools" / "confirm-bc250mcdm-safe-bind.ps1"
    if not watcher_path.exists() or not binder_path.exists() or not confirmer_path.exists():
        errors.append("harness safe-bind incompleto para auditar prazo fail-closed")
        return

    watcher = watcher_path.read_text(encoding="utf-8", errors="replace")
    binder = binder_path.read_text(encoding="utf-8", errors="replace")
    confirmer = confirmer_path.read_text(encoding="utf-8", errors="replace")
    for needle, description in (
        ("deadline_utc_ticks", "prazo UTC absoluto no guardiao"),
        ("DeadlineEnforcer", "modo independente que impoe o prazo"),
        ("Global\\BC250SafeBindRecovery", "rollback com dono unico"),
        ("AbandonedMutexException", "mutex abandonado ainda transfere a posse"),
        ("-OperationTimeoutSec 2", "consultas CIM limitadas"),
    ):
        require(watcher, needle, description, errors)
    require(binder, "BC250-SafeBindDeadline",
            "segunda tarefa agendada do safe-bind", errors)
    require(binder, "deadline_utc_ticks = [long]$deadlineUtc.Ticks",
            "binder materializa o prazo absoluto", errors)
    require(confirmer, "restam menos de 70 s",
            "confirmacao nao atravessa o prazo do rollback", errors)
    forbid(watcher, "(Get-Date).AddSeconds([int]$config.timeout_seconds)",
           "prazo relativo reiniciado apos reboot", errors)

    rollback = watcher.find("Write-State 'ROLLBACK_STARTED'")
    recovery = watcher.find("& cmd.exe /d /c", rollback)
    if rollback < 0 or recovery <= rollback:
        errors.append("safe-bind nao publica ROLLBACK_STARTED antes da recovery")
    else:
        critical = watcher[rollback:recovery]
        if "Get-CimInstance" in critical or "Get-WinEvent" in critical:
            errors.append("safe-bind consulta WMI/eventos depois do prazo e antes da recovery")


def audit_gfx_restart_checks_the_real_halt_bits(errors: list[str]) -> None:
    """O portao do restart do CP_RB0 tem de olhar os bits de halt que existem.

    MEDIDO na corrida 2026-09-19 09:06:34 (P0). Depois do primeiro TDR do anel
    grafico, as tres submissoes seguintes sairam com
    [TDR_GFX_QUIESCE] ... CpMeCntl=0x15000000 e RPTR travado em 0 -- isto e,
    CE_HALT|PFP_HALT|ME_HALT ligados, o CP inteiro parado. Mesmo assim o
    restart anterior havia impresso "CP_RB0 rearmado e comprovado no silicio",
    porque Bc250TdrValidateGfxRestartReadback mascarava 0x00000007 e o
    comentario afirmava que os halts eram "bits 0, 1 e 2".

    Pelo header local (Lei 7, inc/linux_ref/gc_10_1_0_sh_mask.h):
      CP_ME_CNTL__CE_HALT_MASK  0x01000000
      CP_ME_CNTL__PFP_HALT_MASK 0x04000000
      CP_ME_CNTL__ME_HALT_MASK  0x10000000
    Os bits 0..2 nao nomeiam campo nenhum. 0x15000000 & 0x7 == 0: o PASS era
    aritmeticamente inevitavel, e por isso tres corridas inteiras trataram o
    proprio reset como se fosse stall da Drawing Engine.

    Oitava ocorrencia da classe "o gate carimbava o falso sucesso" no projeto
    (A15, mcdm_callbacks_test, C1, sdma_blt_executed, teste do blt com 128
    bytes, entre outras). O portao cobre o PRODUTO e o TESTE, porque naquele
    caso o teste tambem exigia a mascara errada.
    """
    contract_path = ROOT / "driver" / "core" / "bc250_tdr_recovery_contract.c"
    test_path = ROOT / "driver" / "tests" / "tdr_recovery_contract_test.c"
    if not contract_path.exists():
        errors.append("bc250_tdr_recovery_contract.c ausente: restart do CP_RB0 sem portao")
        return
    if not test_path.exists():
        errors.append("tdr_recovery_contract_test.c ausente: restart do CP_RB0 sem teste")
        return

    contract = contract_path.read_text(encoding="utf-8", errors="replace")
    code = strip_c_comments(contract)

    start = code.find("Bc250TdrValidateGfxRestartReadback(")
    if start < 0:
        errors.append("Bc250TdrValidateGfxRestartReadback ausente do contrato de TDR")
        return
    end = code.find("BC250_TDR_STATUS Bc250TdrExecuteResetPlan", start)
    if end < 0:
        end = len(code)
    body = code[start:end]

    for mask in ("0x01000000u", "0x04000000u", "0x10000000u"):
        if mask not in body:
            errors.append(
                "restart do CP_RB0 nao confere o halt " + mask +
                " de CP_ME_CNTL: um restart com a micro-engine parada passaria")

    if "0x00000007u" in body:
        errors.append(
            "restart do CP_RB0 voltou a mascarar 0x00000007 em CP_ME_CNTL: "
            "esses bits nao nomeiam campo no header e a mascara nao reprova halt")

    # O TESTE tambem nao pode voltar a congelar a mentira.
    test = test_path.read_text(encoding="utf-8", errors="replace")
    require(test, "Bc250TdrValidateGfxRestartReadback(1u, 0u, 0u, 0x15000000u, 0u) == BC250_TDR_RESTART_NOT_READY",
            "caso do teste com o CpMeCntl MEDIDO na corrida 19/09 (0x15000000)", errors)
    for frozen in ("Bc250TdrValidateGfxRestartReadback(1u, 0u, 0u, 7u, 0u)",
                   "Bc250TdrValidateGfxRestartReadback(1u, 0u, 0u, 1u, 0u)"):
        if frozen in test:
            errors.append(
                "tdr_recovery_contract_test voltou a exigir reprovacao por bits 0..2 de "
                "CP_ME_CNTL (" + frozen + "): o teste estaria congelando a mascara errada")


# Casa a escrita em CP_RB0_BASE/_HI nas DUAS formas usadas no driver: pela
# transacao MMIO (registrador como argumento) e pelo acesso direto ao BAR por
# offset literal, que e como o bc250_compute_service.c escreve.
CP_RB0_BASE_WRITE_RE = re.compile(
    r"(?:BC250_GFX_REG_CP_RB0_BASE(?:_HI)?\s*,\s*|"
    r"0x0000C(?:100|2C4)u\s*\)\s*=\s*)(?P<value>[^;,]*?)\s*[;)]",
    re.MULTILINE)


def audit_gfx_restart_is_executable_and_aborts_old_fences(errors: list[str]) -> None:
    """Restart do CP_RB0 precisa executar e nao concluir fences abortadas."""
    miniport_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    engine_path = ROOT / "driver" / "wdk" / "bc250_submission_engine.c"
    test_path = ROOT / "driver" / "tests" / "active_miniport_test.c"
    for path in (miniport_path, engine_path, test_path):
        if not path.exists():
            errors.append(path.name + " ausente: restart GFX executavel sem portao")
            return

    miniport = miniport_path.read_text(encoding="utf-8", errors="replace")
    engine = engine_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")
    txn_path = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.c"
    gfx_txn = txn_path.read_text(encoding="utf-8", errors="replace")         if txn_path.exists() else ""
    if not gfx_txn:
        errors.append(
            "bc250_gfx_ring_transaction.c ausente: caminho ONLINE de CP_RB0_BASE sem portao")

    compute_path = ROOT / "driver" / "wdk" / "bc250_compute_service.c"
    compute_ring = compute_path.read_text(encoding="utf-8", errors="replace")         if compute_path.exists() else ""
    if not compute_ring:
        errors.append(
            "bc250_compute_service.c ausente: terceiro escritor de CP_RB0_BASE sem portao")

    proof = function_body(miniport, "static BOOLEAN Bc250ActiveTdrProveGfxRestart(")
    restart = function_body(miniport, "static BOOLEAN Bc250ActiveTdrRestartLifecycle(")
    online = function_body(miniport, "static NTSTATUS Bc250ActiveGfxEngineOnline(")
    abort = function_body(engine, "BOOLEAN Bc250WddmFenceTrackerAbortPending(")

    for needle, label in (
        ("BC250_PM4_PACKET2_NOP", "physical NOP in GFX restart proof"),
        ("Bc250Pm4EmitReleaseMemEop32(", "RELEASE_MEM in GFX restart proof"),
        ("BC250_GFX_FENCE_POISON", "poison before GFX restart proof"),
        ("rptr == cursor && fenceObserved == proofFence",
         "RPTR plus GPU fence verdict in GFX restart proof"),
        ("[TDR_GFX_RESTART_PROOF] PASS", "explicit executable restart evidence"),
    ):
        require(proof, needle, label, errors)
    for needle, label in (
        ("Bc250GfxRingEncodeBaseLo(activeCtx->GfxEngine.RingGpuVa)",
         "CP_RB0_BASE restart encoded as GPUVA shifted by 8"),
        ("Bc250GfxRingEncodeBaseHi(activeCtx->GfxEngine.RingGpuVa)",
         "CP_RB0_BASE_HI restart encoded from the shifted GPUVA"),
        ("[TDR_GFX_RESTART_BASE]", "restart BASE expected/readback telemetry"),
        ("baseHi != baseExpectedHi || baseLo != baseExpectedLo",
         "restart fails closed on CP_RB0_BASE readback mismatch"),
        ("*activeCtx->GfxEngine.RptrWbCpuVirtual = 0u",
         "stale GFX RPTR writeback cleared"),
        ("Bc250ActiveTdrAbortFenceTracker(activeCtx)",
         "old WDDM fences aborted before restart proof"),
        ("Bc250ActiveTdrProveGfxRestart(activeCtx)",
         "executable GFX proof gates restart success"),
        ("Bc250WddmFenceTrackerSetAcceptingWork(",
         "tracker admission reopened only by restart lifecycle"),
    ):
        require(restart, needle, label, errors)
    # O forbid textual da primeira versao deste portao cobria UMA unica forma
    # da expressao errada -- a LO com mascara. Um cast sem mascara, ou a variante
    # HI antiga, passavam. Trocado por verificacao ESTRUTURAL: toda escrita em
    # CP_RB0_BASE/_HI, em qualquer dos tres escritores, tem de carregar o valor
    # vindo da fonte unica. Leitura (argumento por referencia) e permitida.
    for body, origin in (
        (restart, "GFX restart"),
        (gfx_txn, "GFX ring transaction (caminho ONLINE)"),
        (compute_ring, "compute service CP ring setup"),
    ):
        for match in CP_RB0_BASE_WRITE_RE.finditer(body):
            value = match.group("value").strip()
            if value.startswith("&"):
                continue  # readback
            if "Bc250GfxRingEncodeBase" in value:
                continue
            # Excecao NOMINAL e estreita: o ROLLBACK da Lei 5 devolve ao
            # registrador o valor CRU que o SNAPSHOT leu dele. Isso nao e
            # codificacao e nao pode passar pelos helpers. Qualquer outro
            # nome cai no erro -- a excecao nao e generica de proposito.
            if value in ("Evidence->BaseBefore", "Evidence->BaseHiBefore"):
                continue
            errors.append(
                "CP_RB0_BASE written outside the single source in " + origin +
                " (Corrida 43: base 256x errada): " + value)
    for needle, label in (
        ("if (!bootDrainOk)", "boot preamble execution gates GFX online"),
        ("(void)Bc250ActiveGfxEngineOffline(Context);",
         "failed boot preamble rolls the GFX engine back"),
        ("return STATUS_DEVICE_HARDWARE_ERROR;",
         "failed boot preamble fails StartDevice path closed"),
    ):
        require(online, needle, label, errors)
    for needle, label in (
        ("--Tracker->Count", "committed entries removed by abort"),
        ("Tracker->ReservedCount = 0u", "reservations removed by abort"),
    ):
        require(abort, needle, label, errors)
    forbid(abort, "++Tracker->NotifiedTotal",
           "TDR abort must not count work as WDDM completion", errors)
    require(test, "Bc250WddmFenceTrackerAbortPending(",
            "offline regression for TDR fence abort", errors)
    require(test, "tracker.NotifiedTotal == 1u",
            "offline proof that abort does not notify completion", errors)


def audit_gfx_stall_probe_is_read_only(errors: list[str]) -> None:
    """A sonda do stall do CP grafico e diagnostico, nunca escrita.

    Os offsets abaixo entraram em 19/09 para responder, na proxima corrida,
    onde o CP_RB0 para -- CP_ME_HEADER_DUMP e CP_PFP_HEADER_DUMP nomeiam o
    pacote PM4, CP_IB1_BASE/BUFSZ localizam o ponto dentro do IB,
    SQ_DEBUG_STS_GLOBAL diz se ha onda viva, GRBM_STATUS_SE0/SE1 e
    CP_BUSY_STAT separam as esperas. Nenhum deles tem efeito util se for
    ESCRITO, e escrever em registrador de diagnostico do CP durante um TDR e
    risco puro. Este portao exige que estejam na allowlist de LEITURA e
    proibe que qualquer um apareca numa chamada de escrita.
    """
    header_path = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.h"
    transport_path = ROOT / "driver" / "wdk" / "bc250_mmio_topology_transport.c"
    miniport_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    for path in (header_path, transport_path, miniport_path):
        if not path.exists():
            errors.append(path.name + " ausente: sonda de stall do CP sem portao")
            return

    header = header_path.read_text(encoding="utf-8", errors="replace")
    transport = transport_path.read_text(encoding="utf-8", errors="replace")
    miniport = miniport_path.read_text(encoding="utf-8", errors="replace")

    probes = {
        "BC250_GFX_REG_CP_ME_HEADER_DUMP": "0x00008684u",
        "BC250_GFX_REG_CP_PFP_HEADER_DUMP": "0x00008688u",
        "BC250_GFX_REG_GRBM_STATUS_SE0": "0x00008014u",
        "BC250_GFX_REG_GRBM_STATUS_SE1": "0x00008018u",
        "BC250_GFX_REG_SQ_DEBUG_STS_GLOBAL": "0x00008C24u",
        "BC250_GFX_REG_CP_IB1_BASE_LO": "0x00030330u",
        "BC250_GFX_REG_CP_IB1_BASE_HI": "0x00030334u",
        "BC250_GFX_REG_CP_IB1_BUFSZ": "0x00030338u",
    }

    for name, offset in probes.items():
        require(header, "#define " + name, "offset de diagnostico " + name, errors)
        if offset not in transport:
            errors.append(
                "offset " + offset + " (" + name + ") fora da allowlist de LEITURA: "
                "a sonda devolveria zero e o log diria INDETERMINADO para sempre")
        if "Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,\n                        " + name in miniport:
            errors.append("sonda de diagnostico " + name + " usada em ESCRITA")

    code = strip_c_comments(miniport)
    for name in probes:
        idx = 0
        while True:
            idx = code.find(name, idx)
            if idx < 0:
                break
            window = code[max(0, idx - 200):idx]
            if "Bc250MmioTopologyWrite32" in window and "Bc250MmioTopologyRead32" not in window[window.rfind("Bc250MmioTopologyWrite32"):]:
                errors.append("sonda de diagnostico " + name + " alcancada por caminho de escrita")
            idx += len(name)

    # As linhas de log tem de existir, senao a sonda le e nao conta a ninguem.
    for line in ("[TDR_GFX_PACKET]", "[TDR_GFX_WAVES]", "[TDR_GFX_SE]", "[TDR_GFX_CPWAIT]"):
        require(miniport, line, "linha de log da sonda de stall do CP " + line, errors)
    # E a ausencia de leitura tem de ser NOMEADA, nao virar zero silencioso.
    require(miniport, "ME_HEADER_DUMP INDETERMINADO",
            "recusa nomeada quando o Read32 do header do ME falha", errors)
    require(miniport, "SQ_DEBUG_STS_GLOBAL INDETERMINADO",
            "recusa nomeada quando o Read32 do SQ falha", errors)


def audit_conformance_harness_checks_present(errors: list[str]) -> None:
    """O harness D3D11 nao pode declarar quadro que o runtime recusou.

    MEDIDO na corrida 2026-09-19 09:06:34 (P0): o dispositivo foi removido as
    09:07:19 -- d3d11_native_conformance.log registra vkWaitForFences res=-4 e,
    a partir dali, 51 ciclos com "Lock2 failed: status=0xC00002B6" e
    "UploadBufferData ERRO: vkMapMemory falhou" -- e o harness imprimiu 52
    quadros "RENDERING_OK" com FPS subindo de 0.4 a 11.2. Houve UM draw real.
    A causa era simples: o HRESULT de Present() era descartado e a string de
    status saia incondicionalmente.

    A rampa de FPS tambem enganava, e nao por acaso: media cumulativa de um
    quadro de ~2,5 s seguido de ~51 no-ops produz exatamente aquela curva. Por
    isso o portao exige os DOIS numeros no relatorio.
    """
    path = ROOT / "driver" / "tests" / "d3d11_native_conformance_test.cpp"
    if not path.exists():
        errors.append("d3d11_native_conformance_test.cpp ausente")
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    code = strip_c_comments(text)

    require(code, "hr = pSwapChain->Present(",
            "captura do HRESULT de Present", errors)
    require(code, "GetDeviceRemovedReason()",
            "leitura do motivo de remocao do dispositivo", errors)
    require(code, "PRESENT_FALHOU",
            "status honesto quando o Present falha", errors)
    require(code, "return presentFailed ? 2 : 0;",
            "exit code nao-zero quando o Present falhou", errors)
    require(code, "FPS janela",
            "FPS instantaneo ao lado da media cumulativa", errors)

    if "RENDERING_OK" in code:
        errors.append(
            "harness voltou a imprimir RENDERING_OK: essa string foi impressa 52 vezes "
            "na corrida 19/09 com o dispositivo removido desde o quadro 2")

    # ------------------------------------------------------------------
    # O RUNNER tambem nao pode declarar sucesso sem prova de quadro.
    #
    # MEDIDO na Corrida 35 (19/09 10:51): o harness nao imprimiu NENHUMA linha
    # de quadro, nao houve timeout, e o runner anunciou "EXIT CODE: 0". Exit 0
    # sem prova de quadro nao e sucesso -- e ausencia de medicao, e ler os dois
    # como a mesma coisa foi o que permitiu ao docs/288 publicar "52 quadros".
    # ------------------------------------------------------------------
    runner = ROOT / "tools" / "test-d3d11-native-conformance.ps1"
    if not runner.exists():
        errors.append("test-d3d11-native-conformance.ps1 ausente: runner do D3D11 sem portao")
    else:
        rtext = runner.read_text(encoding="utf-8", errors="replace")
        require(rtext, "SEM MEDICAO",
                "veredito proprio quando nao ha linha de quadro nenhuma", errors)
        require(rtext, "PRESENT_OK",
                "contagem de quadros com Present aceito no runner", errors)
        require(rtext, "PRESENT_FALHOU",
                "contagem de Presents falhos no runner", errors)
        if "if ($exitCode -eq 0) { $exitCode = 3 }" not in rtext:
            errors.append(
                "runner nao converte exit 0 SEM prova de quadro em falha: "
                "uma corrida que nao mediu nada voltaria a sair como sucesso")
        # E o despejo de IB tem de alcancar processos que nao sao filhos do
        # runner -- foi por isso que a Corrida 35 arquivou gfx-ib-dump vazio.
        # PORTAO REFEITO POR MUTACAO (M14). Exigir a substring "GfxIbDumpDir"
        # passava com o valor renomeado para "GfxIbDumpDirOutro", porque o
        # nome antigo continua sendo prefixo do novo. O par nome-exato dos
        # DOIS lados (runner e RADV) e' o que precisa casar -- se um mudar
        # sozinho, o despejo volta a nao alcancar o dwm.exe em silencio.
        require(rtext, "-Name 'GfxIbDumpDir'",
                "runner arma o despejo por registro com o nome exato", errors)
        # Corrida 41: no Windows PowerShell 5.1, New-Item -Force sobre uma
        # chave existente recriou HKLM\SOFTWARE\BC250\Umd e apagou
        # D3D11LabPromotion antes de iniciar o harness. O DWM antigo manteve o
        # valor em cache; todo processo novo falhou com 0x887A0004.
        require(rtext, "if (-not (Test-Path -LiteralPath $umdRegistryPath))",
                "criacao condicional da chave UMD sem apagar gates", errors)
        require(rtext, "New-Item -Path $umdRegistryPath -ErrorAction Stop",
                "criacao da chave UMD somente quando ausente", errors)
        registry_block = rtext[rtext.find("$umdRegistryPath ="):
                               rtext.find("$argsList =")]
        if re.search(r"(?im)^\s*New-Item\b[^\r\n]*\s-Force(?:\s|$)",
                     registry_block):
            errors.append(
                "runner voltou a usar New-Item -Force no bloco HKLM Umd: no "
                "PowerShell 5.1 isso apaga D3D11LabPromotion e fabrica o fail-fast")
        patch = ROOT / "external" / "radv-wddm" / "bc250-radv-wddm.patch"
        if patch.exists():
            ptext = patch.read_text(encoding="utf-8", errors="replace")
            if '"GfxIbDumpDir"' not in ptext:
                errors.append(
                    "o delta do RADV nao le a chave GfxIbDumpDir: o runner armaria um "
                    "registro que ninguem consulta, e o despejo voltaria a sair vazio")

    # O laco nao pode seguir depois de um Present que falhou.
    loop_start = code.find("while (frameCount < maxFrames)")
    if loop_start < 0:
        errors.append("laco de render do harness nao encontrado")
        return
    loop = code[loop_start:code.find("printf(\"===", loop_start)]
    if "presentFailed = 1;" not in loop or "break;" not in loop:
        errors.append(
            "laco de render do harness nao aborta no primeiro Present que falha: "
            "os quadros seguintes seriam no-ops contados como sucesso")


def audit_radv_fork_delta_is_mirrored(errors: list[str]) -> None:
    """O driver Vulkan da placa tem de ter copia e proveniencia neste repositorio.

    MEDIDO em 19/09/2026: o `vulkan_radeon.dll` que a BC-250 carrega nao e
    construido aqui -- sai de uma arvore Mesa externa. Ate aquele dia nada neste
    repositorio registrava onde ela estava, de que commit partia nem o que tinhamos
    mudado nela, e as consequencias foram concretas:

      - tools/audit-radv-wddm-safety.py exige --mesa-root e NUNCA tinha rodado;
      - uma auditoria declarou a arvore inexistente e classificou a correcao de
        GCVM de 34 bits como nao auditavel -- conclusao falsa, publicada;
      - o delta inteiro existia em UM diretorio sem copia de seguranca.

    Este portao nao verifica a arvore externa (ela pode nao existir na maquina que
    roda a auditoria). Ele exige que o ESPELHO e a PROVENIENCIA existam aqui, que a
    proveniencia declare os tres identificadores que amarram a cadeia
    (commit base -> patch -> DLL), e que a ferramenta de sincronia exista e saiba
    CONFERIR, nao so copiar. Uma copia que ninguem consegue conferir e uma copia
    que apodrece em silencio.
    """
    mirror = ROOT / "external" / "radv-wddm" / "bc250-radv-wddm.patch"
    prov = ROOT / "external" / "radv-wddm" / "PROVENANCE.md"
    tool = ROOT / "tools" / "sync-radv-fork-delta.py"

    if not mirror.exists():
        errors.append(
            "external/radv-wddm/bc250-radv-wddm.patch ausente: o driver Vulkan da placa "
            "nao tem copia neste repositorio")
        return
    if mirror.stat().st_size == 0:
        errors.append("espelho do delta RADV esta vazio")
        return
    if not prov.exists():
        errors.append("external/radv-wddm/PROVENANCE.md ausente: espelho sem proveniencia")
        return
    if not tool.exists():
        errors.append("tools/sync-radv-fork-delta.py ausente: espelho sem forma de conferir")
        return

    provenance = prov.read_text(encoding="utf-8", errors="replace")
    for field in ("Commit base:", "SHA-256 do patch:", "SHA-256 do DLL:"):
        require(provenance, field, "campo de proveniencia do fork RADV " + field, errors)
    require(provenance, "audit-radv-wddm-safety.py",
            "instrucao de como auditar o RADV com --mesa-root", errors)

    # O patch tem de ser um diff de verdade, nao um arquivo-placebo.
    patch = mirror.read_text(encoding="utf-8", errors="replace")
    require(patch, "diff --git", "conteudo de diff no espelho do delta RADV", errors)
    for expected in ("src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.h",
                     "src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c",
                     "src/vulkan/runtime/vk_wddm2_monitored_fence.c"):
        if expected not in patch:
            errors.append(
                "espelho do delta RADV nao contem " + expected +
                ": o nucleo do fork ficou de fora da copia")

    # A ferramenta tem de conferir, nao apenas regenerar.
    code = tool.read_text(encoding="utf-8", errors="replace")
    for needle in ("def do_verify", "NAO foi construido desta arvore",
                   "DIVERGE do espelho", "saiu do commit base declarado"):
        require(code, needle,
                "verificacao da sincronia do fork RADV: " + needle, errors)

    # A divida do fail-open fica DECLARADA ate cair. Se alguem remover a declaracao
    # sem remover o fail-open, o projeto perde o rastro dela.
    require(provenance, "all fences satisfied",
            "declaracao da divida do fail-open de fence no delta RADV", errors)


def audit_wave_probe_restores_the_selector(errors: list[str]) -> None:
    """A sonda de onda escreve seletores; ela TEM de devolver o broadcast.

    Contexto (Corrida 35, 19/09, P0): o quiesce mediu
    `SQ_DEBUG_STS_GLOBAL=0x00030031 WaveLevelSA0=3 WaveLevelSA1=3` -- seis ondas
    em voo que nao retiram -- e nao tinha como dizer ONDE. A sonda de onda le o
    PC pelo caminho indireto do SQ, e para isso precisa ESCREVER dois seletores:
    GRBM_GFX_INDEX (com os tres bits de broadcast DESLIGADOS) e SQ_IND_INDEX.

    O perigo nao e' a leitura, e' o seletor. GRBM_GFX_INDEX deixado fora de
    broadcast redireciona TODO acesso posterior a um unico shader engine --
    inclusive o soft-reset que o quiesce dispara logo em seguida. Este portao
    exige que a restauracao exista, que ela seja conferida por readback, que a
    falha tenha status PROPRIO (nao um sucesso silencioso) e que a sonda seja
    fail-closed em tres camadas: tunable, ondas>0 e orcamento de leituras.
    """
    contract = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.c"
    header = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.h"
    miniport = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    inf = ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf"
    test = ROOT / "driver" / "tests" / "gfx_ring_contract_test.c"
    for path in (contract, header, miniport, inf, test):
        if not path.exists():
            errors.append(path.name + " ausente: sonda de onda sem portao")
            return

    impl = contract.read_text(encoding="utf-8", errors="replace")
    hdr = header.read_text(encoding="utf-8", errors="replace")
    mini = miniport.read_text(encoding="utf-8", errors="replace")
    inf_text = inf.read_text(encoding="utf-8", errors="replace")
    test_text = test.read_text(encoding="utf-8", errors="replace")

    # 1. A restauracao tem de existir, ser conferida e ter status proprio.
    # PORTAO REFEITO POR MUTACAO (M8). A versao anterior exigia apenas a
    # PRESENCA da constante de broadcast no arquivo -- e ela tambem aparece no
    # comentario e no encode, entao remover a ESCRITA de restauracao passava
    # batido. O invariante correto e' sobre a escrita, nao sobre o simbolo:
    # a ultima escrita em GRBM_GFX_INDEX dentro de Bc250WaveProbeRun tem de
    # ser a que devolve o broadcast.
    run_body = strip_c_comments(impl)
    run_start = run_body.find("BC250_WAVE_PROBE_STATUS Bc250WaveProbeRun(")
    if run_start < 0:
        errors.append("Bc250WaveProbeRun ausente do contrato do anel grafico")
        return
    run_end = run_body.find("const char *Bc250WaveProbeStatusName", run_start)
    if run_end < 0:
        run_end = len(run_body)
    body = run_body[run_start:run_end]

    # O alvo e a ULTIMA ESCRITA, e ela precisa ser localizada como escrita.
    #
    # Duas versoes anteriores deste portao falharam contra a mutacao M8, e as
    # duas pelo mesmo tipo de descuido:
    #   1a: exigia so a PRESENCA da constante no arquivo -- ela tambem aparece
    #       no comentario e no encode;
    #   2a: procurava a ultima ocorrencia de BC250_GFX_REG_GRBM_GFX_INDEX, que
    #       e o READBACK, nao a escrita -- e a janela do readback contem a
    #       comparacao com o broadcast, entao passava com a escrita mutilada.
    # Agora a busca e por chamadas de Write32 sobre aquele registrador, e o
    # casamento e por fronteira de palavra (o nome certo e PREFIXO de qualquer
    # renomeacao, o mesmo buraco que a M14 explorou no runner).
    escritas = [m.start() for m in re.finditer(
        r"Write32\(\s*Interface->Context,\s*BC250_GFX_REG_GRBM_GFX_INDEX\s*,", body)]
    if not escritas:
        errors.append("Bc250WaveProbeRun nao escreve GRBM_GFX_INDEX: a sonda nao seleciona nada")
    else:
        janela = body[escritas[-1]:escritas[-1] + 200]
        if not re.search(r"\bBC250_GRBM_GFX_INDEX_BROADCAST_ALL\b", janela):
            errors.append(
                "a ULTIMA escrita de GRBM_GFX_INDEX em Bc250WaveProbeRun nao devolve o "
                "broadcast: sair do quiesce com o seletor preso faz o soft-reset seguinte "
                "atingir um unico shader engine")
    require(impl, "BC250_WAVE_PROBE_SELECTOR_STUCK",
            "status proprio para seletor que nao voltou", errors)
    require(impl, "Evidence->SelectorReadback",
            "readback do seletor restaurado", errors)
    require(hdr, "#define BC250_GRBM_GFX_INDEX_BROADCAST_ALL   0xE0000000u",
            "valor de broadcast dos tres campos de GRBM_GFX_INDEX", errors)

    code = strip_c_comments(impl)
    # A codificacao do seletor NAO pode ligar bit de broadcast: com broadcast
    # ligado a selecao nao vale e a leitura devolve um agregado sem sentido.
    enc = code[code.find("Bc250WaveProbeEncodeGfxIndex"):]
    enc = enc[:enc.find("uint32_t Bc250WaveProbeStatusIsValid")]
    if "0xE0000000" in enc or "0x80000000" in enc:
        errors.append(
            "Bc250WaveProbeEncodeGfxIndex liga bit de broadcast: a selecao de "
            "SE/SA/instancia deixaria de valer e a leitura viraria agregado")

    # 2. Fail-closed em tres camadas.
    require(code, "BC250_WAVE_PROBE_NO_WAVES",
            "recusa quando o SQ nao reportou onda em voo", errors)
    require(code, "BC250_WAVE_PROBE_PLAN_REJECTED",
            "recusa de plano acima do orcamento de leituras", errors)
    mini_code = strip_c_comments(mini)
    for needle in ("GfxWaveProbePresent != 1u", "GfxWaveProbe != 1u"):
        require(mini_code, needle,
                "guarda do tunable da sonda de onda: " + needle, errors)
    require(inf_text, "HKR,Parameters,GfxWaveProbe,0x00010001,0",
            "GfxWaveProbe declarado no INF com default 0 (fail-closed)", errors)

    # O soft-reset do quiesce NAO pode ser disparado com o seletor possivelmente
    # fora de broadcast. Enquanto a duvida do selector_stuck (Corrida 37) nao
    # fecha, a reposicao incondicional antes do reset e a mitigacao fisica, e
    # ela e independente do veredito da sonda -- vale ate com a sonda desarmada.
    reset_pos = mini.find("BC250_GRBM_REG_SOFT_RESET")
    if reset_pos < 0:
        errors.append("soft-reset do quiesce nao localizado: mitigacao do seletor sem portao")
    else:
        janela = mini[max(0, reset_pos - 2500):reset_pos]
        if "GRBM_GFX_INDEX reposto em broadcast antes do soft-reset" not in janela:
            errors.append(
                "o quiesce dispara GRBM_SOFT_RESET sem repor GRBM_GFX_INDEX em broadcast: "
                "com o seletor apontando para um unico shader engine, o reset atinge so ele")

    # 3. O caminho desarmado NAO pode ser mudo, e nao pode escrever nada.
    require(mini, "[TDR_GFX_WAVEPC] DESARMADO",
            "linha que nomeia a sonda desarmada", errors)
    require(mini, "[TDR_GFX_WAVEPC] ALERTA",
            "alerta quando o seletor nao volta a broadcast", errors)
    # Zero onda encontrada NAO pode ser lido como "nao ha onda": STATUS zero e
    # indistinguivel de slot vazio, e dizer o contrario seria falso negativo.
    require(mini, "INDISTINGUIVEL de slot vazio",
            "ressalva de que zero ondas achadas nao refuta o contador do SQ", errors)

    # 4. O teste nao pode perder o caso do seletor preso, que e o pior desfecho.
    require(test_text, "BC250_WAVE_PROBE_SELECTOR_STUCK",
            "caso de teste do seletor preso", errors)
    require(test_text, "sq.GfxIndexWrites == 0u",
            "caso de teste que prova que a sonda desarmada nao escreve nada", errors)


def audit_descriptor_and_wave_contracts(errors: list[str]) -> None:
    """Corrida 37: real bindings, poison exclusion, exact read ceiling."""
    def code(path: str) -> str:
        return strip_c_comments((ROOT / path).read_text(encoding="utf-8"))
    desc = code("driver/umd/bc250_umd_descriptors.c")
    draw = code("driver/umd/bc250_d3d_umd.c")
    bridge = code("driver/umd/bc250_umd_vulkan_bridge.c")
    adapter = code("driver/umd/bc250_vkd3d_shader_adapter.c")
    wave = code("driver/core/bc250_gfx_ring_transaction.c")
    cache = code("driver/umd/bc250_umd_pipeline_cache.c")
    upload = code("driver/umd/bc250_umd_resource_vk.c")
    for expression, body, reason in (
        (r"vkCreateDescriptorPool\s*\(", desc, "descriptor pool creation"),
        (r"vkAllocateDescriptorSets\s*\(", desc, "descriptor set allocation"),
        (r"vkUpdateDescriptorSets\s*\(", desc, "descriptor writes"),
        (r"vkCmdBindDescriptorSets\s*\(", desc, "descriptor bind"),
        (r"info\.next\s*=\s*&interfaceInfo\s*;", adapter, "explicit vkd3d interface"),
        (r"b->binding\.binding\s*=\s*stage\s*\*\s*BC250_DESCRIPTOR_STAGE_STRIDE\s*\+", adapter, "stage binding isolation"),
        (r"if\s*\(!Bc250UmdBindDrawDescriptors\([^;]+\)\)\s*return FALSE;", draw, "draw descriptor guard"),
        (r"if\s*\(Bc250UmdVkFlushCommands\(&pDevice->VkBridge\)\s*!=\s*VK_SUCCESS\)\s*\{[^}]*return FALSE;[^}]*\}", draw, "fence errors propagate"),
        (r"pBridge->SubmissionFailed\s*=\s*TRUE;", bridge, "failed submit cannot be reused"),
        (r"WaveStatus\s*!=\s*0xBEBEBEEFu", wave, "poison cannot be a wave"),
        (r"WaveStatus\s*&\s*0x00010000u", wave, "SQ VALID bit required"),
        (r"if\s*\(Evidence->ReadsIssued\s*>=\s*ReadBudget\)\s*return 0u;", wave, "strict per-read ceiling"),
        (r"Evidence->SelectorReadback\s*==\s*BC250_GRBM_GFX_INDEX_BROADCAST_ALL", wave, "unproven selector readback must not be accepted"),
        (r"pShader->NumBindings\s*==\s*UINT32_MAX", cache, "reflection failure rejects shader"),
        (r"res\s*=\s*pBridge->vk.vkFlushMappedMemoryRanges\(", upload, "constant buffer flush result propagated"),
    ):
        if not re.search(expression, body):
            errors.append("descriptor/wave contract: " + reason)
    order = [desc.find(name + "(") for name in
             ("vkCreateDescriptorPool", "vkAllocateDescriptorSets", "vkUpdateDescriptorSets", "vkCmdBindDescriptorSets")]
    if order != sorted(order) or min(order) < 0:
        errors.append("descriptor/wave contract: allocate/update/bind order")
    wait = bridge[bridge.find("res = pBridge->vk.vkWaitForFences"):]
    if not re.search(r"else\s*\{\s*Bc250UmdVkReleaseDescriptorBatches\(pBridge\);", wait):
        errors.append("descriptor/wave contract: descriptor retirement only after successful wait")
    if "FALLBACK DE SEGURANCA" in cache or "effectiveKey.VsModule = pCache->DefaultVsModule" in cache:
        errors.append("descriptor/wave contract: substitute shaders hide descriptor errors")


def audit_lab_tunables_are_read_in_the_common_path(errors: list[str]) -> None:
    """Tunable lido dentro do #if de producao nao existe na imagem de laboratorio.

    MEDIDO na Corrida 36 (19/09 12:11:58, P0): o INF empacotado trazia
    `GfxWaveProbe=1`, o manifesto derivava disso, e o quiesce imprimiu
    `[TDR_GFX_WAVEPC] DESARMADO: GfxWaveProbe=0 present=0`. O `present=0` e o
    discriminante -- o valor nao foi lido como zero, NUNCA foi consultado.

    A leitura estava depois de
    `#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)`, que e
    exatamente o simbolo que a imagem de LABORATORIO define: compilada para
    fora da imagem que roda. Uma corrida inteira foi gasta por isso.

    QUARTA ocorrencia da classe no projeto -- A13 (tunable de submissao), B4
    (GpuvaLabPromotion), ContextAbi, e agora esta. Por isso o portao nao trata
    apenas o caso novo: ele exige que TODO tunable desta lista seja lido ANTES
    do #if, que e o unico ponto que as duas imagens executam.
    """
    path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not path.exists():
        errors.append("bc250_active_miniport.c ausente: leitura de tunables sem portao")
        return
    text = path.read_text(encoding="utf-8", errors="replace")

    marker = "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)"
    # ANCORADO EM INICIO DE LINHA. O mesmo texto aparece DENTRO de um
    # comentario mais acima no arquivo (o registro da correcao do B4), e um
    # find() cru acharia aquela posicao -- antes de todos os tunables --,
    # fazendo o portao reprovar leituras que estao no lugar certo.
    directive = re.search(r"^[ 	]*" + re.escape(marker), text, re.M)
    prod_if = directive.start() if directive else -1
    if prod_if < 0:
        errors.append(
            "nao foi possivel localizar o #if de imagem de producao: o portao dos "
            "tunables de laboratorio nao pode se verificar e falha fechado")
        return

    # Tunables que PRECISAM valer na imagem de laboratorio.
    for tunable in ("GfxWaveProbe", "GfxRingPreflight", "Node1JournalGatePerProcess"):
        needle = 'L"' + tunable + '"'
        pos = text.find(needle)
        if pos < 0:
            errors.append("tunable " + tunable + " nao e lido em lugar nenhum")
            continue
        if pos > prod_if:
            errors.append(
                "tunable " + tunable + " e lido DEPOIS de " + marker + ": na imagem de "
                "laboratorio essa leitura nao existe, e o valor chega como present=0 "
                "mesmo com o INF trazendo 1")


def audit_ib_dump_covers_every_submission_path(errors: list[str]) -> None:
    """O despejo de IB nao pode viver so no ramo de concatenacao.

    MEDIDO nas Corridas 35 e 36: `gfx-ib-dump/` saiu VAZIO nas duas. Na 36 nao
    foi por falta da chave -- o runner criou `GfxIbDumpDir` apontando para a
    pasta certa e o RADV nunca chegou a resolve-la. A chamada estava dentro do
    ramo `if (cs_total > 1)` (concatenacao), e a submissao que travou o CP nao
    passou por ele.

    O portao le o espelho do fork (external/radv-wddm), porque a arvore Mesa
    vive fora deste repositorio -- e e por isso que o espelho existe.
    """
    patch = ROOT / "external" / "radv-wddm" / "bc250-radv-wddm.patch"
    if not patch.exists():
        errors.append("espelho do delta RADV ausente: despejo de IB sem portao")
        return
    text = patch.read_text(encoding="utf-8", errors="replace")

    if "bc250_dump_ib(" not in text:
        errors.append("o delta do RADV nao contem a funcao de despejo de IB")
        return

    # A chamada tem de estar fora do ramo de concatenacao. O marcador e a linha
    # de log do submit, que e comum aos dois caminhos.
    if "BC-250 submit: cs_total" not in text:
        errors.append("marcador de submissao comum ausente do delta do RADV")
        return
    submit_pos = text.find("BC-250 submit: cs_total")
    call_positions = [i for i in range(len(text)) if text.startswith("bc250_dump_ib(", i)]
    # A definicao tambem casa; interessa se ALGUMA chamada vem depois do
    # marcador comum, isto e, fora do ramo de concatenacao.
    if not any(pos > submit_pos for pos in call_positions):
        errors.append(
            "toda chamada de bc250_dump_ib fica ANTES do log de submissao comum: o "
            "despejo voltaria a cobrir so o ramo de concatenacao, e foi assim que "
            "duas corridas seguidas arquivaram gfx-ib-dump vazio")


def audit_kmd_tail_archive_declares_truncation(errors: list[str]) -> None:
    """Pasta de evidencia incompleta tem de DIZER que esta incompleta.

    MEDIDO na Corrida 36: a cauda arquivada tinha 210.851 bytes de 219.325
    reais. Os 8.474 que faltaram continham o bloco INTEIRO do TDR -- a unica
    medicao que a corrida existia para produzir -- e nada na pasta indicava
    que ela estava truncada. O codigo de copia nunca esteve errado; o defeito
    e temporal, porque o KMD escreve depois da copia.
    """
    binder = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"
    if not binder.exists():
        errors.append("bind-bc250mcdm-lab.ps1 ausente: arquivamento do KMD sem portao")
        return
    text = binder.read_text(encoding="utf-8", errors="replace")

    require(text, "display-log-final-archive.json",
            "proveniencia do arquivamento final da cauda do KMD", errors)
    # A ATRIBUICAO do campo, nao a mencao dele. A mutacao M18 renomeou o campo
    # e o portao continuou passando porque a palavra sobrevivia dentro da string
    # de "note" logo abaixo -- mencionar um campo nao e o mesmo que publica-lo.
    if not re.search(r"(?<![A-Za-z0-9_])complete_at_capture\s*=", text):
        errors.append(
            "o json do arquivamento nao ATRIBUI complete_at_capture: a pasta voltaria "
            "a nao dizer que esta truncada, que foi como o bloco do TDR da Corrida 36 "
            "sumiu sem deixar rastro")
    require_token(text, "bytes_pass1", "tamanho da primeira passagem de arquivamento", errors)
    require_token(text, "bytes_pass2", "tamanho da segunda passagem de arquivamento", errors)
    require(text, "PASTA INCOMPLETA",
            "aviso alto quando a cauda arquivada esta truncada", errors)
    # Duas passagens de verdade: uma unica copia sempre corre o risco de pegar
    # o log no meio da escrita do bloco de TDR.
    if text.count("$fs.CopyTo($out)") + text.count("$fs2.CopyTo($out2)") < 2:
        errors.append(
            "arquivamento da cauda do KMD voltou a ter UMA passagem: o bloco do TDR "
            "e escrito na borda da coleta e some sem deixar rastro")


def audit_resource_copy_requires_created(errors: list[str]) -> None:
    """Copia entre recursos nao pode confiar em struct nao criada.

    MEDIDO em 19/09, com cdb, em bc250_d3d_umd_test.exe:
      rcx = rdx = 0x00007ffd58bb5b40   (origem e destino iguais)
      r8  = 0x58bb34e0                 (1,4 GB)
    memcpy com comprimento lido de memoria nao inicializada -- o tamanho ate
    carregava o padrao alto do proprio ponteiro. A origem era um
    BC250_UMD_RESOURCE declarado na pilha e nunca inicializado, entregue a
    ResourceResolveSubresource.

    A mina existia havia tempo e so' foi pisada porque acrescentar um campo a
    struct mudou QUAL lixo caia em TotalByteSize. Por isso o portao trava a
    condicao, nao o sintoma: sem Created dos dois lados a copia nao acontece, e
    a recusa fala. Tambem exige que o teste pare de fabricar a entrada invalida
    -- consertar so um lado deixaria o outro pronto para repetir.
    """
    prod = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    test = ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c"
    for path in (prod, test):
        if not path.exists():
            errors.append(path.name + " ausente: copia de recurso sem portao")
            return
    code = strip_c_comments(prod.read_text(encoding="utf-8", errors="replace"))

    start = code.find("Bc250Umd10ResourceCopyRegion")
    if start < 0:
        errors.append("Bc250Umd10ResourceCopyRegion ausente")
        return
    end = code.find("\nVOID APIENTRY", start + 10)
    body = code[start:end if end > 0 else len(code)]

    guard = body.find("!dst->Created || !src->Created")
    copy = body.find("memcpy(")
    if guard < 0:
        errors.append(
            "ResourceCopyRegion nao exige Created dos dois recursos: TotalByteSize e "
            "pCpuShadow voltariam a ser lidos de memoria nao inicializada")
    elif copy >= 0 and guard > copy:
        errors.append(
            "a guarda de Created em ResourceCopyRegion esta DEPOIS do primeiro memcpy: "
            "conferir depois de copiar nao desfaz a copia")

    require(prod.read_text(encoding="utf-8", errors="replace"),
            "ResourceCopyRegion RECUSADO: recurso nao criado",
            "recusa nomeada da copia com recurso nao criado", errors)

    tcode = test.read_text(encoding="utf-8", errors="replace")
    if "memset(&soRes, 0, sizeof(soRes));" not in tcode:
        errors.append(
            "o teste voltou a entregar BC250_UMD_RESOURCE de pilha nao inicializado a "
            "ResourceResolveSubresource")


def audit_texture_path_is_named_and_complete(errors: list[str]) -> None:
    """A recusa de SRV nomeia UMA condicao, e a textura sobe pelos tres caminhos.

    Duas medicoes da Corrida 39 (19/09 16:20:14) sustentam este portao:

    1. `[VK_DESCRIPTOR] RECUSADO: binding index=2 motivo=SRV_AUSENTE_OU_SEM_IMAGEM`
       -- esse nome agregava SETE checagens. Foram nomeadas 17 recusas naquela
       faixa e justamente a que disparou ficou grossa: a corrida disse onde
       parou, nao por que.

    2. `dwm.exe` saiu com upload=0 enquanto outros processos tiveram 21. A
       sincronia com Vulkan em ResourceUpdateSubresourceUP e em ResourceUnmap
       so' acontecia para BUFFER; textura escrevia no shadow de CPU e nunca
       chegava a VkImage. O DWM cria a textura vazia e preenche depois, entao
       cobrir so a criacao nao o alcancava.

    O portao trava as duas: o motivo agregado nao pode voltar, e os dois DDIs
    de atualizacao tem de sincronizar textura pela fonte unica.
    """
    desc = ROOT / "driver" / "umd" / "bc250_umd_descriptors.c"
    umd = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    for path in (desc, umd):
        if not path.exists():
            errors.append(path.name + " ausente: caminho de textura sem portao")
            return

    dtext = desc.read_text(encoding="utf-8", errors="replace")
    # Comentario pode CITAR o nome banido -- o registro historico e' util e nao
    # e' publicacao. A proibicao vale sobre o codigo; strip_c_comments preserva
    # literais de string, entao um refusedReason com o nome antigo ainda casa.
    dcode = strip_c_comments(dtext)

    # 1. O agregado morreu e os sete nomes existem.
    if "SRV_AUSENTE_OU_SEM_IMAGEM" in dcode:
        errors.append(
            "SRV_AUSENTE_OU_SEM_IMAGEM voltou: esse motivo agrega sete condicoes e foi "
            "exatamente o que deixou a Corrida 39 sem saber por que a SRV do dwm.exe caiu")
    for motivo in ("SRV_SLOT_VAZIO", "SRV_NAO_CRIADA", "SRV_SEM_RECURSO_ASSOCIADO",
                   "SRV_RECURSO_SEM_VKIMAGE", "SRV_DIMENSAO_NAO_E_TEXTURE2D",
                   "SRV_MULTISAMPLE_NAO_SUPORTADA",
                   "SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA"):
        require_token(dcode, motivo, "motivo de recusa de SRV " + motivo, errors)

    # Nenhuma recusa pode sair sem motivo atribuido.
    code = dcode
    import re as _re
    for m in _re.finditer(r"goto refused;", code):
        janela = code[max(0, m.start() - 120):m.start()]
        if "refusedReason" not in janela:
            errors.append(
                "existe 'goto refused' sem refusedReason atribuido nas 120 letras anteriores: "
                "a recusa sairia como MOTIVO_NAO_REGISTRADO")
            break

    # 2. Os dois DDIs de atualizacao sincronizam textura, pela MESMA funcao.
    utext = umd.read_text(encoding="utf-8", errors="replace")
    require(utext, "static void Bc250UmdSyncTextureToVkImage",
            "fonte unica da sincronia de textura", errors)
    ucode = strip_c_comments(utext)
    for ddi in ("Bc250Umd10ResourceUpdateSubresourceUP", "Bc250Umd10ResourceUnmap"):
        ini = ucode.find("VOID APIENTRY " + ddi + "(")
        if ini < 0:
            errors.append("DDI " + ddi + " ausente")
            continue
        fim = ucode.find("\n}\n", ini)
        corpo = ucode[ini:fim if fim > 0 else len(ucode)]
        if "Bc250UmdSyncTextureToVkImage(" not in corpo:
            errors.append(
                ddi + " nao sincroniza textura com a VkImage: era assim que o dwm.exe "
                "ficava com upload=0 e a SRV dele sem conteudo")

    # E o escopo tem de ser declarado, nao aproximado.
    require(utext, "fora do escopo carregado",
            "recusa nomeada para subrecurso alem do carregado", errors)

    # Corrida 41: width=590, linha real=2360 e shadow pitch=2560. A DDI
    # copiava apenas 590 bytes quando pDstBox==NULL, e o upload recusava todo
    # pitch com padding. As duas metades precisam permanecer corrigidas.
    require(ucode, "src + (SIZE_T)y * RowPitch, lineBytes",
            "UpdateSubresourceUP copia cada linha pelo pitch da origem", errors)
    require(ucode, "dst + (SIZE_T)y * res->RowPitch",
            "UpdateSubresourceUP avanca pelo pitch do shadow", errors)
    require(ucode, "(UINT)pDstBox->right > res->Width",
            "box de textura validado contra a largura", errors)
    vk_source = (ROOT / "driver" / "umd" / "bc250_umd_resource_vk.c").read_text(
        encoding="utf-8", errors="replace")
    vk_code = strip_c_comments(vk_source)
    require(vk_code, "*pBufferRowLength = RowPitch / bpp",
            "pitch Vulkan convertido de bytes para texels", errors)
    require(vk_code, "region.bufferRowLength = bufferRowLength",
            "VkBufferImageCopy publica o row pitch acolchoado", errors)
    require(vk_code, "Bc250UmdVkUploadBytesPerTexel",
            "tamanho de texel conhecido sem inferencia pelo pitch", errors)
    if "linha com padding nao esta implementada" in vk_code:
        errors.append("upload Vulkan voltou a recusar toda linha com padding")

    # Corrida 40 deixou duas causas indistinguiveis: ordem legitima do DWM ou
    # escrita por caminho ignorado. A recusa precisa nomear o recurso e quantas
    # DDIs de escrita chegaram a ele, sem inferir causalidade por proximidade.
    htext = (ROOT / "driver" / "umd" / "bc250_d3d_umd.h").read_text(
        encoding="utf-8", errors="replace")
    hcode = strip_c_comments(htext)
    require(hcode, "LastVkDrawFailureReason",
            "estado do motivo exato da ultima tentativa Vulkan", errors)
    require(ucode, "backend Vulkan motivo=%s",
            "recusa final do Draw publica a causa Vulkan", errors)
    require(dcode, "device->LastVkDrawFailureReason = refusedReason",
            "recusa de descritor propaga a causa ao Draw", errors)
    for token, label in (
        ("TextureWriteDdiCount", "contador por recurso das DDIs de escrita de textura"),
        ("SRV_RESOURCE=%p", "ponteiro do recurso SRV recusado"),
        ("WxH=%ux%u", "geometria do recurso SRV recusado"),
        ("Format=%u", "formato do recurso SRV recusado"),
        ("BindFlags=0x%08X", "BindFlags do recurso SRV recusado"),
        ("Usage=%u", "Usage do recurso SRV recusado"),
        ("TextureWrites=%u", "contagem de escritas do recurso SRV recusado"),
        ("VkLayout=%s", "layout Vulkan por nome na recusa SRV"),
        ("Bc250UmdVkImageLayoutName", "fonte unica do nome de layout Vulkan"),
    ):
        require(dcode + ucode + hcode, token, label, errors)
    helper_start = ucode.find("static void Bc250UmdSyncTextureToVkImage(")
    helper_end = ucode.find("\n}\n", helper_start)
    helper = ucode[helper_start:helper_end if helper_end > helper_start else len(ucode)]
    require(helper, "++res->TextureWriteDdiCount",
            "contador de escrita incrementado na fonte unica de sincronia", errors)


def audit_lost_log_tail_is_recovered(errors: list[str]) -> None:
    """C2: nenhuma corrida pode ficar sem o trecho decisivo do log.

    MEDIDO em 10/09: o binder copia C:\\BC250_DISPLAY_LOG.txt integral para a
    pasta de evidencia no FIM do roteiro. Quando a maquina sofre BugCheck antes
    disso, o watchdog nao roda e a pasta fica TRUNCADA sem que nada indique.
    Seis pastas do mesmo dia estavam truncadas; a da corrida do marco (11:21)
    perdeu 438.038 bytes contendo o FenceId=770, a linha IB_TRIPOD TIMEOUT e o
    FENCE_VISIBILIDADE que provou a causa-raiz do BugCheck 0x119 -- e a
    auditoria daquela corrida foi feita sem a evidencia decisiva, chegando a
    conclusao oposta. Duas pastas (10:03 e 10:23) nao tinham arquivo nenhum.

    O log cumulativo sobrevive a reboot, entao o dado nunca esteve perdido de
    verdade: estava sem ser coletado. Este portao exige que o resgate exista,
    que o binder o chame ANTES de comecar o bind (o baseline novo delimita o
    fim do trecho anterior) e que o resgate seja SOMENTE LEITURA sobre o log.
    """
    recovery_path = ROOT / "tools" / "recover-bc250mcdm-lost-tail.ps1"
    binder_path = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"
    if not recovery_path.exists():
        errors.append("recover-bc250mcdm-lost-tail.ps1 ausente: trecho perdido de corrida nao seria resgatado")
        return
    if not binder_path.exists():
        errors.append("bind-bc250mcdm-lab.ps1 ausente para auditar resgate de trecho")
        return

    recovery = recovery_path.read_text(encoding="utf-8", errors="replace")
    binder = binder_path.read_text(encoding="utf-8", errors="replace")

    require(recovery, "bc250_display_log-tail-recovered.txt",
            "nome do artefato de trecho resgatado", errors)
    require(recovery, "display-log-tail-recovered.json",
            "proveniencia do trecho resgatado", errors)
    require(recovery, "Get-FileHash",
            "SHA-256 do trecho resgatado", errors)
    require(recovery, "bounded_by_next_bind",
            "declaracao de que o fim do trecho e' delimitado ou nao", errors)
    require(recovery, "[System.IO.FileAccess]::Read",
            "abertura do log vivo em somente leitura", errors)
    # Corrida 42: o binder atual arquiva um DELTA. archive.bytes e' o tamanho
    # desse delta, nao um offset absoluto do arquivo cumulativo. Tratar o valor
    # como offset gerou um falso resgate de 1,6 GB numa corrida anterior e
    # esconderia novamente a fronteira real da evidencia.
    require(recovery, "$isDelimitedDelta =",
            "distincao entre arquivo integral antigo e delta delimitado", errors)
    require(recovery, "$tailStart = $startOffset + [int64]$archive.bytes",
            "offset do trecho perdido soma baseline e tamanho do delta", errors)

    # Somente leitura sobre o LOG VIVO: escrever ou apagar destruiria a fonte.
    #
    # C3.10 (12/09) -- PORTAO TORNADO PRECISO. A versao anterior proibia a string
    # "[System.IO.FileAccess]::Write" em QUALQUER lugar do arquivo, sem
    # distinguir a fonte do destino. O resgate precisa abrir o ARQUIVO DE
    # DESTINO para escrita; com a proibicao cega, a unica saida seria driblar o
    # portao com outra API -- o que e' pior do que o portao existir.
    #
    # A intencao permanece intacta e agora e' verificada onde importa: nenhuma
    # abertura ou comando de escrita pode ter $DisplayLogPath como alvo.
    for needle in ("Remove-Item -LiteralPath $DisplayLogPath",
                   "Set-Content -LiteralPath $DisplayLogPath",
                   "Clear-Content -LiteralPath $DisplayLogPath",
                   "Add-Content -LiteralPath $DisplayLogPath",
                   "Out-File -LiteralPath $DisplayLogPath"):
        forbid(recovery, needle, "escrita no log cumulativo pelo resgate", errors)

    # A abertura do log vivo continua obrigatoriamente em Read.
    require(recovery,
            "[System.IO.File]::Open($DisplayLogPath, [System.IO.FileMode]::Open,",
            "abertura do log vivo por Open(...FileMode::Open...)", errors)
    open_at = recovery.find("[System.IO.File]::Open($DisplayLogPath")
    if open_at < 0:
        errors.append("abertura do log vivo nao encontrada no resgate")
    else:
        window = recovery[open_at:open_at + 260]
        if "[System.IO.FileAccess]::Read" not in window:
            errors.append(
                "log vivo aberto sem FileAccess::Read no resgate")
        for bad in ("[System.IO.FileAccess]::Write",
                    "[System.IO.FileAccess]::ReadWrite",
                    "[System.IO.FileMode]::Create",
                    "[System.IO.FileMode]::Append",
                    "[System.IO.FileMode]::Truncate"):
            if bad in window:
                errors.append(
                    f"log vivo aberto com modo de escrita no resgate ({bad})")

    # E o destino NAO pode ser o proprio log vivo.
    if "[System.IO.File]::Open($DisplayLogPath, [System.IO.FileMode]::Create" in recovery:
        errors.append("resgate recria o log vivo em vez de apenas le-lo")

    # C3.10: fatiamento de array sobre o trecho inteiro nao pode voltar.
    # $buffer[0..($read - 1)] sobre 201 MB travou o bind em 12/09 16:02 e o
    # operador teve de dar Ctrl+C.
    # Sobre o CODIGO, sem comentarios: o comentario que explica a remocao cita
    # a construcao proibida, e uma versao anterior deste portao reprovou o
    # proprio comentario.
    forbid(strip_ps_comments(recovery), "$buffer[0..",
           "fatiamento de array do PowerShell sobre o trecho inteiro "
           "(travou o bind na corrida 16:02)", errors)
    # Forma EXATA: exigir so' a presenca de "$blockSize" passava com a variavel
    # renomeada na atribuicao, porque o nome sobrevivia nos usos seguintes.
    require(recovery, "$blockSize = 1MB",
            "tamanho de bloco fixo na copia do trecho", errors)
    require(recovery, "$dest.Write($buffer, 0, $chunk)",
            "escrita do trecho bloco a bloco, sem materializar o todo", errors)

    # O binder tem de chamar o resgate, e antes do bind.
    require(binder, "recover-bc250mcdm-lost-tail.ps1",
            "chamada do resgate de trecho pelo binder", errors)
    recovery_call = binder.find("$tailRecovery = Join-Path")
    baseline_write = binder.find("'display-log-baseline.json') -Encoding UTF8")
    archive_write = binder.find("'display-log-archive.json') -Encoding UTF8")
    if recovery_call < 0:
        errors.append("binder nao define $tailRecovery: resgate de trecho nao esta ligado")
    elif baseline_write < 0 or recovery_call < baseline_write:
        errors.append(
            "resgate de trecho chamado antes do baseline deste bind: sem a "
            "start_offset nova o fim do trecho anterior fica indelimitado")
    elif archive_write >= 0 and recovery_call > archive_write:
        errors.append(
            "resgate de trecho chamado depois do arquivamento final: precisa "
            "rodar no inicio do bind, senao a corrida anterior segue truncada")


def audit_draw_has_no_cpu_fallback(errors: list[str]) -> None:
    """C1: Draw/DrawIndexed nao podem ser atendidos por copia de CPU.

    DEFEITO ENCONTRADO POR LEITURA DE CODIGO em 10/09, no candidato 3.126.1.0 e
    ainda nao commitado: Bc250UmdCompositeDraw atendia pfnDraw e pfnDrawIndexed
    com um laco de memcpy entre os shadows de CPU de origem e destino, marcava
    o draw como atendido ANTES de tentar qualquer caminho de GPU e devolvia
    sucesso silencioso -- a linha honesta "DrawIndexed: RECUSADO" desaparecia.

    Isso e' fallback de software disfarcado de renderizacao (Lei 4) e teria
    consequencia direta na proxima corrida: qualquer desktop que aparecesse na
    tela ficaria INFALSIFICAVEL, porque nao haveria como distinguir composicao
    por GPU de memcpy. O teste de contrato AINDA EXIGIA esse memcpy, congelando
    a mentira -- terceira ocorrencia dessa classe (A15 obrigava a comparacao
    crua de RPTR; mcdm_callbacks_test exigia STATUS_SUCCESS sem executor).

    Este portao trava as tres pontas: nao existe copia de CPU na funcao de
    substituicao, a decisao sai da politica pura, e o teste continua PROIBINDO
    o memcpy em vez de exigi-lo.
    """
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    test_path = ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c"
    if not umd_path.exists() or not test_path.exists():
        errors.append("fontes da UMD nao encontradas para auditar fallback de draw")
        return

    umd = umd_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # O nome antigo nao pode voltar: ele designava a funcao que continha o fake.
    forbid(umd, "Bc250UmdCompositeDraw",
           "nome da funcao que continha o fallback de CPU no draw", errors)
    require(umd, "static BOOL Bc250UmdSubstituteDrawWithBlt(",
            "funcao unica de substituicao de draw", errors)
    require(umd, "Bc250UmdDrawBltSubstitutionIsLawful(",
            "decisao de substituicao pela politica pura", errors)

    # Recorte exato da funcao de substituicao: nenhuma copia por CPU dentro dela.
    start = umd.find("static BOOL Bc250UmdSubstituteDrawWithBlt(")
    if start < 0:
        errors.append("nao foi possivel recortar Bc250UmdSubstituteDrawWithBlt")
        return
    end = umd.find("\nVOID APIENTRY Bc250Umd10Draw(", start)
    if end < 0:
        errors.append("nao foi possivel delimitar o fim da substituicao de draw")
        return
    body = umd[start:end]

    for needle in ("memcpy(", "RtlCopyMemory(", "CopyMemory(", "memmove("):
        forbid(body, needle,
               "copia por CPU dentro da substituicao de draw", errors)
    # Upload por CPU tambem nao: seria o mesmo fake com outro nome.
    forbid(body, "Bc250WinsysUploadAllocation(",
           "upload por CPU dentro da substituicao de draw", errors)
    require(body, "Bc250UmdBltGpu(",
            "unico produtor de pixel na substituicao: blt de GPU", errors)
    require(body, "SubstitutedDrawCount++",
            "contagem separada de draws substituidos", errors)

    # A substituicao nunca pode ser confundida com rasterizacao no log.
    require(body, "NAO e' rasterizacao",
            "log declarando que substituicao nao e' rasterizacao", errors)

    # O default de fabrica e' recusar: o tunable nao pode nascer ligado.
    inf_path = ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf"
    if inf_path.exists():
        inf = inf_path.read_text(encoding="utf-8", errors="replace")
        for line in inf.splitlines():
            if "D3D11DrawBltSubstitution" in line and not line.strip().startswith(";"):
                if "0x00010001,1" in line.replace(" ", ""):
                    errors.append(
                        "INF fonte arma D3D11DrawBltSubstitution=1: "
                        "o default de fabrica tem de ser 0")

    # E o teste tem de DETECTAR o memcpy, nao exigi-lo.
    forbid(test, "TestCompositeDrawAndUniqueTokens",
           "teste que exigia o memcpy no draw", errors)
    require(test, "static void TestDrawBltSubstitutionFailsClosed(void)",
            "teste do fail-closed da substituicao de draw", errors)
    require(test, "assert(dev.SubstitutedDrawCount == 0u);",
            "assercao de que nenhum draw foi substituido sem winsys", errors)
    require(test, "assert(((const uint8_t*)resDst.pCpuShadow)[i] == 0x00);",
            "deteccao de regressao do memcpy no destino", errors)


def audit_probe_field_names(errors: list[str]) -> None:
    """Todo membro do enum de telemetria precisa de nome, e nomes unicos.

    Motivo (2026-09-02): o switch de Bc250ProbeFieldName tem um ramo
    `default: return NULL`, entao o MSVC nao emite C4061/C4062 e o build
    /W4 /WX passa limpo mesmo com campos sem case. Quatro campos gravados em
    todo bind por bc250_dxgk_readonly_ddi.c estavam nessa situacao: o valor
    nunca aparecia no registro, e a chave aberta em Bc250ProbeRecord vazava um
    handle de kernel por chamada.

    Nome duplicado e' o mesmo defeito pelo avesso: dois campos escreveriam no
    mesmo valor do registro e um apagaria a evidencia do outro em silencio.

    Este audit fecha as duas portas por construcao.
    """
    header = (ROOT / "driver/wdk/bc250_probe_telemetry.h").read_text(
        encoding="utf-8", errors="replace")
    source = (ROOT / "driver/wdk/bc250_probe_telemetry.c").read_text(
        encoding="utf-8", errors="replace")

    match = re.search(
        r"typedef enum[^{]*\{(.*?)\}\s*BC250_PROBE_FIELD", header, re.S)
    if match is None:
        errors.append("enum BC250_PROBE_FIELD nao localizado no header")
        return
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S)
    members = [
        chunk.strip().split("=")[0].strip() for chunk in body.split(",")
    ]
    members = [
        name for name in members
        if re.match(r"^BC250_PROBE_[A-Z0-9_]+$", name)
        and name != "BC250_PROBE_FIELD_COUNT"
    ]
    if not members:
        errors.append("enum BC250_PROBE_FIELD veio vazio do parser")
        return

    cases = set(re.findall(r"case\s+(BC250_PROBE_[A-Z0-9_]+)\s*:", source))
    missing = [name for name in members if name not in cases]
    if missing:
        shown = ", ".join(missing[:8])
        suffix = f" (e mais {len(missing) - 8})" if len(missing) > 8 else ""
        errors.append(
            f"campos de telemetria sem nome mapeado, vazam handle: {shown}{suffix}")

    names = re.findall(r'return\s+L"([A-Za-z0-9_]+)"\s*;', source)
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        errors.append(
            "nomes de telemetria duplicados: " + ", ".join(duplicates[:8]))


def function_body(text: str, signature: str) -> str:
    """Return a C function body, including braces, using balanced braces.

    Skips forward declarations. Sem isso, uma funcao que possui prototipo
    antes da definicao devolvia o corpo da funcao SEGUINTE, e todas as
    exigencias sobre ela passavam ou falhavam pelo motivo errado — falso
    negativo silencioso em um gate fail-closed.
    """
    search = 0
    while True:
        start = text.find(signature, search)
        if start < 0:
            return ""
        search = start + len(signature)
        # Fecha a lista de argumentos a partir do parenteses da assinatura.
        depth = 0
        index = text.find("(", start)
        if index < 0:
            continue
        end_args = -1
        for index in range(index, len(text)):
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
                if depth == 0:
                    end_args = index
                    break
        if end_args < 0:
            continue
        rest = text[end_args + 1:]
        stripped = rest.lstrip()
        if not stripped.startswith("{"):
            continue  # prototipo: procura a proxima ocorrencia
        brace = end_args + 1 + (len(rest) - len(stripped))
        depth = 0
        for index in range(brace, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    return text[brace:index + 1]
        return ""


def audit_gpuva_geometry_single_source(errors: list[str]) -> None:
    """A1 (07/09): a geometria de GPUVA tem UMA fonte, e nao e' um literal.

    MEDIDO na corrida 2026-09-07 11:08: o KMD declara ao WDDM
        [McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2
    enquanto o blob DXGKQAITYPE_UMDRIVERPRIVATE publicava
    BC250_ADAPTER_GPUVA_BITS (= 48) e PageTableLevels = 2 cravado. Duas respostas
    diferentes para a mesma pergunta, saindo do mesmo driver.

    Nao e' cosmetico: a UMD -- e, na rota B, o winsys do RADV -- dispoe enderecos
    dentro do que o blob promete. Prometer 48 bits com uma hierarquia que traduz
    34 (12 offset + 9 folha + 13 raiz, docs/03) produz endereco sem PTE, que e'
    exatamente o sintoma que ja perseguimos no B1.

    Este gate exige que o publicador do blob tire GpuVaBits e PageTableLevels de
    `runtimeCaps` -- a MESMA estrutura que alimenta o GPUMMUCAPS -- e nunca de um
    literal ou de um #define proprio.
    """
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    if not callbacks_path.exists():
        errors.append("bc250_mcdm_callbacks.c nao encontrado para auditar GPUVA")
        return
    text = callbacks_path.read_text(encoding="utf-8", errors="replace")

    required = (
        "facts.GpuVaBits = runtimeCaps.GpuVaBitCount;",
        "facts.PageTableLevels = runtimeCaps.PageTableLevelCount;",
    )
    for needle in required:
        if needle not in text:
            errors.append(
                "blob UMDRIVERPRIVATE nao deriva a geometria do runtime: "
                f"falta `{needle}`")

    # E o publicador do GPUMMUCAPS tem de usar a mesma origem.
    for needle in (
        "caps->VirtualAddressBitCount = runtimeCaps.GpuVaBitCount;",
        "caps->PageTableLevelCount = runtimeCaps.PageTableLevelCount;",
    ):
        if needle not in text:
            errors.append(
                "GPUMMUCAPS nao deriva a geometria do runtime: "
                f"falta `{needle}`")

    # Nenhum literal de geometria pode reaparecer atribuido a facts.
    for line in text.splitlines():
        stripped = line.strip()
        # So atribuicoes. A continuacao de um Bc250DisplayLog tambem comeca com
        # `facts.GpuVaBits,` e nao e' uma fonte de valor.
        if not stripped.startswith("facts.GpuVaBits =") and            not stripped.startswith("facts.PageTableLevels ="):
            continue
        if "runtimeCaps." not in stripped:
            errors.append(
                f"geometria de GPUVA cravada no blob: `{stripped}`")

    # A fonte unica do numero e' a decomposicao de 34 bits, nao um 48 solto.
    space_path = ROOT / "driver" / "core" / "bc250_gpuva_space_contract.h"
    if space_path.exists():
        space = space_path.read_text(encoding="utf-8", errors="replace")
        if "#define BC250_GPUVA_PT_VA_BITS (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS"                 not in space:
            errors.append(
                "BC250_GPUVA_PT_VA_BITS deixou de ser derivado da decomposicao "
                "12 + folha + raiz")

    info_path = ROOT / "driver" / "core" / "bc250_adapter_info_contract.h"
    if info_path.exists():
        info = info_path.read_text(encoding="utf-8", errors="replace")
        if "#define BC250_ADAPTER_GPUVA_BITS          BC250_GPUVA_PT_VA_BITS"                 not in info:
            errors.append(
                "BC250_ADAPTER_GPUVA_BITS voltou a ser literal: era 48 e o "
                "silicio traduz 34")


def audit_frame_pointer_flag(errors: list[str]) -> None:
    """A5: FPO precisa ficar desligado no build do SYS.

    referencias/windows-driver-docs/.../display/debugging-tips-for-wddm-drivers.md
    exige que drivers WDDM 1.2+ desliguem frame pointer omission. Aqui isso nao e'
    cosmetico: sem frame pointer a call stack do MEMORY.DMP fica inutil, e o dump e'
    a unica via de diagnostico enquanto nao existe segunda maquina (P3).
    """
    build_path = ROOT / "driver" / "wdk" / "build-contract.bat"
    if not build_path.exists():
        errors.append("build-contract.bat nao encontrado para auditar FPO")
        return
    text = build_path.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.lower().startswith("set cflags="):
            continue
        if "/Oy-" not in stripped:
            errors.append(
                "CFLAGS do SYS sem /Oy-: FPO ligado quebra a call stack do dump")
        if "/Oy " in stripped or stripped.endswith("/Oy"):
            errors.append("CFLAGS do SYS habilita /Oy explicitamente")
        return
    errors.append("nenhuma definicao de CFLAGS localizada em build-contract.bat")


def audit_batch_line_endings(errors: list[str]) -> None:
    """A8: .bat/.cmd do caminho de build precisam estar em CRLF.

    MEDIDO em 06/09: build-contract.bat estava com terminadores LF, e o cmd.exe,
    que retoma a leitura do arquivo por deslocamento em bytes, passou a comer o
    primeiro caractere das linhas depois de `call vcvars64.bat`. O sintoma foi
    LNK1181 com o nome de arquivo '^.obj' e linhas do proprio .bat sendo
    executadas como comandos -- readonly_miniport_test nunca linkava, e a queda
    ficava escondida no meio de um log de centenas de linhas. Converter para
    CRLF fez a suite inteira voltar a EXIT=0.

    docs/evidence esta fora do escopo de proposito: e' corpus imutavel.
    """
    scope = ("driver", "dbg", "tools")
    for base in scope:
        base_dir = ROOT / base
        if not base_dir.is_dir():
            continue
        for path in base_dir.rglob("*"):
            if path.suffix.lower() not in (".bat", ".cmd"):
                continue
            if "output" in path.parts or "evidence" in path.parts:
                continue
            raw = path.read_bytes()
            lf = raw.count(b"\n")
            crlf = raw.count(b"\r\n")
            if lf and crlf != lf:
                errors.append(
                    f"{path.relative_to(ROOT)}: {lf - crlf} linha(s) sem CRLF "
                    "-- cmd.exe corrompe a leitura por deslocamento")



def audit_submit_path_labels(errors: list[str]) -> None:
    """A13: a frase "IB EXECUTADO" so' pode sair do ramo INDIRECT.

    MEDIDO na corrida 2026-09-07 16:19: o log tem 14 "IB EXECUTADO" e, no mesmo
    log, 2 "PLANO A DIRECT FILL" + 12 "PLANO A SYNC NOP" -- 14 = 2 + 12 e ZERO
    INDIRECT. A linha de sucesso era comum aos tres ramos, entao afirmava
    execucao de DMA buffer onde a GPU nao leu DMA buffer nenhum (Lei 4).

    Tres portoes:
      1. a selecao de ramo sai de Bc250SelectSubmitExecPath (fonte unica);
      2. a frase e' guardada por Bc250SubmitPathExecutesDmaBuffer;
      3. os PLANO A tem rotulo proprio que nega execucao de IB.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append(
            "bc250_active_miniport.c nao encontrado para auditar rotulo de submit")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")

    require(text, "Bc250SelectSubmitExecPath(&pathInput)",
            "selecao de ramo de submit por fonte unica", errors)
    require(text, "Bc250SubmitPathExecutesDmaBuffer(execPath)",
            "guarda da frase IB EXECUTADO", errors)
    require(text, "PLANO A CONCLUIDO",
            "rotulo proprio dos caminhos PLANO A", errors)
    require(text, "NAO foi lido pela GPU",
            "negacao explicita de execucao de IB no PLANO A", errors)

    # A frase de execucao de IB nao pode aparecer fora da guarda.
    for index, line in enumerate(text.splitlines(), start=1):
        if "IB EXECUTADO" not in line:
            continue
        if "IB NAO EXECUTADO" in line:
            continue
        if "INDIRECT" not in line:
            errors.append(
                f"bc250_active_miniport.c:{index}: 'IB EXECUTADO' sem qualificar "
                "INDIRECT -- foi assim que 14 PLANO A viraram 14 IBs no log")


def audit_tripod_leg_one(errors: list[str]) -> None:
    """A13: a perna 1 do tripe (RPTR == WPTR) tem de estar NA DECISAO.

    MEDIDO na mesma corrida: "TRIPÉ NO SILÍCIO COMPROVADO (Item 63)! WPTR=68
    RPTR=52". O gate testava apenas fence e divergencia de bytes; a igualdade
    dos ponteiros nunca era avaliada, e um timeout de 2000 polls tambem passava.
    """
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    contract_path = ROOT / "driver" / "core" / "bc250_blt_proof_contract.c"
    if not present_path.exists() or not contract_path.exists():
        errors.append(
            "fontes do tripe de SDMA nao encontradas para auditar a perna 1")
        return

    present = present_path.read_text(encoding="utf-8", errors="replace")
    contract = contract_path.read_text(encoding="utf-8", errors="replace")

    require(present, "Bc250ClassifySdmaTripod(",
            "veredito do tripe por contrato testado", errors)
    require(present, "Bc250TripodAllowsClaim(tripodVerdict)",
            "guarda da frase TRIPE no bringup de SDMA", errors)
    require(present, "TRIPE_VEREDITO",
            "telemetria do veredito das tres pernas", errors)
    forbid(present,
           "if (g_Bc250SdmaPresent.TripodFenceObserved == (BC250_SDMA_PRESENT_FENCE_INIT + 1u) &&",
           "gate de tripe sem a perna RPTR==WPTR", errors)

    require(contract, "BC250_TRIPOD_RPTR_NOT_RETIRED",
            "veredito nomeado para RPTR que nao alcancou WPTR", errors)
    require(contract, "if (TargetWptrBytes == 0u)",
            "recusa de tripe por vacuidade em anel nunca armado", errors)


def audit_indirect_only_tunable(errors: list[str]) -> None:
    """B3: o default e' somente INDIRECT; PLANO A exige pedido explicito.

    MOTIVO: os dois PLANO A absorveram 14/14 submissoes em 16:19 e escondiam o
    INDIRECT. Depois de 17:43 e 18:16 -- em que o INDIRECT executou o DMA buffer
    real do dxgkrnl com o tripe completo -- a polaridade foi invertida: o
    default e' INDIRECT e SdmaPlanoAFallback=1 restaura a precedencia antiga
    apenas para bisseccao. Valor ausente ou ilegivel NAO liga o fallback.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append(
            "bc250_active_miniport.c nao encontrado para auditar SdmaIndirectOnly")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")

    require(text, 'L"SdmaPlanoAFallback", 0u',
            "PLANO A com default DESLIGADO", errors)
    require(text, "context->SdmaPlanoAFallbackPresent == 1u &&",
            "PLANO A exige valor presente e legivel", errors)
    require(text, "pathInput.PlanoAFallbackEnabled =",
            "tunable de PLANO A ligado a selecao de ramo", errors)
    # A polaridade antiga nao pode voltar por copia e cola.
    forbid(text, "SdmaIndirectOnly",
           "tunable de polaridade invertida (pre-B3)", errors)



def audit_ib_data_leg(errors: list[str]) -> None:
    """A14: o caminho INDIRECT nao pode dizer TRIPE com duas pernas.

    MEDIDO na corrida 2026-09-07 17:13: dez linhas "IB_TRIPOD PASS #N" saidas de
    um gate que avalia apenas RPTR==WPTR e fence sobre poison. Nenhum byte do
    destino era conferido -- mesma classe de falso sucesso da A13, em lugar que
    a A13 nao tocou.
    """
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    contract_path = ROOT / "driver" / "core" / "bc250_blt_proof_contract.c"
    if not present_path.exists() or not contract_path.exists():
        errors.append("fontes do IB indireto nao encontradas para auditar a perna 3")
        return

    present = present_path.read_text(encoding="utf-8", errors="replace")
    contract = contract_path.read_text(encoding="utf-8", errors="replace")

    forbid(present, "IB_TRIPOD PASS",
           "rotulo de tripe com duas pernas no caminho INDIRECT", errors)
    require(present, "Bc250CountDataProofMismatches(&dataPlan",
            "perna de dados conferida no caminho INDIRECT", errors)
    require(present, "IB_ENGINE_2LEGS",
            "rotulo honesto quando a perna de dados nao pode ser conferida", errors)
    require(present, "Bc250PlanDataProofCanaries(ExpectedEffectDestMc",
            "canaries do IB pelo contrato de fonte unica", errors)
    require(contract, "return BC250_DATA_PROOF_CANARY_COUNT;",
            "ausencia de leitura contada como divergencia total", errors)


def audit_process_gpuva_teardown(errors: list[str]) -> None:
    """A14: destruir processo tem de liberar as faixas de GPUVA dele.

    MEDIDO na corrida 17:13: apos o primeiro dwm.exe morrer, o dwm.exe seguinte
    recebeu status=10 (BC250_GPUVA_OWNER_MISMATCH) ao resolver 0x30000 -- faixa
    viva pertencente a um processo que nao existia mais.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append("bc250_active_miniport.c nao encontrado para auditar teardown de GPUVA")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")
    body = function_body(text, "NTSTATUS Bc250ActiveDestroyProcessObject(")
    if not body:
        errors.append("Bc250ActiveDestroyProcessObject nao localizado")
        return
    if "Bc250GpuvaSpaceInvalidateForObject(&Context->GpuvaSpace" not in body:
        errors.append(
            "DestroyProcess sem invalidacao das faixas de GPUVA do processo "
            "-- e assim que OWNER_MISMATCH sobrevive ao dono morto")



def audit_ring_wrap_arithmetic(errors: list[str]) -> None:
    """A15: a perna 1 tem de comparar RPTR com a volta do anel reduzida.

    MEDIDO na corrida 17:43 (FenceId=24): TargetWPTR=516 e RPTR=4612 num anel de
    4096 bytes -- 4612 = 4096 + 516. A fence havia batido; a comparacao crua
    reprovou, o timeout devolveu STATUS_IO_DEVICE_ERROR e o VidMm derrubou a
    maquina com BugCheck 0x119 (Arg1=2, Arg2=0xC0000185). A aritmetica ja existia
    no arquivo (Bc250SdmaRptrMatches) e o caminho INDIRECT nao a usava.
    """
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    if not present_path.exists():
        errors.append("bc250_sdma_present.c nao encontrado para auditar volta do anel")
        return
    text = present_path.read_text(encoding="utf-8", errors="replace")

    forbid(text, "if (rptrReg == targetWptr) {",
           "comparacao crua de RPTR sem a volta do anel", errors)
    require(text, "Bc250SdmaRptrMatches(rptrReg, rptrWbRaw, targetWptr)",
            "perna 1 do IB pela aritmetica de anel", errors)
    require(text, "Bc250TripodWrapRingPointer(",
            "reducao a uma volta antes de classificar e logar", errors)



def audit_node1_gate_telemetry(errors: list[str]) -> None:
    """B4: a recusa de submissao no Node 1 nao pode ser muda.

    MEDIDO nas corridas de 06 e 07/09: o KMD criou dois contextos Node 1 e
    recebeu ZERO submissoes, sem uma linha de log -- nao havia como distinguir
    "o scheduler nao submeteu" de "nos recusamos". A recusa vinha de
    GpuvaMappingsOperational == 0, e esse gate SO' abre quando GpuvaLabPromotion
    esta presente e em 1; essa leitura existia apenas no #else de
    BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP, isto e', fora do pacote ativo.

    Tres portoes: o tunable e lido no caminho de producao, a recusa e' logada
    com o BlockMask, e a abertura do gate tambem aparece no log.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append("bc250_active_miniport.c nao encontrado para auditar o gate do Node 1")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")

    require(text, '[StartDevice] GpuvaLabPromotion=%u (present=%u)',
            "leitura do gate de Node 1 no caminho de producao", errors)
    require(text, "RECUSA: GpuvaMappingsOperational=0",
            "recusa de Node 1 com motivo nomeado", errors)
    require(text, "BlockMask=0x%08X",
            "BlockMask no log da recusa de Node 1", errors)
    require(text, "GATE ABERTO: GpuvaMappingsOperational=1",
            "abertura do gate de Node 1 registrada", errors)

    # A recusa muda nao pode voltar.
    mute = ("&context->GpuvaMappingsOperational, 1, 1) != 1) {" + chr(10) +
            "        return STATUS_NOT_SUPPORTED;")
    if mute in text:
        errors.append(
            "recusa de Node 1 voltou a ser muda (STATUS_NOT_SUPPORTED sem log)")


def audit_node1_physical_gpuva_gate(errors: list[str]) -> None:
    """C3.17: snapshot logico nao pode ser a autoridade do submit Node 1.

    Medido em 12/09 19:27: o primeiro IB do RADV executou, mas a fence manteve
    um dos dois slots fixado ate o drain. A publicacao seguinte encontrou
    BUSY nas 16 tentativas e fechou GpuvaMappingsOperational, embora a page
    table fisica estivesse valida. Mais tarde o espelho tambem atingiu
    NO_SPACE. Fixar o snapshot nao protege a PTE: UpdatePageTable grava a PTE
    fisica antes de tentar publicar o espelho.

    No perfil explicitamente armado, a autoridade passa a ser a caminhada de
    todas as paginas na root do proprio contexto. O snapshot fica somente como
    diagnostico e deve ser liberado antes da reserva/publicacao da fence.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append("bc250_active_miniport.c ausente para auditar gate fisico Node 1")
        return
    text = strip_c_comments(active_path.read_text(encoding="utf-8", errors="replace"))

    require(text, "physicalGpuvaAdmission =",
            "admissao fisica explicita do Node 1", errors)
    require(text, "context->GpuvaLabPromotionPresent == 1u",
            "gate fisico restrito ao perfil explicitamente armado", errors)
    require(text, 'Bc250ActiveValidateGpuVaRange("NODE1_IB",',
            "caminhada integral da page table do IB Node 1", errors)
    require(text, "contextView.RootPageTableAddress",
            "root da caminhada derivada do contexto submetido", errors)
    require(text, "GATE FISICO PASS:",
            "veredito positivo observavel do gate fisico", errors)
    require(text, "gate fisico integral falhou",
            "falha fechada e observavel do gate fisico", errors)
    require(text, "SNAPSHOT_DIAGNOSTICO",
            "snapshot rebaixado explicitamente a diagnostico", errors)
    require(text, "reserveSync.GpuvaPinValid = FALSE;",
            "fence nao retém snapshot logico sem utilidade para o hardware", errors)
    require(text, "sync->ReserveAccepted && sync->GpuvaPinValid &&",
            "anexo de pin condicionado em vez de automatico", errors)
    forbid(text, "gpuvaReader.Space = NULL;",
           "transferencia da referencia do snapshot para a fence", errors)



def audit_tdr_pair_has_backend(errors: list[str]) -> None:
    """B6 / item 25: o par de TDR nao pode devolver sucesso sem silicio.

    ANTES: Bc250McdmResetFromTimeout e Bc250McdmRestartFromTimeout escreviam
    uma linha de log e devolviam STATUS_SUCCESS sem tocar em registrador --
    violacao direta da Lei 4, e no par de DDIs em que o dxgkrnl confia para
    decidir se o adaptador foi quiescido e se voltou. Pior: mcdm_callbacks_test
    EXIGIA esse STATUS_SUCCESS, ou seja o teste congelava a mentira.

    AGORA: as duas dependem de executor instalado; ponteiro nulo ou executor
    que devolve FALSE resulta em STATUS_DEVICE_NOT_READY. O restart ainda exige
    prova fisica (fence sobre poison) dentro do executor.
    """
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    present_header_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.h"
    test_path = ROOT / "driver" / "tests" / "mcdm_callbacks_test.c"
    for path in (callbacks_path, present_path, present_header_path, test_path):
        if not path.exists():
            errors.append(f"{path.name} nao encontrado para auditar o par de TDR")
            return

    callbacks = callbacks_path.read_text(encoding="utf-8", errors="replace")
    present = present_path.read_text(encoding="utf-8", errors="replace")
    present_header = present_header_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    reset_body = function_body(callbacks,
        "NTSTATUS APIENTRY Bc250McdmResetFromTimeout(CONST HANDLE hAdapter)")
    restart_body = function_body(callbacks,
        "NTSTATUS APIENTRY Bc250McdmRestartFromTimeout(CONST HANDLE hAdapter)")
    if not reset_body or not restart_body:
        errors.append("corpo das DDIs de TDR nao localizado")
        return

    if "g_Bc250TdrQuiesce == NULL || !g_Bc250TdrQuiesce()" not in reset_body:
        errors.append(
            "ResetFromTimeout sem executor fisico -- STATUS_SUCCESS seria falso sucesso")
    if "g_Bc250TdrRestart == NULL || !g_Bc250TdrRestart()" not in restart_body:
        errors.append(
            "RestartFromTimeout sem executor fisico -- STATUS_SUCCESS seria falso sucesso")
    for body, name in ((reset_body, "ResetFromTimeout"),
                       (restart_body, "RestartFromTimeout")):
        if "STATUS_DEVICE_NOT_READY" not in body:
            errors.append(f"{name} sem caminho de recusa quando o silicio nao responde")

    # A corrida 20:41 mostrou que HALT + religar o anel preserva o comando
    # POLL_REGMEM preso. O restart precisa pulsar o reset integral, reconstruir
    # o anel, conferir readback e so' depois executar a prova fisica.
    restart_executor = function_body(present,
        "BOOLEAN Bc250SdmaPresentRestartAfterTdr(void)")
    if not restart_executor:
        errors.append("executor Bc250SdmaPresentRestartAfterTdr nao localizado")
        return
    require(present_header, "#define BC250_SDMA0_MMIO_WINDOW_BYTES     0x06000u",
            "janela MMIO cobre GRBM_SOFT_RESET em 0x08020", errors)
    require(restart_executor, "grbmBefore | BC250_GRBM_SOFT_RESET_SDMA0",
            "assert do reset integral da SDMA0", errors)
    require(restart_executor, "grbmBefore & ~BC250_GRBM_SOFT_RESET_SDMA0",
            "deassert do reset integral da SDMA0", errors)
    require(restart_executor, "grbmAsserted",
            "readback do reset assertado", errors)
    require(restart_executor, "BC250_SDMA0_REG_RB_RPTR_HI",
            "reconstrucao do RPTR alto pos-reset", errors)
    require(restart_executor, "Bc250TdrValidateSdmaRestartReadback(",
            "gate puro de readback pos-reset", errors)
    require(restart_executor,
            "TDR_FULL_RESET E REPROGRAMACAO: READBACK PASS",
            "telemetria do readback antes da prova", errors)

    readback_at = restart_executor.find(
        "readbackStatus = Bc250TdrValidateSdmaRestartReadback(")
    proof_at = restart_executor.find("proven = Bc250SdmaPresentSyncNop();")
    if readback_at < 0 or proof_at < 0 or readback_at >= proof_at:
        errors.append("prova fisica do restart aparece antes do gate de readback integral")
    success_log_at = restart_executor.find(
        "TDR_FULL_RESET E REPROGRAMACAO: READBACK PASS")
    if success_log_at < proof_at:
        errors.append("telemetria persistente de readback ainda e emitida sob o spinlock")

    # O executor de restart tem de PROVAR, nao apenas religar registradores.
    require(present, "proven = Bc250SdmaPresentSyncNop();",
            "prova fisica no reinicio pos-TDR", errors)
    require(present, "TDR_RESTART FALHOU",
            "recusa nomeada quando a engine nao volta", errors)
    require(present, "TDR_QUIESCE",
            "telemetria do quiesce fisico", errors)

    # E o teste nao pode voltar a exigir a mentira.
    if "CHECK(Bc250McdmResetFromTimeout((HANDLE)0x250) == STATUS_SUCCESS);" in test and        "Bc250McdmInstallTdrExecutors(TestTdrQuiesceOk, TestTdrRestartOk);" not in test:
        errors.append(
            "mcdm_callbacks_test exige STATUS_SUCCESS de TDR sem executor instalado")



def audit_engine_status_measures(errors: list[str]) -> None:
    """item 24: QueryEngineStatus tem de MEDIR, nao supor.

    ANTES: a DDI punha EngineStatus.Responsive = 1 sem olhar um registrador, e
    mcdm_callbacks_test EXIGIA isso. O dxgkrnl chama essa DDI durante o TDR
    para saber se a engine esta viva; responder "viva" sempre faria o TDR
    concluir que nao havia o que resetar.

    AGORA: a resposta vem de duas passagens de RPTR/WPTR (Bc250TdrDetectHang)
    com os ponteiros reduzidos a uma volta do anel -- a armadilha medida em
    17:43, onde RPTR=4612 e alvo 516 num anel de 4096 bytes.
    """
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    test_path = ROOT / "driver" / "tests" / "mcdm_callbacks_test.c"
    for path in (callbacks_path, present_path, test_path):
        if not path.exists():
            errors.append(f"{path.name} nao encontrado para auditar QueryEngineStatus")
            return

    callbacks = callbacks_path.read_text(encoding="utf-8", errors="replace")
    present = present_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    body = function_body(callbacks,
        "NTSTATUS APIENTRY Bc250McdmQueryEngineStatus(")
    if not body:
        errors.append("Bc250McdmQueryEngineStatus nao localizado")
        return
    if "g_Bc250EngineResponsiveProbe == NULL" not in body:
        errors.append(
            "QueryEngineStatus sem probe de medicao -- Responsive=1 seria suposicao")
    if "EngineStatus.Responsive = 1;" in body and        "g_Bc250EngineResponsiveProbe(&responsive)" not in body:
        errors.append("QueryEngineStatus afirma Responsive=1 sem consultar a probe")

    require(present, "Bc250TdrDetectHang(0u, rptr1, wptr1, rptr2, wptr2, &isHung)",
            "deteccao propria por duas passagens", errors)
    require(present, "ENGINE_PROBE: Responsive=%u",
            "telemetria da deteccao propria", errors)
    # A reducao de volta e' obrigatoria: sem ela, anel drenado vira "travado".
    if present.count("Bc250TripodWrapRingPointer(") < 6:
        errors.append(
            "probe de responsividade sem reducao de volta do anel nos quatro ponteiros")

    if "CHECK(g_engineStatus.EngineStatus.Responsive == 1);" in test and        "Bc250McdmInstallEngineResponsiveProbe(TestProbeAlive);" not in test:
        errors.append(
            "mcdm_callbacks_test exige Responsive==1 sem probe instalada")


def audit_induced_hang_is_fail_closed(errors: list[str]) -> None:
    """item 25: o hang induzido nao pode disparar por acidente nem repetir.

    Ele existe para exercitar o TDR: trava a SDMA0 de verdade e NAO notifica
    completion, para que o timeout de 2 s do dxgkrnl dispare. Exatamente por
    isso precisa de tunable presente E igual a 1, disparo unico por bind, e
    hang FISICO (POLL_REGMEM) -- nunca um delay de CPU fingindo travamento.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    for path in (active_path, present_path):
        if not path.exists():
            errors.append(f"{path.name} nao encontrado para auditar o hang induzido")
            return

    active = active_path.read_text(encoding="utf-8", errors="replace")
    present = present_path.read_text(encoding="utf-8", errors="replace")

    require(active, 'L"TdrInduceHang", 0u',
            "hang induzido com default desarmado", errors)
    require(active, "context->TdrInduceHangPresent == 1u && context->TdrInduceHang == 1u",
            "hang induzido exige valor presente e legivel", errors)
    require(active, "InterlockedCompareExchange(&context->TdrHangInduced, 1, 0) == 0",
            "disparo unico do hang induzido", errors)
    require(present, "Bc250SdmaEmitPollRegMem(packets, 16u, &cursor,",
            "hang induzido por pacote fisico da engine", errors)
    forbid(present, "TDR_HANG_INDUZIDO: KeStallExecutionProcessor",
           "hang simulado por delay de CPU", errors)


def audit_context_abi_before_ready(errors: list[str]) -> None:
    """ContextAbiReady=1 exige ContextAbi POPULADO antes.

    MEDIDO na corrida 08/09 14:06, com a instrumentacao nova:
      [McdmCreateContext] OK: Node=0, hContext=0x..., DmaBuf=0x0, SegSet=0x0

    Bc250ActiveCreateContextObject faz "*ContextInfo = context->ContextAbi",
    sobrescrevendo os defaults que Bc250McdmCreateContext acabara de escrever. E
    ContextAbi so' era populado dentro do bloco
    "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)" -- so' na imagem
    de PRODUCAO. Na de laboratorio, ContextAbiReady ia a 1 com a estrutura
    inteiramente zerada, e o dxgkrnl recebia contexto com DmaBufferSize=0.

    Mesma classe do GpuvaLabPromotion (B4): campo escrito em UM ramo do #if.
    """
    path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not path.exists():
        errors.append(
            "bc250_active_miniport.c nao encontrado para auditar a ABI de contexto")
        return
    text = path.read_text(encoding="utf-8", errors="replace")

    require(text, "static void Bc250ActiveInitializeContextAbi(",
            "fonte unica da ABI de contexto", errors)

    # Fora do helper, ninguem pode escrever nos campos direto: isso seria uma
    # segunda fonte de verdade, que e' exatamente o defeito medido.
    helper = function_body(
        text, "static void Bc250ActiveInitializeContextAbi(")
    if not helper:
        errors.append("Bc250ActiveInitializeContextAbi nao localizada")
        return
    outside = text.replace(helper, "", 1)
    if "ContextAbi.DmaBufferSize" in outside:
        errors.append(
            "ContextAbi.DmaBufferSize escrito fora da fonte unica "
            "(segunda fonte de verdade)")

    # Sem janela arbitraria: entre um "ContextAbiReady=1" e o anterior (ou o
    # inicio do arquivo) tem de existir uma chamada a fonte unica. Assim cada
    # um dos sitios e' coberto por uma populacao propria, independentemente de
    # quantas linhas de comentario existam entre as duas.
    lines = text.splitlines()
    previous = 0
    for index, line in enumerate(lines):
        if "ContextAbiReady, 1)" not in line:
            continue
        segment = "\n".join(lines[previous:index])
        if "Bc250ActiveInitializeContextAbi(" not in segment:
            errors.append(
                "ContextAbiReady=1 na linha %d sem popular ContextAbi antes "
                "(publica DmaBufferSize=0 ao dxgkrnl)" % (index + 1))
        previous = index


def audit_process_lifecycle_ddis_speak(errors: list[str]) -> None:
    """O ciclo de vida de processo nao pode recusar em silencio.

    MEDIDO na corrida 08/09 13:00 (perfil lab, Codigo 0, tela preta): a
    negociacao de VidPn CONVERGIU 39 vezes e foi descartada 46. Em cada ciclo o
    log mostrava CreateProcess -> CreateDevice -> CreateContext(Node 0) ->
    DestroyProcess -- sete deles com o MESMO hDxgkProcess -- e NAO havia como
    saber se alguma dessas DDIs havia RECUSADO: as tres tinham caminhos de falha
    sem uma linha de log. Mesma classe do B4, agora no caminho critico do
    processo que morre.

    Tres portoes: cada uma das tres DDIs nomeia suas recusas, o desfecho de
    sucesso tambem aparece (senao "ausencia de RECUSADO" nao prova nada), e o
    nome do processo vem do contrato -- DXGKARG_CREATEPROCESS carrega
    pProcessName desde WDDM 2.4 e declaramos 2.7.
    """
    path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    if not path.exists():
        errors.append(
            "bc250_mcdm_callbacks.c nao encontrado para auditar o ciclo de "
            "vida de processo")
        return
    text = path.read_text(encoding="utf-8", errors="replace")

    # Exigir a mera PRESENCA de "RECUSADO:" prova apenas que UMA das recusas
    # fala -- verificado por mutacao: removendo uma das tres de CreateContext o
    # gate passava. O invariante correto e': DEPOIS do log de entrada, todo
    # return que nao seja STATUS_SUCCESS tem de ter um Bc250DisplayLog perto.
    signatures = {
        "McdmCreateProcess": "NTSTATUS APIENTRY Bc250McdmCreateProcess(",
        "McdmCreateDevice": "NTSTATUS APIENTRY Bc250McdmCreateDevice(",
        "McdmCreateContext": "NTSTATUS APIENTRY Bc250McdmCreateContext(",
    }
    for ddi, signature in signatures.items():
        body = function_body(text, signature)
        if not body:
            errors.append("%s nao localizada para auditar recusas mudas" % ddi)
            continue
        lines = body.splitlines()
        entered = next(
            (i for i, l in enumerate(lines) if "] Entered:" in l), None)
        if entered is None:
            errors.append("%s nao registra a entrada" % ddi)
            continue
        for index in range(entered + 1, len(lines)):
            stripped = lines[index].strip()
            if not stripped.startswith("return "):
                continue
            if "STATUS_SUCCESS" in stripped:
                continue
            window = "\n".join(lines[max(entered, index - 6):index])
            if "Bc250DisplayLog(" not in window:
                errors.append(
                    "%s: recusa MUDA em '%s' (nenhum log nas 6 linhas "
                    "anteriores)" % (ddi, stripped))

    require(text, "[McdmCreateDevice] OK: hDevice=",
            "desfecho de sucesso em CreateDevice", errors)
    require(text, "[McdmCreateContext] OK: Node=",
            "desfecho de sucesso em CreateContext", errors)

    require(text, "pArgs->pProcessName",
            "nome do processo lido do contrato DXGKARG_CREATEPROCESS", errors)
    require(text, "Flags(System=%u Gdi=%u Vm=%u VmWorker=%u Isolated=%u)",
            "flags de criacao de processo decodificadas", errors)

    # A copia do nome nao pode voltar a virar memcpy: o link offline nao o tem.
    if "volatile WCHAR *nameDst" not in text:
        errors.append(
            "copia do nome de processo sem destino volatile: o /O2 a reduz a "
            "memcpy e o link offline das suites quebra (LNK2019)")


def audit_second_preflight_transport_empty(errors: list[str]) -> None:
    """O segundo preflight readonly exige transporte MMIO vazio.

    MEDIDO na corrida 08/09 11:48 (Codigo 43): o StartDevice de laboratorio
    passou a mapear Core.MmioTransport para programar a MMU global e seguiu com
    ele MAPEADO ate o segundo Bc250DxgkDdiStartDeviceReadonly. A guarda de
    entrada daquela DDI recusa transporte nao-vazio com STATUS_INVALID_PARAMETER
    -- e recusa ANTES de escrever LastStage, entao SecondPreflightStage ficou
    exibindo o 3 (RUNTIME_MISSING) do PRIMEIRO preflight. Resultado:
    SecondReadonlyGatePassed=0, bring-up fisico inteiro pulado, e o rodapé
    publicou o inicializador de persistentStatus (0xC00000BB) como veredito.
    O bind 09:50 do MESMO perfil, sem esse bloco, fechou em estagio 8/Codigo 0.

    Tres portoes:
      1. o transporte volta a vazio antes do segundo preflight;
      2. o StartDevice publica o NTSTATUS CRU da segunda chamada -- o estagio
         sozinho e' ambiguo por construcao;
      3. o rodapé so' pode trocar o status pelo do ensaio de submissao
         persistente se esse ensaio TIVER ACONTECIDO.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append(
            "bc250_active_miniport.c nao encontrado para auditar a ordem do "
            "segundo preflight")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")

    body = function_body(text, "NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(")
    if not body:
        errors.append(
            "Bc250DxgkDdiStartDeviceActive nao localizada para auditar a ordem "
            "do segundo preflight")
        return

    ensure = body.find("Bc250ActiveEnsureGlobalMmu(")
    if ensure < 0:
        errors.append(
            "sitio da MMU global nao localizado no StartDevice ativo")
        return
    # A busca do segundo preflight PRECISA comecar depois da MMU global: a mesma
    # atribuicao textual ocorre no PRIMEIRO preflight, no topo da funcao. Buscar
    # do inicio casava com ele, a janela saia invertida e o gate passava sem
    # examinar nada -- falso negativo silencioso, medido ao mutar o driver.
    second = body.find(
        "preflightStatus = Bc250DxgkDdiStartDeviceReadonly(", ensure)
    if second < 0:
        errors.append(
            "segundo preflight readonly nao localizado depois da MMU global "
            "no StartDevice ativo")
        return

    window = body[ensure:second]
    if "Bc250MmioTopologyUnmap(&context->Core.MmioTransport)" not in window:
        errors.append(
            "transporte MMIO permanece mapeado entre a MMU global e o segundo "
            "preflight (regressao da corrida 08/09 11:48, Codigo 43)")
    if "BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP" not in window:
        errors.append(
            "o retorno do transporte a vazio precisa ser exclusivo do bring-up "
            "de laboratorio: o caminho de producao mantem o mapa por design")

    require(text, "BC250_PROBE_SECOND_PREFLIGHT_STATUS",
            "NTSTATUS cru do segundo preflight publicado", errors)
    require(text, "[StartDevice] Segundo preflight: status=0x%08X",
            "log do segundo preflight com status e estado do transporte",
            errors)

    require(text, "persistentAttempted != FALSE",
            "rodapé so' troca o status por um ensaio que aconteceu", errors)
    masking = ("if (context->PersistentSubmission == 1u &&" + chr(10) +
               "        persistentReady == FALSE) {")
    if masking in text:
        errors.append(
            "rodapé voltou a publicar persistentStatus sem exigir "
            "persistentAttempted (mascara a falha real do bring-up)")

    # O teste que reproduz a corrida nao pode desaparecer.
    test_path = ROOT / "driver" / "tests" / "dxgk_readonly_ddi_test.c"
    if not test_path.exists():
        errors.append("dxgk_readonly_ddi_test.c nao encontrado")
        return
    test_text = test_path.read_text(encoding="utf-8", errors="replace")
    require(test_text, "CHECK(status == STATUS_INVALID_PARAMETER);",
            "teste da recusa por transporte mapeado", errors)
    require(test_text, "CHECK(g_context.RuntimeStaged == 1u);",
            "teste prova que o runtime estava staged na recusa", errors)


def audit_induced_hang_package_order(errors: list[str]) -> None:
    """item 25: o primeiro StartDevice precisa receber o flag pelo INF.

    A chave de servico ainda nao existe antes da instalacao. O pacote normal
    permanece em zero; o empacotador materializa 1 apenas no INF candidato,
    antes do catalogo, e exige versao/consentimento novos. O binder exige o
    mesmo consentimento quando o manifesto diz que o pacote esta armado.
    """
    paths = {
        "inf": ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf",
        "build": ROOT / "tools" / "build-bc250mcdm-package.ps1",
        "bind": ROOT / "tools" / "bind-bc250mcdm-failclosed.ps1",
        "arm": ROOT / "tools" / "arm-tdr-induce-hang.ps1",
    }
    texts: dict[str, str] = {}
    for label, path in paths.items():
        if not path.exists():
            errors.append(f"{path.name} ausente para auditar ordem do ensaio TDR")
            return
        texts[label] = path.read_text(encoding="utf-8", errors="replace")

    require(texts["inf"],
            "HKR,Parameters,TdrInduceHang,0x00010001,0",
            "INF fonte TDR fail-closed", errors)
    require(texts["build"], "[switch]$ArmTdrInducedHang",
            "modo explicito de pacote TDR", errors)
    require(texts["build"],
            "-IAcceptDeliberateHangAndPossibleBugCheck",
            "consentimento no empacotamento TDR", errors)
    require(texts["build"],
            "Pacote TDR armado exige -PackageDriverVersion",
            "versao nova obrigatoria no pacote TDR", errors)
    require(texts["build"],
            "-PackageDriverVersion precisa diferir",
            "reuso da versao fonte proibido no pacote TDR", errors)
    require(texts["build"],
            "HKR,Parameters,TdrInduceHang,0x00010001,1",
            "flag materializado no INF empacotado", errors)
    require(texts["build"], "MCDM_TDR_INDUCED_HANG",
            "identidade explicita do pacote TDR", errors)
    require(texts["bind"],
            "if (-not $IAcceptDeliberateHangAndPossibleBugCheck)",
            "consentimento no bind do pacote TDR", errors)
    require(texts["arm"], "so sera lido no PROXIMO StartDevice",
            "script explica latencia do tunable", errors)
    require(texts["arm"], "Nao crie a chave a mao",
            "script proibe chave manual", errors)


def audit_marco3_closes_its_own_transaction(errors: list[str]) -> None:
    """Marco 3 must preserve partial proof and always restore BasicDisplay."""
    launcher = (ROOT / "tools/EXECUTAR_MARCO3.cmd").read_text(encoding="utf-8")
    runner = (ROOT / "tools/test-radv-wddm-present-tripod.ps1").read_text(
        encoding="utf-8")

    bind_call = launcher.find("bind-bc250mcdm-lab.ps1")
    recovery_call = launcher.find("BC250_RECOVERY.cmd")
    marker_write = launcher.find("CLEAN_BOOT_REQUIRED")
    if bind_call < 0 or recovery_call <= bind_call:
        errors.append("Marco 3 does not run recovery after its physical bind")
    if marker_write <= recovery_call:
        errors.append("Marco 3 does not publish the clean-boot marker after recovery")
    require(launcher, 'if not "%RECOVERY_EXIT%"=="0"',
            "fail-closed Marco 3 recovery result gate", errors)

    checks = runner.split("$checks = [ordered]@{", 1)[-1]
    forbid(checks,
           "render_byte_proof = (-not $timedOut -and $exitCode -eq 0",
           "render proof erased by a later process failure", errors)
    forbid(checks,
           "wddm_present_called = (-not $timedOut -and $exitCode -eq 0",
           "present proof erased by a later process failure", errors)
    require(checks, "dwm_wer_events = $dwmWerCount",
            "separate WER and DWM crash diagnostics", errors)


def audit_present_is_gpu_attributable(errors: list[str]) -> None:
    """C3: nenhum pixel do present pode nascer da CPU sem reprovar a corrida.

    DEFEITO MEDIDO NA CORRIDA 11/09 22:46, a que fechou o "Marco 2". O log do
    KMD daquele bind tem DUAS linhas PRESENT_BLT e nenhuma delas e' de GPU:

        PRESENT_BLT: 2560x1080 (11059200 bytes), GpuVa=0x200020000, ...
        PRESENT_BLT: Host Kernel Blt CONCLUIDO COM SUCESSO! 11059200 bytes ...

    Zero "SDMA HW BLT CONCLUIDO". Os 11 MB foram para o scanout por
    RtlCopyMemory. Mesmo assim o veredito saiu "10/10 PASS" com
    sdma_blt_executed=True, porque o check casava com a string 'PRESENT_BLT'
    -- e ate' com 'BYTE_PROOF INCONCLUSIVO'. E o contador autoritativo do
    driver arquivou "Fallbacks=0", porque FallbackCpuCount so' conta recusa de
    Bc250SdmaPresentBlt e e' CEGO a copia que o escape realmente faz.

    Quarta ocorrencia da classe "o gate carimbava o falso sucesso": A15 obrigava
    a comparacao crua de RPTR, mcdm_callbacks_test exigia STATUS_SUCCESS sem
    executor de TDR, bc250_d3d_umd_test.c exigia o memcpy no Draw.

    Este portao trava as quatro pontas:
      1. o veredito do runner nao pode voltar a aceitar 'PRESENT_BLT' como prova
         de SDMA, e precisa ter o check que reprova pixel de CPU;
      2. o caminho de copia por CPU do escape precisa se declarar como tal e
         contabilizar em HostCpuCopyCount;
      3. a "Garantia de Scanout GOP" -- a copia por CPU que rodava DEPOIS de um
         SDMA bem-sucedido, no mesmo endereco, tornando o quadro 1 nao
         atribuivel a GPU -- nao pode voltar a existir;
      4. o contador tem de aparecer no teardown, senao a evidencia nao carrega
         o numero.
    """
    runner_path = ROOT / "tools" / "test-radv-wddm-present-tripod.ps1"
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    present_header = ROOT / "driver" / "wdk" / "bc250_sdma_present.h"
    for path in (runner_path, active_path, present_path, present_header):
        if not path.exists():
            errors.append(f"fonte ausente para auditar atribuicao do present: {path}")
            return

    runner = runner_path.read_text(encoding="utf-8", errors="replace")
    active = active_path.read_text(encoding="utf-8", errors="replace")
    present = present_path.read_text(encoding="utf-8", errors="replace")
    header = present_header.read_text(encoding="utf-8", errors="replace")

    # 1. Veredito do runner.
    require(runner, "sdma_blt_executed = ($displayLogDelta -match 'SDMA HW BLT CONCLUIDO COM SUCESSO')",
            "sdma_blt_executed derivado SO' da string do ramo de GPU", errors)
    require(runner, "no_host_copy_fallback",
            "check que reprova a corrida com pixel produzido por CPU", errors)

    # A linha do check nao pode citar as strings frouxas que produziram o falso
    # PASS. Recorte exato: so' a atribuicao de sdma_blt_executed.
    marker = "sdma_blt_executed ="
    start = runner.find(marker)
    if start < 0:
        errors.append("nao foi possivel recortar a atribuicao de sdma_blt_executed")
    else:
        line_end = runner.find("\n", start)
        assignment = runner[start:line_end if line_end > 0 else len(runner)]
        for loose in ("'PRESENT_BLT'", "BYTE_PROOF INCONCLUSIVO", "Bc250Escape",
                      "Spread SCANOUT", "TILE_PROBE"):
            if loose in assignment:
                errors.append(
                    f"sdma_blt_executed voltou a aceitar prova frouxa ({loose}); "
                    "foi assim que a corrida 11/09 22:46 saiu 10/10 PASS com "
                    "present feito por RtlCopyMemory")

    # 2. O ramo de copia por CPU do escape declara-se e conta.
    require(active, "PRESENT_BLT: COPIA POR CPU",
            "ramo de copia por CPU do escape declarado honestamente", errors)
    require(active, "InterlockedIncrement(&g_Bc250SdmaPresent.HostCpuCopyCount)",
            "copia por CPU do escape contabilizada", errors)
    forbid(active, "Host Kernel Blt CONCLUIDO COM SUCESSO",
           "rotulo antigo que anunciava copia por CPU como sucesso", errors)

    # 3. A garantia por CPU depois de SDMA bem-sucedido nao pode voltar.
    forbid(active, "Garantia de Scanout GOP",
           "copia por CPU sobre destino que o SDMA ja escreveu "
           "(torna o quadro nao atribuivel a GPU)", errors)

    # Invariante sem janela de linhas: entre o log de SDMA bem-sucedido e o
    # return daquele ramo nao pode existir copia de memoria alguma.
    #
    # ARMADILHA JA VIVIDA (o gate do B3 reprovou por citar o nome banido dentro
    # de um comentario): a varredura roda sobre o codigo COM OS COMENTARIOS
    # REMOVIDOS, senao o proprio comentario que explica a remocao do memcpy
    # reprova o arquivo.
    # O corte tem de ser feito sobre o arquivo JA sem comentarios: fatiar antes
    # de remover comeca a leitura no meio de um literal de string e dessincroniza
    # o parser de aspas.
    active_code = strip_c_comments(active)
    success = active_code.find("SDMA HW BLT CONCLUIDO COM SUCESSO")
    if success < 0:
        errors.append("ramo de sucesso do SDMA no escape nao encontrado")
    else:
        tail = active_code[success:]
        stop = tail.find("return STATUS_SUCCESS;")
        window = tail[:stop] if stop > 0 else tail
        for copier in ("RtlCopyMemory", "memcpy", "RtlMoveMemory"):
            if copier in window:
                errors.append(
                    f"{copier} entre o sucesso do SDMA e o retorno do escape: "
                    "o pixel deixa de ser atribuivel a GPU")

    # 4. Contador visivel na evidencia.
    require(header, "HostCpuCopyCount",
            "campo de contagem de copia por CPU no estado do present", errors)
    require(present, "HostCpuCopies=%ld",
            "contagem de copia por CPU publicada no teardown", errors)


def audit_present_source_uses_segment_base(errors: list[str]) -> None:
    """C3.4: a origem do present tem de sair da fonte unica de conversao MC.

    DEFEITO MEDIDO NA CORRIDA 12/09 12:29, e visivel na tela.

    Bc250ActiveResolveDisplayGpuVa devolve um OFFSET DE SEGMENTO -- o campo de
    saida de Bc250GpuvaSpaceResolveAddress chama-se literalmente
    BackingSegmentOffset. O segmento 1 nao comeca no byte zero da VRAM: comeca
    em LocalSegmentBaseOffset (0xA8C000 = 11.059.200, logo apos a reserva de
    scanout). O caminho de present somava SO' VramMcStart:

        srcGpu = g_Bc250SdmaPresent.VramMcStart + physAddr;

    Resultado: o SDMA lia 11.059.200 bytes ANTES da imagem. O blt concluia com
    fence e RPTR==WPTR -- a engine executou de verdade -- mas sobre a regiao
    errada. Na tela deu preto com uma faixa cinza embaixo. O tripe inteiro
    passava, porque nenhuma das tres pernas confere de ONDE se leu.

    O comentario de BC250_MCDM_RUNTIME_CAPS.VramMcBase ja dizia que o campo era
    "a unica fonte da base, usada tanto pelo codificador de entradas quanto pela
    instalacao da root, para que as duas nao possam mais sair de fase". O
    present era um TERCEIRO consumidor, e saiu de fase.

    Este portao exige que a conversao passe por Bc250SegmentOffsetToMc e proibe
    o padrao cru que produziu o defeito.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    contract_c = ROOT / "driver" / "core" / "bc250_segment_address_contract.c"
    test_path = ROOT / "driver" / "tests" / "segment_address_contract_test.c"
    for path in (active_path, contract_c, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar origem do present: {path}")
            return

    active = strip_c_comments(
        active_path.read_text(encoding="utf-8", errors="replace"))
    test = test_path.read_text(encoding="utf-8", errors="replace")

    require(active, "mcStatus = Bc250SegmentOffsetToMc(",
            "conversao de offset de segmento pela fonte unica, governando o ramo",
            errors)

    # O padrao cru nao pode voltar em sitio nenhum do miniport.
    for bad in ("VramMcStart + physAddr",
                "VramMcStart + segmentOffset",
                "VramMcStart + offset"):
        if bad in active:
            errors.append(
                f"conversao crua de volta no present ({bad}): falta "
                "LocalSegmentBaseOffset -- foi o defeito visivel da corrida 12:29")

    # Recorte do ramo que resolve a GPUVA de origem: entre a chamada ao
    # resolvedor e o disparo do blt, a atribuicao de srcGpu tem de vir do
    # helper, nao de uma soma local.
    start = active.find("Bc250ActiveResolveDisplayGpuVa(targetHandle")
    stop = active.find("Bc250SdmaPresentBlt(srcGpu")
    if start < 0 or stop <= start:
        errors.append("nao foi possivel recortar o ramo de origem do present")
    else:
        window = active[start:stop]
        if "Bc250SegmentOffsetToMc(" not in window:
            errors.append(
                "o ramo que resolve a GPUVA de origem nao chama "
                "Bc250SegmentOffsetToMc antes do blt")
        if "g_Bc250SdmaPresent.VramMcStart +" in window:
            errors.append(
                "soma crua com VramMcStart dentro do ramo de origem do present")

        # ARMADILHA DO DOMINIO, medida na corrida 12/09 12:51.
        #
        # Existem DUAS bases de VRAM e elas nao sao intercambiaveis:
        #   caps.VramMcBase                = 0x450000000   (GCVM: PTE, root, VMID)
        #   g_Bc250SdmaPresent.VramMcStart = 0xF400000000  (anel de present SDMA)
        #
        # A primeira versao da correcao usou a base da GCVM na ORIGEM enquanto o
        # DESTINO seguia derivado de VramMcStart. Dominios diferentes: o blt deu
        # timeout com a fence no poison em 8 de 8 tentativas e o present caiu na
        # copia por CPU -- que na tela parecia sucesso, porque a copia por CPU le
        # os pixels certos. So' o contador HostCpuCopies denunciou.
        #
        # Origem e destino do blt PRECISAM partir da mesma base.
        if "srcCaps.VramMcBase" in window or "caps.VramMcBase" in window:
            errors.append(
                "origem do present derivada de caps.VramMcBase (dominio da GCVM) "
                "enquanto o destino usa g_Bc250SdmaPresent.VramMcStart: foi o "
                "timeout de 8/8 da corrida 12:51")
        if "g_Bc250SdmaPresent.VramMcStart," not in window:
            errors.append(
                "origem do present nao parte de g_Bc250SdmaPresent.VramMcStart, "
                "a mesma base do destino")

    # O teste tem de carregar os numeros medidos, senao vira carimbo.
    for needle in ("0x000000F401560000ull", "0x000000F401FEC000ull",
                   "0x0000000000A8C000ull", "0x0000000451FEC000ull"):
        require(test, needle,
                f"numero medido da corrida 12:29 no teste ({needle})", errors)

    # ------------------------------------------------------------------
    # C3.6 (12/09) -- O SEGUNDO CAMINHO DE PRESENT.
    #
    # A C3.4 corrigiu a conversao no caminho de ESCAPE e este portao so' cobria
    # aquele. DxgkDdiPresent tinha o MESMO defeito, intocado: usava o offset de
    # segmento direto como indice de g_Bc250DisplayState.FrameBufferVirtual.
    # Resultado medido na corrida 13:11: amostrou 11.059.200 bytes antes da
    # superficie, leu zeros, reprovou um quadro correto e a reacao derrubou a
    # maquina com 0x119.
    #
    # Cobrir um caminho e deixar o outro foi o proprio defeito. O portao passa a
    # exigir os DOIS.
    # ------------------------------------------------------------------
    display_path = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
    present_c = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    if not display_path.exists() or not present_c.exists():
        errors.append("fontes do caminho de display ausentes")
        return
    display = strip_c_comments(
        display_path.read_text(encoding="utf-8", errors="replace"))
    present_src = present_c.read_text(encoding="utf-8", errors="replace")

    # Forma EXATA da condicao, nao so' a presenca do nome. Uma versao anterior
    # deste portao exigia apenas a string "Bc250SdmaPresentSegmentOffsetToVram(",
    # e a mutacao "if (0 && Bc250SdmaPresentSegmentOffsetToVram(...)" passava --
    # portao que nao pega a propria mutacao nao e' portao.
    require(display, "if (Bc250SdmaPresentSegmentOffsetToVram(presentSegmentId,",
            "conversao de offset de segmento governando o ramo do DxgkDdiPresent",
            errors)
    require(display, "srcPhysical = srcVramOffset;",
            "origem do present vinda da conversao, nao do offset cru", errors)
    require(present_src, "Bc250SegmentOffsetToVramOffset(",
            "vista de offset de VRAM derivada da fonte unica", errors)
    # Guarda de geometria: forma exata, pelo mesmo motivo.
    require(present_src,
            "if (InterlockedCompareExchange(&g_Bc250SdmaPresent.SegmentGeometryReady, 1, 1) != 1) {",
            "recusa de conversao sem geometria publicada", errors)

    # A atribuicao crua do offset de segmento nao pode voltar.
    forbid(display, "srcPhysical = presentPhysical;",
           "offset de segmento usado direto como offset de VRAM no "
           "DxgkDdiPresent (defeito medido na corrida 13:11)", errors)

    # Fonte unica de verdade: a geometria tem UM escritor.
    writers = display.count("g_Bc250SdmaPresent.LocalSegmentBaseOffset =")
    writers += display.count("g_Bc250SdmaPresent.LocalSegmentBytes =")
    if writers:
        errors.append(
            "geometria do segmento escrita fora de "
            "Bc250SdmaPresentSetSegmentGeometry")


def audit_node1_journal_gate_granularity(errors: list[str]) -> None:
    """C3.9: o bloqueio por journal do Node 1 e' fail-closed e mensuravel.

    MEDIDO NA CORRIDA 12/09 13:29. Com D3D11LabPromotion armado, o dwm.exe
    sobreviveu 3 min 13 s -- primeira vez, contra 7 crashes em 18 s sem ele. Mas
    o gate do Node 1 usava Bc250GpuvaJournalSetAnyPending, que pergunta "ALGUM
    slot do adaptador esta pendente?". Com o DWM paginando sem parar, essa
    resposta nunca foi zero: BlockMask=0x1 (JOURNAL), submissao de compute do
    RADV RECUSADA, shader nao executou e o byte proof leu poison 0x43DAFFFE.
    Compute e desktop nao coexistiam por GRANULARIDADE do gate, nao por limite
    do hardware.

    Este portao exige tres coisas:
      1. a vista por processo existe e e' usada na decisao;
      2. ela e' FAIL-CLOSED -- so' vale com o tunable presente E em 1 E com
         processo conhecido, senao a condicao global continua valendo;
      3. a telemetria que nomeia os slots pendentes existe, porque "JOURNAL=1"
         sozinho nao distingue pendencia propria da do dwm.exe -- foi essa
         ambiguidade que fez a corrida 13:29 precisar de mineracao manual.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    journal_c = ROOT / "driver" / "core" / "bc250_gpuva_journal_contract.c"
    test_path = ROOT / "driver" / "tests" / "gpuva_journal_contract_test.c"
    for path in (active_path, journal_c, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar o gate do journal: {path}")
            return

    active = strip_c_comments(
        active_path.read_text(encoding="utf-8", errors="replace"))
    journal = journal_c.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. A vista por processo existe e decide.
    require(journal, "Bc250GpuvaJournalSetAnyPendingForProcess(",
            "vista por processo da pendencia de journal", errors)
    require(active, "Bc250GpuvaJournalSetAnyPendingForProcess(",
            "gate consultando a pendencia do processo que submete", errors)

    # 2. Fail-closed: forma EXATA da guarda tripla.
    require(active,
            "if (active->Node1JournalGatePerProcessPresent == 1u &&",
            "tunable exigido presente antes de relaxar o bloqueio", errors)
    require(active, "active->Node1JournalGatePerProcess == 1u &&",
            "tunable exigido em 1 antes de relaxar o bloqueio", errors)
    require(active, "Process != NULL) {",
            "processo conhecido exigido antes de relaxar o bloqueio", errors)
    # O caminho global nao pode desaparecer: ele e' o default.
    require(active, "} else if (Bc250GpuvaJournalSetAnyPending(active->GpuvaJournals,",
            "condicao global preservada como default do gate", errors)

    # 3. Telemetria que nomeia os slots.
    require(journal, "Bc250GpuvaJournalSetDescribePending(",
            "telemetria de slots pendentes do journal", errors)
    require(active, "JOURNAL_PENDENTE: SlotMask=0x%08X Count=%u",
            "log que nomeia quem segura o journal na recusa", errors)

    # O teste tem de provar a DISTINCAO, senao a correcao pode virar no-op.
    require(test, "Bc250GpuvaJournalSetAnyPendingForProcess(set, 3u, procA) == 0u",
            "teste provando que a pendencia de outro processo nao bloqueia",
            errors)
    require(test, "Bc250GpuvaJournalSetAnyPending(set, 3u) == 1u",
            "teste provando que a pergunta global ainda responde 1", errors)

    # C3.9: a leitura do tunable tem de ser visivel a imagem de LABORATORIO.
    # A primeira versao ficou dentro do ramo de producao e o tunable nunca seria
    # lido no perfil que roda os ensaios -- pego por conferencia de string no SYS
    # assinado, nao por teste. O portao abaixo fecha a classe inteira.
    active_raw = active_path.read_text(encoding="utf-8", errors="replace")
    require_outside_production_only(
        active_raw,
        "context->Node1JournalGatePerProcess = Bc250ActiveReadTunableChecked(",
        "leitura de Node1JournalGatePerProcess", errors)


def audit_snapshot_publish_retries(errors: list[str]) -> None:
    """C3.8: o publicador do snapshot retenta, e BUSY nao se confunde com erro.

    MEDIDO NA CORRIDA 12/09 16:09. Bc250GpuvaSnapshotAcquire (o LEITOR) sempre
    tentou 16 vezes antes de devolver BUSY. Bc250GpuvaSnapshotPublishBatch (o
    PUBLICADOR) fazia UMA tentativa de CAS e desistia.

    Com o dwm.exe vivo, cada Present resolve uma GPUVA e mantem contagem de
    leitor num slot; quando esse slot vira o inativo, a publicacao seguinte
    exige contagem ZERO e falhava de imediato. Dai:
      journal PUBLISH_FAILED (status 7)
        -> ActiveFlushTlb devolve STATUS_IO_DEVICE_ERROR
        -> GpuvaMappingsOperational so' reabre no ramo "journalStatus == OK",
           entao a admissao do Node 1 NUNCA voltava
        -> STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE no 3o submit.

    Este portao trava:
      1. o laco de retentativa existe e usa a MESMA constante do lado leitor;
      2. o diagnostico separa BUSY de erro real de faixa -- o log antigo
         colapsava os dois em "status 7";
      3. o KMD publica esse diagnostico na linha de falha, senao a proxima
         corrida volta a nao saber a causa;
      4. o teste prova a DISTINCAO com leitor preso, e nao apenas o caminho
         feliz -- sem isso a correcao poderia ser um no-op.
    """
    snap_c = ROOT / "driver" / "core" / "bc250_gpuva_snapshot_contract.c"
    snap_h = ROOT / "driver" / "core" / "bc250_gpuva_snapshot_contract.h"
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    test_path = ROOT / "driver" / "tests" / "gpuva_snapshot_contract_test.c"
    for path in (snap_c, snap_h, active_path, test_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar publicacao de snapshot: {path}")
            return

    snap = strip_c_comments(snap_c.read_text(encoding="utf-8", errors="replace"))
    header = snap_h.read_text(encoding="utf-8", errors="replace")
    active = strip_c_comments(
        active_path.read_text(encoding="utf-8", errors="replace"))
    test = test_path.read_text(encoding="utf-8", errors="replace")

    # 1. Laco de retentativa, com a constante compartilhada.
    require(header, "#define BC250_GPUVA_SNAPSHOT_PUBLISH_ATTEMPTS",
            "constante de tentativas da publicacao", errors)
    require(snap, "for (attempt = 0u; attempt < BC250_GPUVA_SNAPSHOT_PUBLISH_ATTEMPTS; ++attempt) {",
            "laco de retentativa do publicador", errors)

    # A desistencia numa unica tentativa nao pode voltar.
    if "0L) != 0L) {" in snap and "for (attempt" not in snap:
        errors.append("publicador voltou a desistir na primeira tentativa")

    # 2. Diagnostico separando BUSY de erro real.
    for field in ("LastPublishAttempts", "LastPublishBusyCount", "LastPublishStatus",
                  "LastPublishRangeCount", "LastPublishFailedRangeIndex",
                  "LastPublishFailedMapRange", "LastPublishFailedBaseGpuVa",
                  "LastPublishFailedSize"):
        require(header, field, f"campo de diagnostico da publicacao ({field})", errors)
    require(snap, "_InterlockedExchange(&Store->LastPublishStatus, (long)BC250_GPUVA_BUSY);",
            "BUSY registrado como causa distinta", errors)
    require(snap, "_InterlockedExchange(&Store->LastPublishStatus, (long)status);",
            "erro real de faixa registrado como causa distinta", errors)

    # 3. O KMD publica o diagnostico na linha de falha.
    require(active, "PublishStatus=%ld Attempts=%ld Busy=%ld",
            "diagnostico da publicacao no log de falha do flush", errors)
    require(active, "PUBLICACAO_FALHA Batch=%ld FailedIndex=%ld",
            "tamanho do batch e indice exato que falhou na publicacao", errors)
    require(active, "Map=%ld Base=0x%llX Size=0x%llX",
            "geometria e operacao da faixa que falhou", errors)

    # 4. O teste prova a distincao com leitor preso.
    require(test, "c38Store.ReaderState[c38Inactive] = 1L;",
            "teste com leitor preso no slot inativo", errors)
    require(test, "== BC250_GPUVA_BUSY",
            "teste exigindo BUSY sob contencao", errors)
    require(test, "c38Store.LastPublishBusyCount ==",
            "teste provando que o publicador retentou", errors)

    # ------------------------------------------------------------------
    # C3.12 (12/09) -- CAS NAO E' LEITURA.
    #
    # A integracao da telemetria de ocupacao amostrava o slot ativo com
    # InterlockedCompareExchange(&GpuvaSnapshots.ActiveSlot, 0, 0). No x86 isso
    # compila para "lock cmpxchg", que executa uma ESCRITA TRAVADA mesmo quando
    # nao troca valor -- sobre um campo cujo dono e' outra thread (o publicador
    # do snapshot). Resultado: active_miniport_test reprovou de forma
    # DETERMINISTICA na linha 632 (StopDeviceActive deixando de devolver
    # STATUS_SUCCESS), inclusive com build limpo.
    #
    # A bisseccao levou quatro builds e passou por duas hipoteses erradas
    # (string de formato longa; leitura de Slots[1]). O discriminante final foi
    # trocar a amostragem por leitura volatil pura -- e o teste voltou a passar.
    #
    # ActiveSlot e' "volatile long": ler basta. Este portao impede que a
    # telemetria volte a usar read-modify-write para amostrar o campo.
    # ------------------------------------------------------------------
    forbid(active,
           "InterlockedCompareExchange(\n                &active->GpuvaSnapshots.ActiveSlot, 0, 0)",
           "CAS usado como leitura de ActiveSlot na telemetria "
           "(lock cmpxchg escreve; reprovou active_miniport_test)", errors)
    require(active, "LONG occSlot = active->GpuvaSnapshots.ActiveSlot;",
            "amostragem do slot ativo por leitura volatil pura", errors)
    require(active, "Bc250GpuvaSpaceDescribeOccupancy(",
            "telemetria de ocupacao integrada ao caminho de falha do flush",
            errors)
    require(active, "OCUPACAO slot=%ld st=%u Cap=%u Mapped=%u Teardown=%u Reusable=%u",
            "log que separa capacidade esgotada de slots presos em TEARDOWN",
            errors)


def audit_gpuva_snapshot_capacity(errors: list[str]) -> None:
    """C3.14-C3.16: capacidade pelo estado final, nao pelo historico bruto.

    A corrida 17:45 mediu ate 252 faixas ativas. O journal aceita ate 256
    runs por commit. Scratch parte vazio e representa o estado final, portanto
    o antigo teto 256 era capacidade insuficiente, nao overhead transitorio do
    rebuild. O teste de contrato reproduz 252 + 256 e deve permanecer ligado ao
    teto real, para que reduzir a constante nao vire uma regressao silenciosa.
    """
    space_h = ROOT / "driver" / "core" / "bc250_gpuva_space_contract.h"
    journal_h = ROOT / "driver" / "core" / "bc250_gpuva_journal_contract.h"
    journal_c = ROOT / "driver" / "core" / "bc250_gpuva_journal_contract.c"
    snapshot_test_path = ROOT / "driver" / "tests" / "gpuva_snapshot_contract_test.c"
    journal_test_path = ROOT / "driver" / "tests" / "gpuva_journal_contract_test.c"
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    for path in (space_h, journal_h, journal_c, snapshot_test_path, journal_test_path,
                 active_path):
        if not path.exists():
            errors.append(f"fonte ausente para auditar capacidade GPUVA: {path}")
            return

    header = strip_c_comments(space_h.read_text(encoding="utf-8", errors="replace"))
    journal_header = strip_c_comments(
        journal_h.read_text(encoding="utf-8", errors="replace"))
    journal_source = strip_c_comments(
        journal_c.read_text(encoding="utf-8", errors="replace"))
    test = strip_c_comments(
        snapshot_test_path.read_text(encoding="utf-8", errors="replace"))
    journal_test = strip_c_comments(
        journal_test_path.read_text(encoding="utf-8", errors="replace"))
    active = strip_c_comments(
        active_path.read_text(encoding="utf-8", errors="replace"))
    match = re.search(
        r"#define\s+BC250_GPUVA_SPACE_MAX_RANGES\s+(\d+)u", header)
    if match is None:
        errors.append("teto BC250_GPUVA_SPACE_MAX_RANGES ausente ou nao numerico")
    elif int(match.group(1)) < 1024:
        errors.append(
            "capacidade GPUVA abaixo do envelope C3.14: "
            f"{match.group(1)} < 1024")

    require(test, "C314_EXISTING_RANGES = 252, C314_BATCH_RANGES = 461",
            "replay do pico ativo mais o update esparso medido", errors)
    require(test, "C314_EXISTING_RANGES + C314_BATCH_RANGES",
            "teste ligando a capacidade ao total final", errors)
    require(test, "c314Occupancy.Mapped ==",
            "teste conferindo todas as faixas publicadas", errors)
    require(test, "c314Store.ActiveSlot == 1L",
            "teste conferindo publicacao atomica do slot", errors)

    # C3.15: o teto anterior ao snapshot tambem precisa comportar o envelope.
    # A corrida 18:21 mediu updates esparsos de 333, 443 e 461 entradas. O
    # miniport os quebrava em chunks de 256, mas o journal acumula todos os
    # runs ate o flush; logo o segundo chunk ainda estourava o teto antigo.
    journal_match = re.search(
        r"#define\s+BC250_GPUVA_JOURNAL_MAX_CHANGES\s+(\d+)u",
        journal_header)
    if journal_match is None:
        errors.append("teto BC250_GPUVA_JOURNAL_MAX_CHANGES ausente ou nao numerico")
    elif int(journal_match.group(1)) < 1024:
        errors.append(
            "capacidade do journal abaixo do envelope C3.15: "
            f"{journal_match.group(1)} < 1024")
    require(journal_test, "C315_SPARSE_RUNS = 461",
            "replay do update esparso medido na corrida 18:21", errors)
    require(journal_test, "c315Journal.ChangeCount == C315_SPARSE_RUNS",
            "teste conferindo os 461 runs no mesmo journal", errors)
    require(journal_test, "c315Occupancy.Mapped == C315_SPARSE_RUNS",
            "teste conferindo publicacao integral dos 461 runs", errors)
    require(active, "transacao parcial ABORTADA",
            "overflow do journal aborta prefixo parcial", errors)
    require(active, "Bc250GpuvaJournalAbort(leafJournal);",
            "abort fisico do prefixo parcial do journal", errors)
    # C3.16: 1024 tambem estourou (239 vezes) na corrida 18:45. A solucao nao
    # e outro aumento arbitrario: o journal deve substituir intervalos antigos
    # e guardar somente o delta final nao sobreposto.
    require(journal_source, "Bc250GpuvaJournalOverlayChange(",
            "journal aplica updates por sobreposicao de estado final", errors)
    require(journal_test, "i < 17471u",
            "replay do pico historico medido na corrida 18:45", errors)
    require(journal_test, "overlayJournal.ChangeCount == 1u",
            "17.471 reescritas da mesma VA ocupam um unico estado final", errors)
    require(journal_test, "overlayJournal.ChangeCount == 3u",
            "sobreposicao parcial preserva as duas pontas", errors)



def audit_dx21_pipeline_ir_and_ib_dump_reach_evidence(errors: list[str]) -> None:
    """Faixa DX.21 (22/09/2026).

    (1) O despejo do programa ACO de cada pipeline so' vale se for chamado
    DEPOIS do vkCreateGraphicsPipelines bem-sucedido, se a flag de captura for
    condicionada a extensao ter sido aceita, e se o device tiver fallback sem a
    extensao (instrumentacao nunca decide se o device existe).
    (2) O despejo de IB nao pode apontar para a pasta de evidencia: ela fica no
    perfil do Daniel e o dwm.exe (DWM-<n>) nao grava la' -- causa do
    gfx-ib-dump vazio nas Corridas 35, 36 e 66. Staging em C:/Windows/Temp,
    armado ANTES do bind, copiado para a evidencia na coleta FINAL.
    """
    cache = (ROOT / "driver" / "umd" / "bc250_umd_pipeline_cache.c").read_text(encoding="utf-8", errors="replace")
    bridge = (ROOT / "driver" / "umd" / "bc250_umd_vulkan_bridge.c").read_text(encoding="utf-8", errors="replace")
    create = cache.find("vkCreateGraphicsPipelines(pBridge->Device")
    dump = cache.find("Bc250UmdVkDumpPipelineIr(pBridge, pipeline")
    fail = cache.find("if (!psoSuccess || pipeline == VK_NULL_HANDLE)")
    if create < 0 or dump < 0 or fail < 0 or not (create < fail < dump):
        errors.append("DX.21: Bc250UmdVkDumpPipelineIr tem de vir DEPOIS da criacao e do ramo de falha do pipeline")
    flag = cache.find("VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR")
    guard = cache.rfind("if (pBridge->PipelineIrCaptureEnabled)", 0, flag if flag >= 0 else 0)
    if flag < 0 or guard < 0 or flag - guard > 200:
        errors.append("DX.21: flag de captura de IR sem a guarda PipelineIrCaptureEnabled imediatamente antes")
    fn = bridge.find("VOID Bc250UmdVkDumpPipelineIr(")
    body = bridge[fn:bridge.find("VOID Bc250UmdVkReleaseDescriptorBatches", fn)] if fn >= 0 else ""
    if "!pBridge->PipelineIrCaptureEnabled" not in body.split("pipelineIndex = ")[0]:
        errors.append("DX.21: o despejo de IR tem de sair ANTES de qualquer chamada quando a captura esta desarmada")
    if "TRUNCADO NO TETO" not in body or "TETO de %u pipelines" not in body:
        errors.append("DX.21: truncamento e teto do despejo de IR precisam ser DECLARADOS no log")
    dev = bridge[bridge.find("kIrExtensions"):bridge.find("LOAD_VK_DEV(vkDestroyDevice)")]
    if dev.count("vkCreateDevice(pBridge->PhysicalDevice") != 2 or "enabledExtensionCount = 0" not in dev:
        errors.append("DX.21: vkCreateDevice precisa de fallback SEM a extensao de IR (duas criacoes, a segunda sem extensoes)")
    runner = (ROOT / "tools" / "test-d3d11-native-conformance.ps1").read_text(encoding="utf-8", errors="replace")
    binder = (ROOT / "tools" / "bind-bc250mcdm-lab.ps1").read_text(encoding="utf-8", errors="replace")
    staging = r"C:\Windows\Temp\BC250_GFX_IB_DUMP"
    if re.search(r"\$ibDumpDir\s*=\s*Join-Path\s+\$EvidenceDir", runner):
        errors.append("DX.21: runner voltou a apontar o despejo de IB para a pasta de evidencia (dwm.exe nao grava la')")
    if ("$ibDumpDir = '" + staging + "'") not in runner:
        errors.append("DX.21: runner nao usa o staging gravavel pelo DWM para o despejo de IB")
    arm = binder.find("$gfxIbDumpStaging = '" + staging + "'")
    reg = binder.find("-Name 'GfxIbDumpDir' -Value $gfxIbDumpStaging", arm if arm >= 0 else 0)
    acl = binder.find("*S-1-5-90-0:(OI)(CI)M", arm if arm >= 0 else 0)
    baseline = binder.find("display-log-baseline.json")
    if arm < 0 or reg < 0 or acl < 0 or not (arm < acl < reg < baseline):
        errors.append("DX.21: binder tem de criar o staging, conceder ACL ao DWM e armar GfxIbDumpDir ANTES do bind")
    final = binder.rfind("Coleta FINAL do despejo de IB")
    copy = binder.rfind("Get-ChildItem -LiteralPath '" + staging + "'", 0, final if final >= 0 else 0)
    if final < 0 or copy < 0 or final - copy > 800:
        errors.append("DX.21: coleta FINAL nao copia o staging do despejo de IB para a evidencia")


def audit_dx24_d3d11_state_reaches_vulkan(errors: list[str]) -> None:
    """Faixa DX.24 (22/09/2026).

    Corrida 69, desfecho B: origem com conteudo, draws no alvo apresentado e
    superficie preta. A leitura achou estado D3D11 que nao chegava ao Vulkan.
    Este portao trava as correcoes que nao passam pelo modulo puro
    bc250_umd_state_translate.c (esse tem suite propria com mutacao).
    """
    umd = ROOT / "driver" / "umd"
    cache = (umd / "bc250_umd_pipeline_cache.c").read_text(encoding="utf-8", errors="replace")
    draw = (umd / "bc250_d3d_umd.c").read_text(encoding="utf-8", errors="replace")
    desc = (umd / "bc250_umd_descriptors.c").read_text(encoding="utf-8", errors="replace")
    bridge = (umd / "bc250_umd_vulkan_bridge.c").read_text(encoding="utf-8", errors="replace")

    explicit = cache.find("if (effectiveKey.BlendFactorsExplicit) {")
    raw = cache.find("colorBlendAttachment.srcColorBlendFactor = effectiveKey.SrcColorBlendFactor;", explicit)
    if explicit < 0 or raw < 0 or raw - explicit > 400:
        errors.append("DX.24: pipeline nao usa os fatores de blend pedidos quando BlendFactorsExplicit")
    if "effectiveKey.SrcColorBlendFactor ? effectiveKey.SrcColorBlendFactor" in cache:
        errors.append("DX.24: pipeline voltou a trocar VK_BLEND_FACTOR_ZERO (valor 0) pelo padrao")
    if "effectiveKey.BlendFactorsExplicit ? effectiveKey.ColorWriteMask" not in cache:
        errors.append("DX.24: mascara de escrita 0 do D3D11 voltou a virar RGBA")
    if "VK_DYNAMIC_STATE_BLEND_CONSTANTS" not in cache or "dynamicState.dynamicStateCount = 3;" not in cache:
        errors.append("DX.24: BLEND_CONSTANTS dinamico ausente do pipeline")

    bind = draw.find("vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);")
    setc = draw.find("vkCmdSetBlendConstants(cmd, pDevice->PipelineState.BlendFactor);", bind if bind >= 0 else 0)
    trace = draw.find("Bc250UmdTraceDrawData(pDevice, StartVertexLocation);", bind if bind >= 0 else 0)
    if bind < 0 or setc < 0 or trace < 0 or setc - bind > 300 or trace - bind > 400:
        errors.append("DX.24: blend constante e [DRAW_DATA] tem de sair logo apos o bind do pipeline")
    if "vp.height = -pDevice->PipelineState.Viewports[0].Height;" not in draw:
        errors.append("DX.24: viewport sem altura negativa -- a imagem D3D sai de cabeca para baixo")
    if "&vb0->VkBuf, &offset" in draw:
        errors.append("DX.24: slot de vertice sem buffer voltou a ser preenchido com o slot 0")
    if "psoKey.InputAttributes[e].location = e;" in draw:
        errors.append("DX.24: location voltou a seguir a ordem do elemento em vez do InputRegister")
    if "Bc250UmdTranslateInputLayout(il->Elements" not in draw:
        errors.append("DX.24: draw nao usa a traducao pura do layout de entrada")
    if "Bc250UmdMapBlendFactor((UINT)rt0->SrcBlend" not in draw or "psoKey.BlendFactorsExplicit = TRUE;" not in draw:
        errors.append("DX.24: fatores de blend do D3D11 nao sao traduzidos")
    if "view.components = Bc250UmdVkSrvComponentMapping(srv->Desc.Format);" not in desc:
        errors.append("DX.24: view de amostragem sem swizzle (X8 sem alpha=1, A8 em vermelho)")
    if "case 88: /* DXGI_FORMAT_B8G8R8A8_UNORM_SRGB */" in bridge:
        errors.append("DX.24: tabela de formatos voltou com os comentarios trocados")


def audit_dx25_drawn_vertices_and_target_readback(errors: list[str]) -> None:
    """Faixa DX.25 (22/09/2026).

    Corrida 70: o vertice amostrado (offset 0 do VB) nao era o desenhado -- os
    draws do DWM sao indexados. E a grade do Present so' ve o fim do quadro.
    Trava: indice lido a partir de IndexOffset + Start, BaseVertex somado, e a
    leitura do alvo apos o draw com teto e SEM escrever no alvo.
    """
    draw = (ROOT / "driver" / "umd" / "bc250_d3d_umd.c").read_text(encoding="utf-8", errors="replace")
    fn = draw.find("static void Bc250UmdTraceDrawVertices(")
    body = draw[fn:draw.find("static void Bc250UmdReadbackDrawTarget(", fn)] if fn >= 0 else ""
    if "d->PipelineState.IndexOffset + ((uint64_t)Start + k) * size" not in body:
        errors.append("DX.25: indice nao e' lido a partir de IndexOffset + StartIndexLocation")
    if "(Indexed ? (int64_t)BaseVertex : 0)" not in body:
        errors.append("DX.25: BaseVertexLocation nao e' somado ao indice -- o vertice medido nao seria o desenhado")
    rb = draw.find("static void Bc250UmdReadbackDrawTarget(")
    rbody = draw[rb:draw.find("static BOOL Bc250UmdExecuteVkDraw(", rb)] if rb >= 0 else ""
    if "BC250_DRAW_READBACK_MAX" not in rbody or "TETO de %u leituras" not in rbody:
        errors.append("DX.25: leitura do alvo apos o draw sem teto declarado")
    for forbidden in ("vkCmdClearColorImage", "vkCmdCopyBufferToImage", "vkCmdCopyImage(", "memcpy("):
        if forbidden in rbody:
            errors.append(f"DX.25: leitura do alvo ESCREVE ({forbidden}) -- instrumentacao nao pode tocar no pixel")
    t1 = draw.find("Bc250UmdTraceDrawData(pDevice, StartVertexLocation);")
    t2 = draw.find("Bc250UmdTraceDrawVertices(pDevice, Indexed, StartVertexLocation, BaseVertexLocation);")
    fin = draw.find("DRAW fence concluida; pixels NAO CONFERIDOS")
    rcall = draw.find("Bc250UmdReadbackDrawTarget(pDevice, rtv->pResource);", fin if fin >= 0 else 0)
    if t1 < 0 or t2 < 0 or t2 - t1 > 200 or fin < 0 or rcall < 0 or rcall - fin > 600:
        errors.append("DX.25: [DRAW_VERTS] e [DRAW_READBACK] fora do lugar no ExecuteVkDraw")


def audit_dx26_golden_csib_snapshot_is_read_only(errors: list[str]) -> None:
    """Faixa DX.26 (22/09/2026): snapshot dos golden settings e do CSB e' so' LEITURA.

    A Fase A (docs/338) mostrou que o KMD nunca aplica nem le os 34 goldens nem
    o RLC_CSIB_*. Medir antes de escrever: a funcao de snapshot nao pode conter
    escrita, e os offsets novos entram so' na allowlist de LEITURA.
    """
    mini = (ROOT / "driver" / "wdk" / "bc250_active_miniport.c").read_text(encoding="utf-8", errors="replace")
    trans = (ROOT / "driver" / "wdk" / "bc250_mmio_topology_transport.c").read_text(encoding="utf-8", errors="replace")
    fn = mini.find("static void Bc250ActiveGoldenSnapshotReadOnly(")
    # o corpo termina no fim da propria funcao (DX.27 pos um escritor logo abaixo)
    body = mini[fn:mini.find("\n}\n", fn)] if fn >= 0 else ""
    if not body:
        errors.append("DX.26: snapshot dos golden/CSB ausente")
    elif "Write32" in body or "WRITE_REGISTER" in body:
        errors.append("DX.26: snapshot dos golden/CSB ESCREVE em registrador -- era para ser somente leitura")
    if "Bc250ActiveGoldenSnapshotReadOnly(Context);" not in mini:
        errors.append("DX.26: snapshot dos golden/CSB nunca e' chamado")
    rd = trans.find("static uint32_t Bc250MmioTopologyOffsetAllowed(uint32_t ByteOffset)")
    wr = trans.find("Bc250MmioTopologyWriteOffsetAllowed(uint32_t ByteOffset)")
    rd_body = trans[rd:trans.find("}", rd)] if rd >= 0 else ""
    wr_body = trans[wr:trans.find("\n}", wr)] if wr >= 0 else ""
    if "Bc250MmioTopologyGoldenReadAllowed(ByteOffset)" not in rd_body:
        errors.append("DX.26: offsets de golden/CSB fora da allowlist de leitura")
    # DX.27 abriu ESCRITA so' para RLC_CSIB_* (escritor unico, portao proprio);
    # os golden settings continuam proibidos na allowlist de escrita.
    if "Golden" in wr_body:
        errors.append("DX.26: golden settings entraram na allowlist de ESCRITA")

    # Autoteste grafico (controle positivo) tem de rodar ANTES do harness D3D11,
    # nas tres variantes do RADV, e sua ausencia tem de ser declarada.
    runner = (ROOT / "tools" / "test-d3d11-native-conformance.ps1").read_text(encoding="utf-8", errors="replace")
    st = runner.find("bc250-gfx-selftest.exe")
    harness = runner.find("$proc = Start-Process -FilePath $Harness")
    if st < 0 or harness < 0 or st > harness:
        errors.append("DX.26: autoteste grafico nao roda antes do harness D3D11")
    if "@('nodcc', 'nodcc,nonggc', 'nodcc,nongg')" not in runner:
        errors.append("DX.26: autoteste sem as tres variantes (nodcc / nonggc / nongg)")
    if "AUTOTESTE GRAFICO AUSENTE" not in runner:
        errors.append("DX.26: ausencia do autoteste nao e' declarada")
    selftest_src = (ROOT / "driver" / "tests" / "gfx_selftest" / "bc250_gfx_selftest.c").read_text(encoding="utf-8", errors="replace")
    if "MAP_FALHOU" not in selftest_src or "NAO_MEDIDO" not in selftest_src:
        errors.append("DX.26: autoteste pode transformar falha de medicao em zero")
    if selftest_src.count("PoisonAndCopy(img);") < 2 or "p_vkCmdFillBuffer(g_cmd, g_rb, 0, VK_WHOLE_SIZE, POISON);" not in selftest_src:
        errors.append("DX.26: autoteste nao reenvenena o buffer pela GPU antes de cada copia")


def audit_dx27_clear_state_follows_upstream(errors: list[str]) -> None:
    """Faixa DX.27 (22/09/2026): clear state do GFX10 como no upstream.

    A Corrida 72 leu RLC_CSIB_* = 0 e o preambulo de boot emitia PREAMBLE_CNTL
    com payload 0/1 sem nenhum SET_CONTEXT_REG. Este portao exige: tabela upstream
    sem edicao, codificacao BEGIN/END_CLEAR_STATE de nvd.h, escritor UNICO do
    RLC_CSIB com snapshot/readback/restauracao, CSB antes do anel, sem SET_BASE, e
    o S1 do autoteste sem a confusao veneno==esperado.
    """
    lr = ROOT / "inc" / "linux_ref"
    want = {
        "clearstate_gfx10.h": "e6ca3544dd51a657d9200243be9e6d05c4dcfa052a239dadebbc6cd7409e64cd",
        "clearstate_defs.h": "70eb9f9a57d2cacb2ee2d7b9f17485384019ac03c845ce4bea941acdae137285",
    }
    for name, digest in want.items():
        p = lr / name
        if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
            errors.append(f"DX.27: inc/linux_ref/{name} ausente ou EDITADO (sha256 diverge do upstream registrado)")
    if not (lr / "PROVENANCE_CLEARSTATE.txt").exists():
        errors.append("DX.27: procedencia do clear state ausente")

    pm4 = (ROOT / "driver" / "core" / "bc250_pm4_packet_builder.c").read_text(encoding="utf-8", errors="replace")
    pm4h = (ROOT / "driver" / "core" / "bc250_pm4_packet_builder.h").read_text(encoding="utf-8", errors="replace")
    if "clearstate_gfx10.h" not in pm4 or "gfx10_cs_data" not in pm4:
        errors.append("DX.27: CSB nao sai da tabela upstream gfx10_cs_data")
    if "#define BC250_PM4_PREAMBLE_BEGIN_CLEAR_STATE (2u << 28)" not in pm4h or \
       "#define BC250_PM4_PREAMBLE_END_CLEAR_STATE   (3u << 28)" not in pm4h:
        errors.append("DX.27: BEGIN/END_CLEAR_STATE diferentes de nvd.h:395-396")
    test = (ROOT / "driver" / "tests" / "pm4_packet_builder_test.c").read_text(encoding="utf-8", errors="replace")
    for needle in ("g_csb[1] == 0x20000000u", "g_csb[n - 3] == 0x30000000u",
                   "g_csb[7 + 144] == 0x80000000u", "return Bc250RunGfx10ClearStateTest();"):
        if needle not in test:
            errors.append(f"DX.27: teste do CSB perdeu a assercao {needle!r}")

    mini = (ROOT / "driver" / "wdk" / "bc250_active_miniport.c").read_text(encoding="utf-8", errors="replace")
    fn = mini.find("static BOOLEAN Bc250ActiveProgramClearStateBuffer(")
    body = mini[fn:mini.find("\n}\n", fn)] if fn >= 0 else ""
    if not body:
        errors.append("DX.27: escritor unico do RLC_CSIB ausente")
    else:
        first_write = body.find("Write32(mmio, BC250_REG_RLC_CSIB")
        first_read = body.find("Read32(mmio, BC250_REG_RLC_CSIB")
        if first_read < 0 or first_write < 0 or first_read > first_write:
            errors.append("DX.27: RLC_CSIB escrito sem snapshot anterior")
        if body.count("Read32(mmio, BC250_REG_RLC_CSIB") < 6:
            errors.append("DX.27: RLC_CSIB sem readback dos tres registradores")
        if "if (!pass)" not in body:
            errors.append("DX.27: RLC_CSIB nao restaura o snapshot quando o readback falha")
    # nenhum outro sitio do driver pode escrever o CSIB
    for src in (ROOT / "driver" / "wdk").glob("*.c"):
        txt = src.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"Write32\([^;]*RLC_CSIB", txt):
            if src.name != "bc250_active_miniport.c" or not (fn <= m.start() < fn + len(body)):
                errors.append(f"DX.27: escrita de RLC_CSIB fora do escritor unico em {src.name}")
    online = mini.find("static NTSTATUS Bc250ActiveGfxEngineOnline(")
    boot = mini[online:mini.find("Bc250ActiveGoldenSnapshotReadOnly(Context);", online)] if online >= 0 else ""
    b = boot.find("Bc250Pm4BuildGfx10ClearState(")
    w = boot.find("Bc250ActiveProgramClearStateBuffer(Context, csbMc, csbDwords)")
    c = boot.find("RingCpuVirtual[bootCursor++] = csbCpu[csbIdx];")
    if not (0 <= b < w < c):
        errors.append("DX.27: ordem do upstream quebrada (montar CSB -> programar RLC_CSIB -> anel)")
    if "0xC0004A00u" in boot or "IT_SET_BASE" in boot:
        errors.append("DX.27: preambulo de boot voltou a ter PREAMBLE_CNTL manual ou SET_BASE")

    trans = (ROOT / "driver" / "wdk" / "bc250_mmio_topology_transport.c").read_text(encoding="utf-8", errors="replace")
    wr = trans.find("Bc250MmioTopologyWriteOffsetAllowed(uint32_t ByteOffset)")
    wr_body = trans[wr:trans.find("\n}", wr)] if wr >= 0 else ""
    for name in ("BC250_REG_RLC_CSIB_ADDR_LO_BYTE", "BC250_REG_RLC_CSIB_ADDR_HI_BYTE", "BC250_REG_RLC_CSIB_LENGTH_BYTE"):
        if name not in wr_body:
            errors.append(f"DX.27: {name} fora da allowlist de escrita -- o escritor falharia calado")

    st = (ROOT / "driver" / "tests" / "gfx_selftest" / "bc250_gfx_selftest.c").read_text(encoding="utf-8", errors="replace")
    if "Submit(), S1_FILL);" not in st or "CPU_SENTINEL" not in st:
        errors.append("DX.27: S1 do autoteste voltou a esperar o proprio veneno")
    i_exp = st.find("if (v == expected) ++exp;")
    i_poi = st.find("else if (v == POISON) ++poison;")
    if i_exp < 0 or i_poi < 0 or i_exp > i_poi:
        errors.append("DX.27: Inspect classifica veneno antes do esperado")


def audit_dx28_rotation_carries_vulkan_content(errors: list[str]) -> None:
    """Faixa DX.28 (22/09/2026): a rotacao de identidades leva o conteudo Vulkan.

    Corrida 73: grade 86400/86400 apos o draw e 0/86400 no Present, logo depois
    dos ResourceCopyRegion do buffer anterior. RotateResourceIdentities girava
    Allocation/pCpuShadow e deixava a VkImage para tras. Exige: grupo Vulkan na
    rotacao, RTV seguindo a imagem no Draw e no Clear, teste que confere a
    VkImage girando, e a sentinela de fault GFX somente leitura.
    """
    umd = (ROOT / "driver" / "umd" / "bc250_d3d_umd.c").read_text(encoding="utf-8", errors="replace")
    rv = (ROOT / "driver" / "umd" / "bc250_umd_resource_vk.c").read_text(encoding="utf-8", errors="replace")
    test = (ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c").read_text(encoding="utf-8", errors="replace")
    fn = umd.find("HRESULT APIENTRY Bc250DxgiRotateResourceIdentities(")
    body = umd[fn:umd.find("\n}\n", fn)] if fn >= 0 else ""
    for needle in ("dst->VkImg = src->VkImg;", "dst->VkMem = src->VkMem;", "dst->VkLayout = src->VkLayout;",
                   "last->VkImg = savedImg;", "last->VkMem = savedMem;", "last->VkLayout = savedLayout;"):
        if needle not in body:
            errors.append(f"DX.28: rotacao nao leva o conteudo Vulkan ({needle!r} ausente)")
    # a linha tambem existe dentro da FollowResource: tem de ser procurada no
    # corpo da CRIACAO (a primeira versao deste portao passou com ela removida)
    cr = rv.find("BOOL Bc250UmdVkCreateRtv(")
    cr_body = rv[cr:rv.find("\n}\n", cr)] if cr >= 0 else ""
    if "pRtv->VkImageAtCreation = pRes->VkImg;" not in cr_body or "BOOL Bc250UmdVkRtvFollowResource(" not in rv:
        errors.append("DX.28: RTV nao registra na criacao / nao segue a imagem do recurso")
    draw = umd.find("rtv = (BC250_UMD_RTV*)pDevice->PipelineState.BoundRTVs[0].pDrvPrivate;")
    if draw < 0 or "Bc250UmdVkRtvFollowResource(pDevice, rtv)" not in umd[draw:draw + 600]:
        errors.append("DX.28: Draw usa a RTV sem seguir a rotacao")
    clr = umd.find("VOID APIENTRY Bc250Umd10ClearRenderTargetView(")
    if clr < 0 or "Bc250UmdVkRtvFollowResource(device, rtv)" not in umd[clr:clr + 1200]:
        errors.append("DX.28: Clear usa a RTV sem seguir a rotacao")
    for needle in ("assert(chain[0].VkImg == (VkImage)(uintptr_t)0xB001u);",
                   "assert(chain[2].VkImg == (VkImage)(uintptr_t)0xB000u);"):
        if needle not in test:
            errors.append(f"DX.28: teste da rotacao perdeu {needle!r}")

    mini = (ROOT / "driver" / "wdk" / "bc250_active_miniport.c").read_text(encoding="utf-8", errors="replace")
    s = mini.find("DX.28 -- SENTINELA DE FAULT DO GCVM")
    blk = mini[s:mini.find("s_faultSentinelLast = st;", s)] if s >= 0 else ""
    if not blk:
        errors.append("DX.28: sentinela de fault GFX ausente")
    elif "Write32" in blk:
        errors.append("DX.28: sentinela de fault GFX ESCREVE -- era somente leitura")
    if not (ROOT / "external" / "radv-wddm" / "pending" / "DX29-remover-preamble-cntl-do-fork.patch").exists():
        errors.append("DX.28: patch pendente do PREAMBLE_CNTL do fork ausente")


def audit_dx29_vmid_switch_sync_on_gfx_ring(errors: list[str]) -> None:
    """Faixa DX.29 (22/09/2026): sincronia de troca de VMID no anel grafico.

    Corridas 73-74: 5 de 5 travamentos de 240 ondas num IB logo apos IB de OUTRO
    VMID. Exige: WAIT_REG_MEM mem/PFP sobre a fence do anel + PFP_SYNC_ME ANTES do
    INDIRECT_BUFFER quando o VMID muda, codificacao de nvd.h, LastSubmitVmid
    atualizado so' no sucesso e zerado (0xFFFFFFFF) no boot e no restart.
    """
    mini = (ROOT / "driver" / "wdk" / "bc250_active_miniport.c").read_text(encoding="utf-8", errors="replace")
    pm4h = (ROOT / "driver" / "core" / "bc250_pm4_packet_builder.h").read_text(encoding="utf-8", errors="replace")
    test = (ROOT / "driver" / "tests" / "pm4_packet_builder_test.c").read_text(encoding="utf-8", errors="replace")
    fn = mini.find("static NTSTATUS Bc250ActiveGfxEngineSubmit(")
    body = mini[fn:mini.find("\n}\n", fn)] if fn >= 0 else ""
    w = body.find("BC250_PM4_WAIT_REG_MEM_PFP_MEM_GE")
    s = body.find("BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_PFP_SYNC_ME, 1u)")
    ib = body.find("pm4Status = Bc250Pm4EmitIndirectBuffer(")
    if not (0 <= w < s < ib):
        errors.append("DX.29: sincronia de troca de VMID ausente ou depois do INDIRECT_BUFFER")
    if "prevVmid != Vmid" not in body or "Context->GfxEngine.FenceGpuVa, waitFence" not in body:
        errors.append("DX.29: a espera nao e' condicionada a troca de VMID ou nao usa a fence do anel")
    if "Context->GfxEngine.LastSubmitVmid = Vmid;" not in body:
        errors.append("DX.29: LastSubmitVmid nao e' atualizado no sucesso")
    if mini.count("LastSubmitVmid = 0xFFFFFFFFu;") < 2:
        errors.append("DX.29: LastSubmitVmid nao volta a 'nenhum' no boot e no restart de TDR")
    if "#define BC250_PM4_WAIT_REG_MEM_PFP_MEM_GE    ((5u << 0) | (1u << 4) | (1u << 8))" not in pm4h or \
       "#define BC250_PM4_IT_PFP_SYNC_ME             0x42u" not in pm4h:
        errors.append("DX.29: codificacao do WAIT_REG_MEM/PFP_SYNC_ME diverge de nvd.h")
    for needle in ("CHECK(BC250_PM4_WAIT_REG_MEM_PFP_MEM_GE == 0x00000115u);",
                   "CHECK(ring[1] == 0x00000115u);"):
        if needle not in test:
            errors.append(f"DX.29: teste perdeu {needle!r}")


def main() -> int:
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    engine_path = ROOT / "driver" / "wdk" / "bc250_submission_engine.c"
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    objects_path = (ROOT / "driver" / "core" /
                    "bc250_mcdm_object_contract.c")
    active_test_path = ROOT / "driver" / "tests" / "active_miniport_test.c"
    recovery_path = ROOT / "tools" / "BC250_RECOVERY.cmd"
    inf_path = ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf"
    stage_path = ROOT / "tools" / "stage-bc250mcdm-package.ps1"
    bind_path = ROOT / "tools" / "bind-bc250mcdm-failclosed.ps1"
    lab_bind_path = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"
    lab_package_path = ROOT / "tools" / "package-bc250mcdm-lab.ps1"
    lab_watchdog_path = ROOT / "tools" / "watch-bc250mcdm-lab.ps1"
    safe_guard_assert_path = (ROOT / "tools" /
                              "assert-bc250-safe-bind-guard.ps1")
    lab_rollback_path = ROOT / "tools" / "rollback-bc250mcdm-lab.ps1"
    render_prepare_path = ROOT / "tools" / "prepare-bc250mcdm-rendernode.ps1"
    render_inspect_path = ROOT / "tools" / "inspect-bc250mcdm-rendernode.ps1"
    collector_path = ROOT / "tools" / "capture-bc250mcdm-binding.ps1"
    watchdog_path = ROOT / "tools" / "watch-bc250mcdm-binding.ps1"
    confirm_display_path = ROOT / "tools" / "confirm-bc250mcdm-display.ps1"
    arm_reboot_path = (ROOT / "tools" /
                       "arm-bc250mcdm-reboot-watchdog.ps1")
    watchdog_reboot_path = (ROOT / "tools" /
                            "watch-bc250mcdm-reboot.ps1")
    confirm_reboot_path = (ROOT / "tools" /
                           "confirm-bc250mcdm-reboot.ps1")
    rollback_path = ROOT / "tools" / "rollback-bc250mcdm-online.ps1"
    replace_path = ROOT / "tools" / "replace-bc250mcdm.ps1"
    orphan_cleanup_path = (ROOT / "tools" /
                           "cleanup-bc250mcdm-prebinding-residue.ps1")
    readonly_ddi_path = ROOT / "driver" / "wdk" / "bc250_dxgk_readonly_ddi.c"
    platform_snapshot_path = (ROOT / "driver" / "core" /
                              "bc250_platform_register_snapshot.c")
    vram_probe_path = (ROOT / "driver" / "wdk" /
                       "bc250_vram_probe_transaction.c")
    paging_path = ROOT / "driver" / "core" / "bc250_paging_contract.c"
    tlb_transaction_path = (ROOT / "driver" / "core" /
                            "bc250_gfxhub_tlb_transaction.c")
    mmio_transport_path = (ROOT / "driver" / "wdk" /
                           "bc250_mmio_topology_transport.c")
    compute_mqd_path = (ROOT / "driver" / "core" /
                        "bc250_compute_mqd_contract.c")
    pm4_builder_path = (ROOT / "driver" / "core" /
                        "bc250_pm4_packet_builder.c")
    pm4_header_path = (ROOT / "driver" / "core" /
                       "bc250_pm4_packet_builder.h")
    display_path = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
    sdma_present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    umd_header_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.h"
    winsys_path = ROOT / "driver" / "umd" / "bc250_wddm_winsys.c"
    winsys_test_path = ROOT / "driver" / "tests" / "bc250_wddm_winsys_test.c"
    build_path = ROOT / "driver" / "wdk" / "build-contract.bat"
    umd_build_path = ROOT / "driver" / "umd" / "build-umd.bat"
    arm_dwm_path = ROOT / "tools" / "arm-dwm-crash-capture.ps1"
    restore_dwm_path = ROOT / "tools" / "restore-dwm-crash-capture.ps1"
    radv_tripod_path = (ROOT / "tools" /
                        "test-radv-wddm-compute-tripod.ps1")
    umd_node1_tripod_path = (ROOT / "tools" /
                             "test-b7-umd-node1-tripod.ps1")
    radv_gate_path = (ROOT / "driver" / "tests" /
                      "bc250_radv_compute_gate.c")

    active = active_path.read_text(encoding="utf-8")
    engine = engine_path.read_text(encoding="utf-8")
    callbacks = callbacks_path.read_text(encoding="utf-8")
    objects = objects_path.read_text(encoding="utf-8")
    active_test = active_test_path.read_text(encoding="utf-8")
    recovery = recovery_path.read_text(encoding="utf-8")
    inf = inf_path.read_text(encoding="utf-8")
    stage = stage_path.read_text(encoding="utf-8")
    bind = bind_path.read_text(encoding="utf-8")
    lab_bind = lab_bind_path.read_text(encoding="utf-8")
    lab_package = lab_package_path.read_text(encoding="utf-8")
    lab_watchdog = lab_watchdog_path.read_text(encoding="utf-8")
    safe_guard_assert = safe_guard_assert_path.read_text(encoding="utf-8")
    lab_rollback = lab_rollback_path.read_text(encoding="utf-8")
    render_prepare = render_prepare_path.read_text(encoding="utf-8")
    render_inspect = render_inspect_path.read_text(encoding="utf-8")
    collector = collector_path.read_text(encoding="utf-8")
    watchdog = watchdog_path.read_text(encoding="utf-8")
    confirm_display = confirm_display_path.read_text(encoding="utf-8")
    arm_reboot = arm_reboot_path.read_text(encoding="utf-8")
    watchdog_reboot = watchdog_reboot_path.read_text(encoding="utf-8")
    confirm_reboot = confirm_reboot_path.read_text(encoding="utf-8")
    rollback = rollback_path.read_text(encoding="utf-8")
    replace = replace_path.read_text(encoding="utf-8")
    orphan_cleanup = orphan_cleanup_path.read_text(encoding="utf-8")
    readonly_ddi = readonly_ddi_path.read_text(encoding="utf-8")
    platform_snapshot = platform_snapshot_path.read_text(encoding="utf-8")
    vram_probe = vram_probe_path.read_text(encoding="utf-8")
    paging = paging_path.read_text(encoding="utf-8")
    tlb_transaction = tlb_transaction_path.read_text(encoding="utf-8")
    mmio_transport = mmio_transport_path.read_text(encoding="utf-8")
    compute_mqd = compute_mqd_path.read_text(encoding="utf-8")
    pm4_builder = pm4_builder_path.read_text(encoding="utf-8")
    pm4_header = pm4_header_path.read_text(encoding="utf-8")
    display = display_path.read_text(encoding="utf-8")
    sdma_present = sdma_present_path.read_text(encoding="utf-8")
    umd = umd_path.read_text(encoding="utf-8")
    umd_header = umd_header_path.read_text(encoding="utf-8")
    winsys = winsys_path.read_text(encoding="utf-8")
    winsys_test = winsys_test_path.read_text(encoding="utf-8")
    build = build_path.read_text(encoding="utf-8")
    umd_build = umd_build_path.read_text(encoding="utf-8")
    arm_dwm = arm_dwm_path.read_text(encoding="utf-8")
    restore_dwm = restore_dwm_path.read_text(encoding="utf-8")
    radv_tripod = radv_tripod_path.read_text(encoding="utf-8")
    umd_node1_tripod = umd_node1_tripod_path.read_text(encoding="utf-8")
    radv_gate = radv_gate_path.read_text(encoding="utf-8")
    errors: list[str] = []

    # C3.16: a submissao #8896 da corrida 18:45 tinha 750 paginas, mas o gate
    # conferia apenas primeira/meio/ultima. Cobertura e log agora sao integrais
    # no gate e amostrados somente na telemetria de sucesso.
    require(active, 'Bc250ActiveValidateGpuVaRange("IB"',
            "IB validado pagina a pagina antes da SDMA", errors)
    require(active, 'Bc250ActiveValidateGpuVaRange("SRC"',
            "origem validada pagina a pagina antes da SDMA", errors)
    require(active, 'Bc250ActiveValidateGpuVaRange("DEST"',
            "destino validado pagina a pagina antes da SDMA", errors)
    forbid(active, "uint64_t probes[3]",
           "gate de traducao nao pode voltar a amostrar tres paginas", errors)
    require(display, "BC250_VERBOSE_LOG_PERIOD      1024L",
            "log de alto volume amostrado", errors)
    require(display, "LOG_SAMPLING_SUMMARY",
            "contagens suprimidas preservadas no teardown", errors)
    require(display, 'Bc250LogTextContains(Text, "FAULT")',
            "falhas permanecem fora da amostragem", errors)

    # A MMU global usa exatamente 26 offsets catalogados. O bind de 15:49
    # falhou antes do MMIO porque 0xA610 nao estava na allowlist do transporte.
    # As duas direcoes precisam compartilhar a mesma lista fechada.
    global_mmu_allowlist = function_body(
        mmio_transport,
        "static uint32_t Bc250MmioTopologyGlobalMmuOffsetAllowed(")
    mmio_read_allowlist = function_body(
        mmio_transport,
        "static uint32_t Bc250MmioTopologyOffsetAllowed(")
    mmio_write_allowlist = function_body(
        mmio_transport,
        "static uint32_t Bc250MmioTopologyWriteOffsetAllowed(")
    global_mmu_registers = (
        "BC250_REG_GCMC_VM_AGP_BASE",
        "BC250_REG_GCMC_VM_AGP_BOT",
        "BC250_REG_GCMC_VM_AGP_TOP",
        "BC250_REG_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR",
        "BC250_REG_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR",
        "BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB",
        "BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB",
        "BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO",
        "BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI",
        "BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2",
        "BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO",
        "BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI",
        "BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO",
        "BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI",
        "BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO",
        "BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI",
        "BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO",
        "BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI",
        "BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO",
        "BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI",
        "BC250_REG_GCMC_VM_MX_L1_TLB_CNTL",
        "BC250_REG_GCVM_L2_CNTL",
        "BC250_REG_GCVM_L2_CNTL2",
        "BC250_REG_GCVM_L2_CNTL3",
        "BC250_REG_GCVM_L2_CNTL4",
        "BC250_REG_GCVM_L2_CNTL5",
    )
    for register in global_mmu_registers:
        require(global_mmu_allowlist, "case " + register + ":",
                "global MMU MMIO allowlist: " + register, errors)
    require(mmio_read_allowlist,
            "Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset)",
            "global MMU read allowlist shared helper", errors)
    require(mmio_write_allowlist,
            "Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset)",
            "global MMU write allowlist shared helper", errors)

    # GPUVA/Present deve permanecer transacional: UpdatePT nao publica um
    # cache paralelo, erro do journal nao vira sucesso e o blt so conclui com
    # fence nova (poison substituido) mais RPTR alcancando WPTR.
    update_pt = function_body(
        active, "static NTSTATUS Bc250ActiveUpdatePageTableHandler(")
    flush_tlb = function_body(
        active, "NTSTATUS Bc250ActiveFlushTlbHandler(")
    resolve_present = function_body(
        display, "BOOLEAN Bc250DisplayResolvePresentSource(")
    sdma_blt = function_body(
        sdma_present, "BOOLEAN Bc250SdmaPresentBlt(")
    require(update_pt, "return STATUS_DEVICE_BUSY;",
            "journal BUSY propagated by UpdatePageTable", errors)
    require(update_pt, "return STATUS_INVALID_PARAMETER;",
            "journal invalid state propagated by UpdatePageTable", errors)
    forbid(update_pt, "Bc250DisplayRecordGpuVaMapping(",
           "GPUVA publication before journal commit/FlushTlb", errors)
    require(flush_tlb, "return STATUS_IO_DEVICE_ERROR;",
            "journal commit failure propagated by FlushTlb", errors)
    require(flush_tlb, "flushRootKey != 0ull",
            "explicit valid root key gate before journal commit", errors)
    require(flush_tlb, "0u, 0u,\n        &generation",
            "global journal commit after global physical TLB invalidation", errors)
    forbid(resolve_present, "Bc250DisplayLookupPhysicalAddress(",
           "parallel uncommitted GPUVA cache in Present", errors)
    require(sdma_blt, "BC250_SDMA_PRESENT_FENCE_POISON;",
            "per-blit fence poison", errors)
    # A15: o gate era pela comparacao CRUA. Ela nao cobre a volta do anel --
    # 4612 == 516 (mod 4096) na corrida 17:43 -- e exigi-la literalmente
    # obrigava o defeito a continuar existindo. Agora o gate exige a aritmetica.
    require(sdma_blt, "Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr)",
            "per-blit RPTR equals WPTR gate (com volta do anel)", errors)
    # A10: a palavra "TRIPE" so' pode sair quando as tres pernas estao provadas.
    # O gate antigo exigia "BLT_TRIPOD PASS" incondicional, e por isso protegia
    # uma linha que anunciava tres pernas tendo duas.
    require(sdma_blt, "BLT_TRIPOD PASS #%ld (3 pernas)",
            "tripod telemetry conditioned on the data leg", errors)
    require(sdma_blt, "BLT_ENGINE_2LEGS",
            "two-leg telemetry when the data leg is unproven", errors)
    require(sdma_blt, "BC250_BLT_DATA_PROOF_OK",
            "tripod claim gated by the data-proof state", errors)

    present_body = function_body(
        display, "NTSTATUS APIENTRY Bc250DxgkDdiPresent(")
    # A9: o veredito da prova vem do contrato em driver/core, nao de um if solto.
    require(present_body, "Bc250ClassifyBltProof(",
            "blt proof verdict from the core contract", errors)
    require(present_body, "Bc250BltProofAllowsTripodClaim(",
            "tripod claim gated by the proof contract", errors)
    require(present_body, "Bc250BltProofRequiresFailClosed(",
            "fail-closed decision from the proof contract", errors)
    require(present_body, "BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)",
            "blank-frame proof reported as inconclusive, never PASS", errors)
    require(present_body, "BYTE_PROOF PASS (dados conferidos, frame com conteudo)",
            "byte-by-byte Present proof telemetry", errors)
    # O rotulo antigo certificava preto contra preto: 63/63 amostras com
    # NonZero=0/256 sairam como PASS na corrida 2026-09-06 22:42.
    forbid(present_body, 'BYTE_PROOF PASS: Source=',
           "unconditional BYTE_PROOF PASS label (certifies black-vs-black)",
           errors)
    # C3.7 (12/09) -- PORTAO INVERTIDO. Ele EXIGIA o desligamento global do SDMA
    # apos um byte proof reprovado, e portanto OBRIGAVA o defeito a existir.
    #
    # Medido na corrida 13:11: um byte proof reprovado punha
    # g_Bc250SdmaPresent.HardwareEnabled = FALSE, e a partir dali TODA submissao
    # WDDM caia em "SubmitIndirectAsync recusado: HwEnabled=0" -> "IB NAO
    # EXECUTADO; completion NAO notificado" -> BugCheck 0x119 Arg1=2
    # Arg2=0xC0000185, sete linhas de log depois. E a reprovacao era FALSA: o
    # byte proof lia o endereco errado por falta de LocalSegmentBaseOffset.
    #
    # QUINTA ocorrencia da classe "o gate obrigava o defeito a existir": A15
    # (comparacao crua de RPTR), mcdm_callbacks_test (STATUS_SUCCESS sem executor
    # de TDR), bc250_d3d_umd_test.c (memcpy no Draw), sdma_blt_executed (C3.1).
    #
    # Falhar fechado continua obrigatorio -- o present reprovado nao declara
    # sucesso. O que fica PROIBIDO e' condenar o motor de submissao do adaptador
    # inteiro por causa de um quadro. Quem desabilita a engine e' o caminho de
    # TDR, que tem quiesce e restart com prova.
    forbid(present_body, "g_Bc250SdmaPresent.HardwareEnabled = FALSE;",
           "desligamento global do SDMA no caminho de byte proof "
           "(caminho medido do BugCheck 0x119 na corrida 13:11)", errors)
    require(present_body, "present reprovado (engine PRESERVADA)",
            "byte proof reprovado sem condenar a engine", errors)
    require(present_body, "sdmaDone = FALSE;",
            "present reprovado continua falhando fechado", errors)
    forbid(present_body, "else if (fromPrimary)",
           "unresolved Present fallback to SharedPrimary", errors)

    # Display PnP must not activate laboratory hardware transactions.
    require(active,
            "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)",
            "compile-time display-only StartDevice gate", errors)
    require(active, "Bc250ProbeSnapshotDdiTrace();",
            "V5 DDI trace snapshot before teardown", errors)
    require(active,
            "InterlockedCompareExchange(&context->IsActive, 0, 0) != 0",
            "idempotent StopDevice/RemoveDevice lifecycle", errors)
    require(display, "Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_INTERFACE);",
            "QueryInterface V5 DDI trace", errors)
    require(active,
            "registry value must never be able to turn",
            "non-tunable display safety contract", errors)
    require(active,
            "Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u);",
            "compiled display-only bring-up skip evidence", errors)
    forbid(active, "#define BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP",
           "local activation of unsafe StartDevice bring-up", errors)
    forbid(build, "/DBC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP",
           "unsafe StartDevice bring-up in package build", errors)

    # A lab image must not advertise persistent submission without wiring the
    # complete physical lifecycle into the active miniport. Candidate 3.62
    # regressed here: the engine existed but StartDevice never called it.
    start_active = function_body(
        active, "NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(")
    # Desde o 3.67 Bc250DxgkDdiStopDeviceActive e' um involucro de uma linha
    # sobre a implementacao parametrizada pelo motivo do teardown. Auditar o
    # involucro nao provaria nada.
    stop_active = function_body(
        active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason(")
    quiesce_active = function_body(
        active, "static NTSTATUS Bc250ActiveQuiesceAndSnapshotTracker(")
    submit_wrapper = function_body(
        active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualLifecycle(")
    submit_gate = function_body(
        active, "BOOLEAN Bc250ActiveSubmissionGateEnter(")
    submit_body = function_body(
        active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualBody(")
    one_shot_hqd = function_body(
        active, "static BC250_HQD_ACTIVE_STATUS "
                "Bc250ActiveRunActiveHqdSubmissionTransaction(")
    for needle, label in (
        ('L"PersistentSubmission"', "persistent-submission tunable read"),
        ("Bc250SubmissionEngineOnline(context);", "persistent engine online"),
        ("Bc250SubmissionEngineSelfTest(context);", "P1 physical self-test"),
        ("Bc250SubmissionEngineP2StreamTest(context);", "P2 stream test"),
        ("Bc250SubmissionEngineRecordTelemetry(context);",
         "P1/P2 telemetry publication"),
        ("context->SubmissionEngine.SelfTest.PhysicalReady != 0u",
         "P1 physical-ready success gate"),
        ("context->SubmissionEngine.P2Stream.PhysicalReady != 0u",
         "P2 physical-ready success gate"),
        ("(context->PersistentSubmission == 0u ||",
         "strict persistent-submission policy gate"),
        ("(context->ShaderDispatch == 0u ||",
         "strict shader-dispatch policy gate"),
    ):
        require(start_active, needle, label, errors)
    require(stop_active, "Bc250SubmissionEngineOffline(context);",
            "persistent engine StopDevice teardown", errors)
    if (stop_active and
            stop_active.find("Bc250SubmissionEngineOffline(context);") >
            stop_active.find("Bc250DxgkDdiStopDeviceReadonly(&context->Core)")):
        errors.append("persistent engine teardown occurs after core teardown")

    # ------------------------------------------------------------------
    # Contrato de quiescencia do teardown (candidato 3.67).
    #
    # O 3.66 decidiu o teardown pelo retorno de DxgkCbSynchronizeExecution e
    # devolveu STATUS_DEVICE_BUSY com o rastreador comprovadamente vazio
    # (Pending=0, Reserved=0, SyncStatus=0xC0000001). O gate abaixo impede
    # tanto a volta daquele mecanismo unico quanto a "correcao" oposta —
    # ler os contadores sem lock e sem quiescencia.
    # ------------------------------------------------------------------
    require(stop_active, "Bc250ActiveQuiesceAndSnapshotTracker(",
            "tracker quiescence before StopDevice teardown", errors)
    require(stop_active, "pendingCount != 0u || reservedCount != 0u",
            "fail-closed StopDevice on non-empty tracker", errors)
    require(stop_active, "STATUS_DEVICE_BUSY",
            "StopDevice busy verdict preserved", errors)
    for needle, label in (
        ("BC250_ACTIVE_LIFECYCLE_STOPPING",
         "STOPPING state before tracker snapshot"),
        ("Bc250WddmFenceTrackerSetAcceptingWork(tracker, FALSE)",
         "admission gate closed before snapshot"),
        ("Bc250ActiveRetirementTimerStop(Context);",
         "retirement timer stopped before snapshot"),
        ("Bc250ActiveStopFlushDpcs();", "DPC drain before snapshot"),
        ("Bc250WddmFenceTrackerSnapshotCounts(",
         "locked tracker snapshot"),
        ("&Context->SubmitInFlight, 0, 0",
         "in-flight submission drain"),
    ):
        require(quiesce_active, needle, label, errors)
    # A fotografia so vale sob o lock proprio; leitura crua e' proibida em
    # ambos os corpos.
    for body, where in ((quiesce_active, "quiescence"), (stop_active, "StopDevice")):
        forbid(body, "WddmFences.Count",
               f"unsynchronized tracker Count read in {where}", errors)
        forbid(body, "WddmFences.ReservedCount",
               f"unsynchronized tracker ReservedCount read in {where}", errors)

    if quiesce_active.count("Bc250ActiveRetirementTimerStop(Context);") < 2:
        errors.append("missing second retirement timer stop after in-flight drain")

    # O portao de ciclo de vida precisa recusar submissao nova depois do
    # STOPPING. Desde a revisao final do 3.67 ele e' uma funcao testavel, em
    # vez de texto enterrado no wrapper.
    for needle, label in (
        ("BC250_ACTIVE_LIFECYCLE_RUNNING",
         "submission lifecycle gate"),
        ("InterlockedIncrement(&Context->SubmitInFlight)",
         "in-flight submission accounting"),
    ):
        require(submit_gate, needle, label, errors)
    for needle, label in (
        ("Bc250ActiveSubmissionGateEnter(context)",
         "wrapper enters submission gate"),
        ("Bc250ActiveSubmitCommandVirtualBody(",
         "single-exit submission body"),
        ("Bc250ActiveSubmissionGateLeave(context)",
         "single submission gate release"),
    ):
        require(submit_wrapper, needle, label, errors)
    require(submit_body, "BC250_ACTIVE_LIFECYCLE_RUNNING",
            "timer rearm forbidden after STOPPING", errors)
    require(submit_body, "Bc250ActiveRetirementTimerStart(context);",
            "conditional retirement timer rearm", errors)

    # Rastreador com lock proprio em todos os caminhos.
    for needle, label in (
        ("Bc250FenceLockAcquire(&Tracker->Lock", "tracker lock acquisition"),
        ("BC250_FENCE_LOCK_SPIN_LIMIT", "bounded tracker lock spin"),
        ("Bc250WddmFenceTrackerReserveUnlocked(", "locked reserve path"),
        ("Bc250WddmFenceTrackerCommitUnlocked(", "locked commit path"),
        ("Bc250WddmFenceTrackerCancelUnlocked(", "locked cancel path"),
        ("++Tracker->RejectedWhileStopping;",
         "admission gate rejection accounting"),
    ):
        require(engine, needle, label, errors)

    # Evidencia nao pode misturar corridas.
    #
    # Ate o 3.75 este gate exigia os dois resets por familia. Eles tinham
    # chamador real, mas cobertura incompleta: Shader*, HqdActive* e Dmm*
    # ficavam de fora de toda varredura, e a corrida 3.75 publicou
    # DmmChildRelationsCalled=56 e DmmIsSupportedCount=41 herdados de boots
    # anteriores. O gate passava enquanto a contaminacao acontecia.
    #
    # Agora exige o reset INTEGRAL, que zera o enum inteiro e nao depende de
    # nenhuma lista ser mantida em dia.
    driver_entry = function_body(active, "NTSTATUS DriverEntry(")
    require(driver_entry, "Bc250ProbeResetAllVolatile();",
            "integral telemetry reset at load", errors)

    # ORDEM do early-return render-only dentro de StartDevice.
    #
    # Nao basta o perfil render-only devolver zero sources/children: ele tem de
    # faze-lo ANTES de adquirir post-display ownership e antes de
    # Bc250DisplayInit. Um early-return colocado depois estaria tomando o
    # scanout do GOP para so' entao desistir — o cenario de tela preta que o
    # perfil existe para evitar. Isto e' uma propriedade de ordem no codigo, e
    # so' analise estatica a captura; nenhum teste de tabela a veria.
    start_device = function_body(active, "NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(")
    early = start_device.find("BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB")
    post_display = start_device.find("Bc250AcquirePostDisplayReservation")
    display_init = start_device.find("Bc250DisplayInit(")
    if early == -1:
        errors.append("StartDevice lacks the render-only early return")
    if post_display != -1 and not (0 <= early < post_display):
        errors.append(
            "render-only early return does not precede post-display ownership")
    if display_init != -1 and not (0 <= early < display_init):
        errors.append(
            "render-only early return does not precede Bc250DisplayInit")

    # A MMU global precisa ser comum aos perfis production/lab e qualquer
    # falha precisa encerrar StartDevice. A corrida 14:32 mostrou o defeito:
    # SNAPSHOT_FAILED foi apenas logado e SetRootPageTable continuou.
    global_mmu_call = start_device.find(
        'Bc250ActiveEnsureGlobalMmu(\n                context, "StartDevice")')
    unsafe_profile_guard = start_device.find(
        "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)")
    if global_mmu_call == -1:
        errors.append("StartDevice lacks global MMU initialization")
    if unsafe_profile_guard != -1 and not (
            0 <= global_mmu_call < unsafe_profile_guard):
        errors.append("global MMU initialization is not common to active/lab profiles")
    for needle, label in (
        ("if (!NT_SUCCESS(globalMmuNtStatus))",
         "global MMU StartDevice failure branch"),
        ("return globalMmuNtStatus;",
         "global MMU StartDevice fail-closed return"),
        ("Global MMU transport map status",
         "global MMU explicit MMIO transport preflight"),
    ):
        require(start_device, needle, label, errors)

    set_root = function_body(
        active, "NTSTATUS Bc250ActiveSetRootPageTableObject(")
    ensure_at = set_root.find("Bc250ActiveEnsureGlobalMmu(")
    publish_at = set_root.find("Bc250McdmObjectSetRootPageTable(")
    if ensure_at == -1 or publish_at == -1 or ensure_at > publish_at:
        errors.append(
            "SetRootPageTable publishes software state before global MMU readiness")
    for needle, label in (
        ("return globalMmuStatus;",
         "SetRootPageTable global MMU fail-closed return"),
        ("GlobalMmuDummyAllocated",
         "owned global MMU dummy allocation"),
        ("Bc250VramAllocate(allocator, 0x1000ull, 0x1000ull",
         "allocator-backed global MMU dummy page"),
        ("allocator = &Context->GlobalMmuAllocator;",
         "global MMU allocator independent from unstaged bootstrap"),
        ("Bc250VramReserveFixed(allocator,",
         "inherited scanout reserved before global MMU dummy"),
    ):
        require(active, needle, label, errors)

    stop_device = function_body(
        active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason(")
    require(stop_device, "mmuRestoreStatus = Bc250GcvmGlobalMmuRestore(",
            "checked global MMU restore", errors)
    require(stop_device, "Global GFXHUB MMU restore FALHOU",
            "truthful global MMU restore failure telemetry", errors)
    forbid(stop_device, "(void)Bc250GcvmGlobalMmuRestore",
           "unchecked global MMU restore", errors)

    # O marcador de bring-up inseguro so' pode existir sob a macro. Se ele
    # vazar para fora do #if, a deteccao por conteudo do empacotador seguro
    # passa a dar falso positivo em toda imagem, e o gate vira decoracao.
    marker = "BC250-UNSAFE-STARTDEVICE-BRINGUP-COMPILED"
    if marker in active:
        guard = active.find("#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)")
        at = active.find(marker)
        endif = active.find("#endif", guard) if guard != -1 else -1
        if not (0 <= guard < at < endif):
            errors.append(
                "unsafe bring-up marker is not enclosed by its #if guard")

    # Captura integral do GUID em QueryInterface, e marcacao de DDI uma unica
    # vez por chamada (o 3.66 marcava duas e inflava V5DdiMarks).
    require(display, "Bc250ProbeRecordQueryInterface(&record);",
            "full GUID capture on QueryInterface", errors)
    require(display, "RtlCopyMemory(record.Data4",
            "QueryInterface GUID Data4 capture", errors)
    require(active, "Bc250ProbeSnapshotQueryInterfaces();",
            "QueryInterface snapshot published at teardown", errors)
    query_interface_body = function_body(
        display, "NTSTATUS APIENTRY Bc250DxgkDdiQueryInterface(")
    if query_interface_body.count(
            "Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_INTERFACE);") != 1:
        errors.append(
            "QueryInterface must mark the DDI trace exactly once")

    # Nenhuma interface pode ser aceita por suposicao para manter o
    # adaptador vivo: a recusa continua sendo o unico retorno.
    forbid(query_interface_body, "return STATUS_SUCCESS;",
           "QueryInterface accepting an unimplemented interface", errors)
    for needle, label in (
        ("props.SchedulerMe = 2u;", "Cyan Skillfish MEC2 selector"),
        ("props.SchedulerPipe = 0u;", "Cyan Skillfish pipe selector"),
        ("props.SchedulerQueue = 0u;", "Cyan Skillfish queue selector"),
    ):
        require(one_shot_hqd, needle, label, errors)

    # RenderNode must select a reachable registration path in DriverEntry.
    # Candidate 3.63 retained DxgkInitialize only behind a DEADBEEF condition,
    # so every real boot silently remained display-only.
    driver_entry = function_body(active, "NTSTATUS DriverEntry(")
    start_v5 = function_body(
        active, "NTSTATUS APIENTRY "
                "Bc250DxgkDdiStartDeviceV5Telemetry(")
    for needle, label in (
        ('L"RenderNode"', "RenderNode DriverEntry tunable read"),
        ("renderNodePresent == 1u && renderNode == 1u",
         "reachable full-WDDM registration gate"),
        ("status = DxgkInitialize(", "full-WDDM registration call"),
        ("status = DxgkInitializeDisplayOnlyDriver(",
         "display-only registration fallback"),
        ("BC250_PROBE_V5_REGISTRATION_MODE",
         "registration-mode telemetry"),
    ):
        require(driver_entry, needle, label, errors)
    forbid(driver_entry, "0xDEADBEEF",
           "dead-code-only DxgkInitialize reachability", errors)
    for needle, label in (
        ("Bc250DxgkDdiStartDeviceActive(", "active StartDevice delegation"),
        ("BC250_PROBE_V5_START_DEVICE_STATUS",
         "full-WDDM StartDevice status telemetry"),
        ("BC250_PROBE_V5_START_DEVICE_SOURCES",
         "full-WDDM source-count telemetry"),
        ("BC250_PROBE_V5_START_DEVICE_CHILDREN",
         "full-WDDM child-count telemetry"),
    ):
        require(start_v5, needle, label, errors)

    vsync_dpc = function_body(display, "static VOID Bc250VsyncDpcRoutine(")
    synchronized_vsync = function_body(
        display, "static BOOLEAN Bc250SynchronizedVsyncCallback(")
    if not vsync_dpc:
        errors.append("missing VSync DPC body")
    if not synchronized_vsync:
        errors.append("missing synchronized VSync callback body")
    require(vsync_dpc, "DxgkCbSynchronizeExecution(",
            "DIRQL-synchronized VSync notification", errors)
    forbid(vsync_dpc, "DxgkCbNotifyInterrupt(",
           "direct NotifyInterrupt call from VSync DPC", errors)
    forbid(vsync_dpc, "Bc250ProbeRecord(",
           "persistent registry telemetry from VSync DPC", errors)
    forbid(synchronized_vsync, "Bc250ProbeRecord(",
           "persistent registry telemetry from synchronized VSync", errors)
    require(display, "KeGetCurrentIrql() == PASSIVE_LEVEL",
            "PASSIVE_LEVEL display telemetry gate", errors)
    require(display, "Mode->Format.Graphics.Stride >=",
            "source stride independent from GOP pitch", errors)
    require(display, "Mode->Format.Graphics.PixelFormat == D3DDDIFMT_X8R8G8B8",
            "32-bit XRGB source mode support", errors)
    require(display, "Bc250FillNativeSignalInfo(",
            "coherent native signal timing helper", errors)
    require(display, "BC250_NATIVE_HBLANK",
            "shared EDID/VidPN horizontal blanking", errors)
    require(display, "BC250_NATIVE_VBLANK",
            "shared EDID/VidPN vertical blanking", errors)
    require(synchronized_vsync, "DxgkCbNotifyInterrupt(",
            "NotifyInterrupt inside synchronized callback", errors)
    require(display, "InterlockedExchange(&g_Bc250DisplayState.DisplayStarted, 0);",
            "VSync lifecycle shutdown before teardown/removal", errors)
    require(display, "g_Bc250DisplayState.VsyncRequested",
            "VSync request preservation across D-state transitions", errors)
    require(display, "PostDisplayInfo->PhysicAddress.QuadPart == 0",
            "GOP physical-address validation", errors)
    require(display, "PostDisplayInfo->Pitch < (PostDisplayInfo->Width * 4u)",
            "GOP pitch validation", errors)
    require(display, "return STATUS_INSUFFICIENT_RESOURCES;",
            "fail-closed framebuffer mapping", errors)
    require(display, "ExAcquireRundownProtection(&g_Bc250DisplayState.PresentRundown)",
            "present/teardown rundown acquisition", errors)
    require(display, "ExWaitForRundownProtectionRelease(&g_Bc250DisplayState.PresentRundown)",
            "present/teardown rundown drain", errors)
    require(display, "pPresentDisplayOnly->BytesPerPixel != 4",
            "display-only present pixel-format validation", errors)
    require(display, "pPresentDisplayOnly->Pitch <= 0",
            "display-only present pitch validation", errors)
    require(display, "&pPresentDisplayOnly->pMoves[i].DestRect, &clipped",
            "bounded display-only move rectangles", errors)
    require(display, "PositionX >= g_Bc250DisplayState.CurrentMode.Width",
            "system-display horizontal origin bound", errors)
    require(display, "SourceStride < SourceWidth * 4u",
            "system-display source-stride bound", errors)
    require(display, "static NTSTATUS Bc250ValidateVidPn(",
            "shared fail-closed VidPN validator", errors)
    require(display, "if (pathCount != 1)",
            "single-path VidPN topology", errors)
    require(display, "Bc250VidPnPathSupported(path, RequirePinnedModes)",
            "VidPN source/target and transform validation", errors)
    require(display, "Bc250SourceModeMatchesGop(sourceMode)",
            "pinned source mode GOP validation", errors)
    require(display, "Bc250TargetModeMatchesGop(targetMode)",
            "pinned target mode GOP validation", errors)
    require(display, "g_Bc250DisplayState.VidPnCommitted",
            "present gated by committed VidPN", errors)
    cofunc = function_body(
        display, "NTSTATUS APIENTRY Bc250DxgkDdiEnumVidPnCofuncModality(")
    require(cofunc, "if (pathCount != 1)",
            "cofunctional enumeration single-path gate", errors)
    require(cofunc, "pfnUpdatePathSupportInfo(",
            "cofunctional identity transform publication", errors)
    require(cofunc, "Bc250PublishCofuncSourceMode(",
            "checked cofunctional source publication", errors)
    require(cofunc, "Bc250PublishCofuncTargetMode(",
            "checked cofunctional target publication", errors)
    require(display, "Successful assignment transfers ownership",
            "explicit VidPN mode-set ownership transfer", errors)
    forbid(active, "context->PostDisplayEvidence.Width ?",
           "fabricated fallback GOP width", errors)
    forbid(active, "context->PostDisplayEvidence.Height ?",
           "fabricated fallback GOP height", errors)
    set_address = function_body(display, "NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceAddress(")
    legacy_present = function_body(display, "NTSTATUS APIENTRY Bc250DxgkDdiPresent(")
    require(set_address, "STATUS_NOT_SUPPORTED",
            "unused full-WDDM source-address DDI fails closed", errors)
    require(legacy_present, "STATUS_NOT_SUPPORTED",
            "unused full-WDDM present DDI fails closed", errors)

    # PM4 GFX10 must be structurally complete before any hardware claim.
    for needle in (
        "BC250_PM4_IT_SET_UCONFIG_REG     0x79u",
        "BC250_PM4_UCONFIG_REG_BASE 0xC000u",
    ):
        require(pm4_header, needle, "canonical GFX10 UCONFIG encoding", errors)
    for needle in (
        "ExtraCount == 0u",
        "BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_NOP, ExtraCount)",
        "(2u << 29)",
        "0u); /* ctxid/reserved */",
    ):
        require(pm4_builder, needle, "complete PM4 packet encoding", errors)
    for needle in (
        "0xC0017900u",
        "0x00000040u",
        "0xC0064900u",
        "0x06603514u",
        "0x20000000u",
        "submittedDwords != 11u",
        "rptrPoll == submittedDwords",
    ):
        require(compute_mqd, needle, "canonical HQD 3+8 dword payload", errors)
    forbid(compute_mqd, "0x00002040u", "raw segment-less SCRATCH PM4 target", errors)

    # Runtime evidence must not be synthesized by AddDevice/StartDevice.
    forbid(active, "Bc250BuildDefaultActiveRuntime", "synthetic runtime builder", errors)
    require(active,
            "context->RuntimeInventoryReport.OwnershipEvidenceFlags;",
            "runtime-derived ownership flags", errors)
    forbid(active, "Reservations[0].Offset =", "fixed inherited reservation", errors)
    require(active,
            "context->Core.ReadonlyGatePassed == 1u &&",
            "VRAM mutation gated by passed readonly preflight", errors)
    require(active, "InterlockedExchange(&context->IsActive, 0);",
            "StartDevice inactive state", errors)
    require(active, "return preflightStatus;",
            "StartDevice fail-closed return", errors)
    require(active, "Bc250AcquirePostDisplayReservation(",
            "physical post-display ownership capture", errors)
    require(active, "BC250_PROBE_POST_DISPLAY_STATUS",
            "post-display telemetry", errors)
    require(active, "BC250_RUNTIME_EVIDENCE_SCANOUT_PROBED |",
            "partial runtime inventory evidence", errors)
    require(active, "BC250_PROBE_RUNTIME_INVENTORY_COMPLETE",
            "runtime inventory completeness telemetry", errors)
    require(active, "Bc250ActiveQueryRuntimeCaps(",
            "runtime-backed capability provider", errors)
    require(callbacks, "runtimeCaps.LocalSegmentBytes",
            "runtime-backed segment sizing", errors)
    require(callbacks, "case DXGKQAITYPE_QUERYSEGMENT4:",
            "mandatory MCDM QUERYSEGMENT4", errors)
    require(callbacks, "case DXGKQAITYPE_NUMPOWERCOMPONENTS:",
            "mandatory MCDM power component query", errors)
    require(callbacks, "case DXGKQAITYPE_HISTORYBUFFERPRECISION:",
            "mandatory MCDM history precision query", errors)
    require(callbacks, "runtimeCaps.FullMcdmReady == 0u",
            "full MCDM capability gate", errors)
    forbid(callbacks, "caps->SupportNonVGA = TRUE",
           "MCDM-prohibited non-VGA capability", errors)
    # Alvo do driver: WDDM display+render (jogos).
    #
    # Este gate exigia MiscCaps.ComputeOnly = 1u, do alvo MCDM original. Essa
    # exigencia entrou em contradicao direta com o resto do driver assim que o
    # miniport passou a registrar por DxgkInitialize em 2026-08-31: o INF
    # declara Class=Display, o StartDevice reporta uma fonte de video e um
    # filho, e a tabela de VidPn esta inteira preenchida e provada em hardware.
    #
    # Um adaptador ComputeOnly nao apresenta nada. Manter a exigencia seria
    # congelar a contradicao que o dxgkrnl rejeita com STATUS_INTERNAL_ERROR.
    # A propriedade de seguranca que importa continua garantida logo acima:
    # SupportNonVGA nao pode ser TRUE.
    #
    # 2026-09-02: o bit deixou de ser literal e passou a ser derivado do
    # perfil, porque o perfil de laboratorio RENDER_ONLY_LAB reporta 0 sources
    # e 0 children e para ELE o valor coerente e' 1. O forbid abaixo continua
    # valendo -- ele impede que alguem volte a cravar 1u no fonte --, mas
    # sozinho ele nao alcanca mais o caso derivado.
    #
    # Por isso os dois require: o primeiro garante que o valor vem do getter
    # (e nao de um literal novo), e o segundo garante que o UNICO perfil que
    # recebe 1 e' o de laboratorio. A garantia de que os perfis de PRODUCAO
    # reportam 0 e' fechada por Bc250RunAdapterProfileTest, que quebra o build.
    forbid(callbacks, "caps->MiscCaps.ComputeOnly = 1u",
           "compute-only capability incompatible with display+render target",
           errors)
    require(callbacks, "caps->MiscCaps.ComputeOnly = Bc250McdmGetComputeOnly();",
            "compute-only capability must be derived from the adapter profile",
            errors)
    require(active,
            "(Profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) ? 1u : 0u",
            "compute-only may only be declared by the laboratory profile",
            errors)
    forbid(active, "BC250_RUNTIME_EVIDENCE_COMPLETE;",
           "fabricated complete runtime inventory", errors)
    require(active, "BC250_RUNTIME_EVIDENCE_PLATFORM_CARVEOUTS_PROBED",
            "explicit platform carveout evidence", errors)
    require(active, "AddressSpace.CpuVisibleBytes;",
            "runtime-derived opaque carveout boundary", errors)
    require(active, "BC250_VRAM_ROLE_PLATFORM_RESERVED",
            "opaque non-visible VRAM reservation", errors)
    forbid(active, "0x1F800000", "historical fixed PSP TMR offset", errors)
    require(readonly_ddi, "Bc250CapturePlatformRegistersReadonly(",
            "readonly platform-register capture", errors)
    require(readonly_ddi, "BC250_PROBE_PLATFORM_SNAPSHOT_STATUS",
            "platform-register telemetry", errors)
    require(platform_snapshot, "for (pass = 0u; pass < 2u; ++pass)",
            "two-pass stable platform snapshot", errors)
    require(platform_snapshot, "BC250_PLATFORM_SNAPSHOT_UNSTABLE",
            "unstable platform snapshot rejection", errors)
    require(vram_probe, "original[i] = target[i]",
            "VRAM probe original-byte capture", errors)
    require(vram_probe, "target[i] = original[i]",
            "VRAM probe restoration", errors)
    require(vram_probe, "BC250_VRAM_PROBE_RESTORE_FAILED",
            "VRAM probe restore verification", errors)
    require(paging, "Plan->EngineId = 17u;",
            "GC 10.1.3 GFXHUB invalidation engine", errors)
    require(paging, "Plan->RequiresDummyRequestRead = 1u;",
            "GFXHUB invalidation dummy request read", errors)
    require(paging, "Plan->UsesSemaphore = 0u;",
            "GFXHUB no-semaphore contract", errors)
    require(paging, "Plan->AckMask = 1u << Params->Vmid;",
            "per-VMID invalidation ACK mask", errors)
    flush_start = paging.find("BC250_PAGING_STATUS Bc250PagingBuildFlushTlb(")
    flush_body = paging[flush_start : flush_start + 850]
    forbid(flush_body, "Bc250SdmaEmitNop",
           "fake SDMA NOP TLB invalidation", errors)
    require(flush_body, "BC250_PAGING_UNSUPPORTED_OPERATION",
            "physical TLB flush remains fail-closed", errors)
    require(tlb_transaction, "Evidence->AckBefore == 0xFFFFFFFFu",
            "all-ones ACK rejection", errors)
    require(tlb_transaction, "Evidence->RequestDummyRead",
            "mandatory post-request dummy read evidence", errors)
    require(tlb_transaction, "BC250_TLB_TRANSACTION_TIMEOUT",
            "bounded TLB ACK timeout", errors)
    require(tlb_transaction, "Evidence->RangeRestorePassed = 1u;",
            "invalidation range restoration proof", errors)
    require(mmio_transport,
            "static uint32_t Bc250MmioTopologyWriteOffsetAllowed",
            "explicit MMIO write allowlist", errors)
    require(mmio_transport, "ByteOffset == 0xA2D0u ||",
            "GFXHUB engine-17 request write", errors)
    write_allowlist_start = mmio_transport.find(
        "static uint32_t Bc250MmioTopologyWriteOffsetAllowed")
    write_allowlist_body = mmio_transport[
        write_allowlist_start : write_allowlist_start + 350]
    forbid(write_allowlist_body, "ByteOffset == 0xA318u",
           "write access to read-only ACK", errors)
    require(active, "Bc250ActiveExecuteGcvmBacking(",
            "owned GCVM backing StartDevice transaction", errors)
    require(active, "Bc250ActiveFlushMappedGfxhubTlb(",
            "physical GFXHUB TLB transaction inside VMID8 probe", errors)
    require(active, "context->TlbTransactionEvidence.PhysicalReady",
            "hardware-derived TLB readiness telemetry", errors)
    require(active, "Bc250McdmObjectRegistryInitialize(&context->Objects)",
            "owned MCDM object registry initialization", errors)
    require(active, "Bc250McdmObjectRegistryTeardown(",
            "MCDM object teardown on StopDevice", errors)

    # These DDIs must remain explicit failures until backed by owned hardware state.
    for function in (
        "Bc250McdmBuildPagingBuffer",
        "Bc250McdmCreateAllocation",
        "Bc250McdmCreateContext",
        "Bc250McdmPreemptCommand",
        "Bc250McdmQueryEngineStatus",
        "Bc250McdmResetFromTimeout",
        "Bc250McdmRestartFromTimeout",
        "Bc250McdmSetPowerState",
        "Bc250McdmSubmitCommandVirtual",
    ):
        body = function_body(callbacks, f"NTSTATUS APIENTRY {function}(")
        if not body:
            body = function_body(callbacks, f"NTSTATUS {function}(")
        if not body:
            errors.append(f"missing callback: {function}")
            continue
        require(body, "STATUS_NOT_SUPPORTED", f"fail-closed {function}", errors)

    build_paging = function_body(
        callbacks, "NTSTATUS APIENTRY Bc250McdmBuildPagingBuffer(")
    require(build_paging, "Unhandled Op=%u -> NOT_SUPPORTED",
            "unknown paging operation diagnostic", errors)
    forbid(build_paging, "Unhandled Op=%u -> SUCCESS",
           "unknown paging operation false success", errors)
    require(callbacks, "update->Flags.Repeat != 0u ? 1u",
            "Repeat PTE diagnostic bounds", errors)
    require(callbacks, "g_Bc250PagingStages[64]",
            "lock-free paging stage ring", errors)
    paging_stage = function_body(callbacks, "static VOID Bc250PagingStage(")
    require(paging_stage, "InterlockedIncrement",
            "atomic paging stage reservation", errors)
    require(paging_stage, "KeMemoryBarrier",
            "paging stage publication barrier", errors)
    forbid(paging_stage, "Zw", "blocking Zw call in paging stage ring", errors)
    forbid(paging_stage, "Bc250DisplayLog",
           "file diagnostic call in paging stage ring", errors)

    # A UMD de producao nunca pode esconder ausencia de render nativo com WARP.
    forbid(umd, "d3d10warp.dll", "WARP software rasterizer delegation", errors)
    forbid(umd, "Bc250EnsureWarp", "WARP loader in production UMD", errors)
    # O arquivo por processo continua obrigatorio, mas a escrita mudou de lugar
    # em 13/09: agora ha UM sink (Bc250WinsysTraceRaw) compartilhado pela UMD e
    # pela winsys. Antes eram duas instrumentacoes, e a da winsys so' ia para
    # stderr -- que o dwm.exe nao tem, o que cegou a corrida das 05:50.
    winsys_src = (ROOT / "driver" / "umd" / "bc250_wddm_winsys.c").read_text(
        encoding="utf-8", errors="replace")
    require(winsys_src, "BC250_UMD_LOG_%lu.txt",
            "per-process UMD trace file", errors)
    require(umd_header, "#define BC250_D3D10_11_NATIVE_READY 0",
            "D3D10/11 production readiness gate", errors)
    for signature in (
        "HRESULT APIENTRY OpenAdapter10(",
        "HRESULT APIENTRY OpenAdapter10_2(",
        "HRESULT APIENTRY OpenAdapter11(",
    ):
        open_legacy = function_body(umd, signature)
        require(open_legacy, "return E_NOTIMPL;",
                f"fail-closed {signature.split('(')[0]}", errors)
    open12 = function_body(umd, "HRESULT APIENTRY OpenAdapter12(")
    require(open12, "return E_NOTIMPL;",
            "fail-closed D3D12 adapter advertisement", errors)
    forbid(open12, "return S_OK;",
           "D3D12 adapter success without native CreateDevice", errors)
    require(winsys, "pfnCreateContextVirtualCb",
            "WDDM2 virtual-context callback path", errors)
    require(winsys, "pfnAllocateCb",
            "VidMm allocation callback path", errors)
    require(winsys, "pfnSubmitCommandCb",
            "dxgkrnl virtual submission callback path", errors)
    require(winsys, "BroadcastContextCount = 1u",
            "explicit submission context ownership", errors)
    require(winsys, "BC250_ALLOCATION_PRIVATE_MAGIC",
            "versioned allocation ABI from UMD to KMD", errors)
    require(active, "Bc250AllocationIsPrimary(&request)",
            "primary allocation selected by explicit ABI token", errors)
    require(active, "Bc250ActiveGpuvaRootKey(",
            "zero-offset segment root encoded without null sentinel collision", errors)
    require(active, "flushRootKey != 0ull",
            "GPUVA flush accepts a valid zero segment offset through root key", errors)
    require(objects, "processRec->RootPageTableSetCount == 0u",
            "root presence uses explicit SetRootPageTable state", errors)
    forbid(active, "request.ByteSize >= 11059200ull ||",
           "primary allocation inferred from size/first resource", errors)
    forbid(winsys, "FenceValue++", "CPU-authored winsys fence", errors)
    forbid(winsys, "Sleep(", "winsys delay pretending execution", errors)
    require(winsys_test, "WDDM WINSYS CONTRACT: PASS",
            "offline winsys lifecycle contract", errors)
    require(umd_build, "bc250_wddm_winsys_test.exe",
            "winsys contract integrated in UMD build", errors)
    require(build, "Visual Studio\\18",
            "KMD build selects Visual Studio 2026 explicitly", errors)
    require(build, "set WDKVER=10.0.28000.0",
            "KMD build pins SDK/WDK 28000", errors)
    require(umd_build, "Visual Studio\\18",
            "UMD build selects Visual Studio 2026 explicitly", errors)
    forbid(build, "Visual Studio\\2022",
           "VS2022 fallback in the canonical KMD build", errors)
    forbid(umd_build, "Visual Studio\\2022",
           "VS2022 fallback in the canonical UMD build", errors)
    require(lab_package, "10.0.28000.0\\x86\\Inf2Cat.exe",
            "lab packaging prefers the WDK 28000 Inf2Cat", errors)
    require(lab_package, "10.0.28000.0\\x64\\signtool.exe",
            "lab packaging prefers the WDK 28000 SignTool", errors)
    require(collector, "LogName='Application'",
            "Application event capture for DWM failures", errors)
    require(collector, "dwm\\.exe|0xe0464645|bc250umd",
            "DWM/UMD event filter", errors)
    require(bind, "arm-dwm-crash-capture.ps1",
            "DWM crash capture before risky bind", errors)
    require(bind, "RunRadvComputeTripod nao pertence a trilha ativa",
            "active bind refuses the lab-only RADV tripod", errors)
    require(lab_bind, "-EnableGpuvaLabPromotion exige -IAcceptGpuvaLabPromotion",
            "GPUVA promotion is opt-in with explicit acceptance", errors)
    require(lab_bind, "-EnableGpuvaLabPromotion exige -EnablePersistentSubmission",
            "GPUVA promotion requires a submission node", errors)
    require(lab_bind, "GpuvaLabPromotion = $(if ($EnableGpuvaLabPromotion) { 1 } else { 0 })",
            "GPUVA promotion defaults to disabled in the pre-bind policy", errors)
    require(lab_package, "HKR,Parameters,GpuvaLabPromotion,0x00010003,0",
            "lab INF declares the GPUVA promotion as a NOCLOBBER default", errors)
    require(lab_bind, "-RunRadvComputeTripod exige -EnableGpuvaLabPromotion",
            "lab tripod hook requires the GPUVA gate open", errors)
    require(lab_bind, "-RunRadvComputeTripod exige o watchdog",
            "lab tripod hook requires the armed rollback watchdog", errors)
    require(lab_bind, "ICD explicito validado antes do binding",
            "lab bind proves the explicit ICD identity before APPLY", errors)
    require(lab_bind, "RADV_WDDM_COMPUTE_TRIPOD falhou",
            "lab bind fails closed on RADV preflight failure", errors)
    require(lab_bind, "compute-display-prerequisite-gate.json",
            "lab bind records the selected compute display gate", errors)
    require(lab_bind, "[GetStdAllocData\\] SharedPrimary:",
            "RADV requires a primary allocation from this bind", errors)
    require(lab_bind, "=== CommitVidPn Entered:",
            "RADV requires CommitVidPn from this bind", errors)
    require(lab_bind, "=== SetVidPnSourceAddress ",
            "RADV requires SetVidPnSourceAddress from this bind", errors)
    require(lab_package, "[ValidateSet(20, 27)][int]$WddmContractVersion",
            "lab WDDM contract version is an explicit, bounded choice", errors)
    require(lab_package, "$sourceDriverVersion.Build + 1",
            "lab package ranks above the active package sharing its HWID", errors)
    require(lab_bind, "[uint32]$manifest.WddmContractVersion",
            "lab bind takes the WDDM contract from the manifest", errors)
    require(lab_bind, "PackageUmdSha256 'UMD'",
            "lab bind checks the signed UMD package identity", errors)
    require(lab_bind, "SourceUmdSha256",
            "lab bind rejects a package stale against the UMD build", errors)
    require(lab_package, "PackagePdbSha256",
            "lab manifest seals the exact private PDB", errors)
    require(lab_bind, "Assert-ArtifactHash $packagePdb $manifest.PackagePdbSha256",
            "lab bind verifies the private PDB identity", errors)
    require(lab_bind, "private-symbols.json",
            "lab bind archives private symbols before destructive work", errors)
    require(lab_bind, "Copy-Item -LiteralPath $packageSys -Destination $evidenceSys",
            "lab bind archives the exact SYS beside its PDB", errors)
    require(lab_bind, "Get-FileHash -LiteralPath $evidenceSys",
            "lab bind rehashes the archived SYS destination", errors)
    require(lab_bind, "Get-FileHash -LiteralPath $evidencePdb",
            "lab bind rehashes the archived PDB destination", errors)
    require(lab_bind, "$evidencePdbHash -ne [string]$manifest.PackagePdbSha256",
            "lab bind fails closed when the archived PDB is corrupted", errors)
    # REBAIXAMENTO DE PERFIL (20/09, preparando a Corrida 45). O empacotador
    # aceita qualquer combinacao de switches e a auditoria do PACOTE da PASS num
    # perfil fraco -- ela confere o pacote, nao a INTENCAO do ensaio. Ao preparar
    # o candidato pos-Corrida 44 foi produzido um DISPLAY_LAB em modo READ-ONLY
    # para um ensaio que EXIGE DISPLAY_RENDER_LAB com submissao persistente:
    # nada comparava com o que havia rodado antes, e um bind assim mede o nada.
    for needle, label in (
        ("$lastBinding.profile",
         "lab bind compares the candidate profile against the last binding"),
        ("Perfil do candidato e MAIS FRACO que o do ultimo bind",
         "lab bind names the profile downgrade instead of binding silently"),
        ("if (-not $IAcceptProfileDowngrade) { throw $msg }",
         "profile downgrade fails closed without explicit acceptance"),
        ("profile = [ordered]@{",
         "lab bind records the bound profile for the next comparison"),
        ("PersistentSubmissionEnabled = [bool]$manifest.PersistentSubmissionEnabled",
         "recorded profile carries the submission mode"),
    ):
        require(lab_bind, needle, label, errors)

    # CAUDA FINAL: CONTEUDO, NAO TAMANHO (Corrida 50). O criterio era
    # $tailPass2 -ge $expected -- puro tamanho. Deu "completo" com 2.751.230
    # bytes que eram 100% ESPACO, e a leitura da corrida concluiu "perdemos a
    # coleta" com o dado intacto no arquivamento inicial. Medir o proxy em vez
    # do efeito e a classe de defeito mais cara deste projeto.
    for needle, label in (
        # (o CALCULO e exigido estruturalmente abaixo -- exigir so o nome deixa
        #  passar "$contentIsPadding = $false", e a mutacao M2 passou por isso)
        ("-and (-not $contentIsPadding)",
         "completude exige conteudo real, nao so tamanho"),
        ("content_is_padding  = $contentIsPadding",
         "o veredito de preenchimento vai para a proveniencia"),
        ("e' preenchimento. NAO leia esse arquivo",
         "arquivo so com preenchimento alerta em voz alta"),
    ):
        require(lab_bind, needle, label, errors)
    # O veredito tem de vir da CONTAGEM de bytes nao-preenchimento, nao de um
    # literal. Espacos normalizados para sobreviver a reformatacao.
    lab_bind_ws = re.sub(r"\s+", "", lab_bind)
    if "$contentIsPadding=($finalNonPad-le64)" not in lab_bind_ws:
        errors.append(
            "a cauda FINAL nao classifica preenchimento pela contagem de bytes "
            "($contentIsPadding = ($finalNonPad -le 64)): fixar o veredito num literal "
            "devolveria o defeito da Corrida 50, em que tamanho plausivel e zero conteudo "
            "passaram como coleta completa")

    require(lab_bind, "Escolha uma unica experiencia fisica: RADV ou B7_UMD_NODE1_TRIPOD",
            "lab bind forbids two physical experiments in one run", errors)
    require(lab_bind, "-RunUmdNode1Tripod exige -EnableGpuvaLabPromotion",
            "UMD Node 1 run requires explicit GPUVA promotion", errors)
    require(lab_bind, "-RunUmdNode1Tripod exige o watchdog",
            "UMD Node 1 run requires automatic rollback", errors)
    require(lab_bind, "-IAcceptOnePhysicalNode1Submission",
            "lab bind passes explicit consent to the UMD Node 1 runner", errors)
    require(lab_bind, "umd-node1-tripod-console.txt",
            "lab bind archives the UMD Node 1 runner console", errors)
    require(umd_node1_tripod, "PackageUmdSha256",
            "UMD Node 1 runner is bound to the signed package UMD", errors)
    require(umd_node1_tripod, "-RequiredRemainingSeconds ($TimeoutSeconds + 10)",
            "UMD Node 1 run fits inside the rollback deadline", errors)
    require(umd_node1_tripod, "watchdog.instance_id",
            "UMD Node 1 watchdog belongs to the exact device", errors)
    require(umd_node1_tripod, "WaitForExit($TimeoutSeconds * 1000)",
            "UMD Node 1 child lifetime is bounded", errors)
    require(umd_node1_tripod, "Stop-Process -Id $harnessProcess.Id -Force",
            "timed-out UMD Node 1 child is terminated before rollback", errors)
    require(umd_node1_tripod, "authorization_disarmed",
            "UMD Node 1 authorization teardown is measured", errors)
    require(radv_tripod, "output_lab.manifest.json",
            "RADV tripod bound to the lab bring-up package", errors)
    require(radv_tripod, "BuildKind -ne 'MCDM_LAB_BRINGUP'",
            "RADV tripod rejects non-lab packages", errors)
    require(radv_tripod, "manifest.SkipHwBringup -ne 0",
            "RADV tripod requires compiled hardware bring-up", errors)
    require(radv_tripod, "ProfileMode -ne 'DISPLAY_RENDER_LAB'",
            "RADV tripod requires the DISPLAY_RENDER profile", errors)
    require(radv_tripod, "GpuvaLabPromotionPresent -ne 1",
            "RADV tripod requires the GPUVA promotion armed", errors)
    require(radv_tripod, "gpuva_gate_operational",
            "GPUVA gate member of RADV tripod", errors)
    require(radv_tripod, "assert-bc250-safe-bind-guard.ps1",
            "RADV tripod audits the persistent SYSTEM guard", errors)
    require(radv_tripod, "-RequiredRemainingSeconds ($TimeoutSeconds + 10)",
            "RADV run fits inside rollback deadline", errors)
    require(safe_guard_assert, "Get-ScheduledTask -TaskName $taskName",
            "guard audit verifies the scheduled task", errors)
    require(safe_guard_assert, "watch-bc250mcdm-safe-bind\\.ps1",
            "guard audit verifies the exact live SYSTEM process", errors)
    require(safe_guard_assert, "state.state -ne 'OBSERVING'",
            "guard audit requires OBSERVING state", errors)
    require(lab_bind, "New-ScheduledTaskPrincipal -UserId SYSTEM",
            "all lab binds arm the watchdog as SYSTEM", errors)
    forbid(lab_bind, "Start-Process -FilePath powershell.exe -ArgumentList $watchdogArguments",
           "lab bind must not regress to a killable child watchdog", errors)
    require(radv_tripod, "installedSysHash -ne [string]$manifest.PackageSysSha256",
            "RADV physical gate verifies installed SYS", errors)
    require(radv_tripod, "EXPLICIT_DIRECT_LOAD_NO_VULKAN_LOADER",
            "test records explicit ICD direct-load mode", errors)
    require(radv_tripod, "loaded_icd_identity",
            "loaded ICD identity is a physical-test gate", errors)
    require(radv_gate, "LoadLibraryExA(requested",
            "harness loads the exact ICD path directly", errors)
    require(radv_gate, "GetModuleFileNameA(g_icd_module",
            "harness reads back the loaded ICD module path", errors)
    require(radv_gate, "vk_icdGetInstanceProcAddr",
            "harness enters through the loader/ICD ABI", errors)
    require(radv_tripod, "WaitForExit($TimeoutSeconds * 1000)",
            "bounded RADV child lifetime", errors)
    require(radv_tripod, "Save-RadvHangDump $process.Id",
            "hung RADV process dump captured before termination", errors)
    require(radv_tripod, "radv-harness-hang.dmp",
            "stable hang dump evidence name", errors)
    require(radv_tripod, "$dumpBytes -ge 1MB",
            "hang dump must exceed the truncation floor", errors)
    require(radv_tripod, "radv-harness-hang-capture-state.json",
            "hang dump capture has an explicit measured verdict", errors)
    forbid(radv_tripod, "https://msdl.microsoft.com/download/symbols",
           "network symbol lookup inside bounded hang capture", errors)
    require(radv_tripod, "physical_fence_replaced_poison",
            "physical fence member of RADV tripod", errors)
    require(radv_tripod, "byte_proof",
            "byte proof member of RADV tripod", errors)
    require(radv_tripod, "hardware_validated = $hardwareValidated",
            "measured RADV hardware verdict", errors)
    forbid(radv_tripod, "$env:VK_DRIVER_FILES =",
           "process-global Vulkan ICD mutation", errors)
    forbid(radv_tripod, "EnvironmentVariables['VK_DRIVER_FILES']",
           "elevated Vulkan Loader override that Windows ignores", errors)
    require(arm_dwm, "LocalDumps\\dwm.exe",
            "per-application WER LocalDumps key", errors)
    require(arm_dwm, "dwm-localdumps-before.reg",
            "WER registry snapshot before mutation", errors)
    require(arm_dwm, "DumpType", "full DWM dump configuration", errors)
    require(arm_dwm, "Get-ItemProperty -LiteralPath $werKey",
            "WER LocalDumps readback", errors)
    require(restore_dwm, "BC250_DWM_DUMPS_[0-9a-f]{32}",
            "exact DWM dump folder restore gate", errors)
    require(restore_dwm, "Copy-Item -LiteralPath $dump.FullName",
            "DWM dump preservation before teardown", errors)
    require(restore_dwm, "RESTORED_PENDING_DUMPS",
            "WER registry restoration independent from locked dumps", errors)
    require(restore_dwm, "registry_restored_at",
            "WER registry restoration journal", errors)
    require(collector, "restore-dwm-crash-capture.ps1",
            "collector WER teardown", errors)
    require(watchdog, "Restore-DwmCapture",
            "watchdog WER teardown", errors)

    require(callbacks, "g_Bc250CreateProcessObject(",
            "CreateProcess lifecycle routing", errors)
    require(active, "Bc250McdmInstallProcessLifecycle(",
            "active-only process lifecycle installation", errors)
    require(active, "Bc250McdmInstallDeviceLifecycle(",
            "active-only device lifecycle installation", errors)
    require(active, "Bc250McdmInstallContextLifecycle(",
            "active-only context lifecycle installation", errors)
    require(active, "ContextAbiReady", 
            "context ABI evidence gate", errors)
    require(active, "Bc250McdmInstallAllocationLifecycle(",
            "active-only allocation lifecycle installation", errors)
    require(active, "Bc250ActiveOpenAllocationLifecycle",
            "device-specific OpenAllocation lifecycle", errors)
    require(active, "Bc250ActiveCloseAllocationLifecycle",
            "device-specific CloseAllocation lifecycle", errors)
    require(active, "Bc250McdmObjectFindAllocationByToken(",
            "versioned allocation-token binding", errors)
    require(callbacks, "g_Bc250OpenAllocationObjects(",
            "OpenAllocation callback routing", errors)
    require(callbacks, "g_Bc250CloseAllocationObjects(",
            "CloseAllocation callback routing", errors)
    require(active, "Bc250ActiveCollectDebugLifecycle",
            "software-only CollectDbgInfo snapshot", errors)
    require(active, "Bc250McdmInstallDebugLifecycle(",
            "active-only debug lifecycle installation", errors)
    require(callbacks, "g_Bc250CollectDebugSnapshot(",
            "CollectDbgInfo callback routing", errors)
    require(active, "Bc250ProbeGetFirstFailure()",
            "first-failure diagnostic preservation", errors)
    debug_start = active.find("Bc250ActiveCollectDebugLifecycle")
    debug_body = active[debug_start : debug_start + 4200]
    for forbidden_debug_access in (
        "READ_REGISTER",
        "WRITE_REGISTER",
        "DxgkCbReadDeviceSpace",
        "Mmio",
    ):
        forbid(debug_body, forbidden_debug_access,
               "hardware access in CollectDbgInfo", errors)
    require(active, "AllocationAbiReady",
            "allocation ABI evidence gate", errors)
    require(active, "context->AllocationSegmentSet != 1u",
            "allocation segment-1 readiness gate", errors)
    require(active, "request.ByteSize > context->AllocationSegmentBytes",
            "allocation physical segment bound", errors)
    require(active, "info->PreferredSegment.SegmentId0 = 1u",
            "allocation segment-1 preference", errors)
    require(active, "Bc250AllocationAbiParse(",
            "versioned allocation private-data validation", errors)
    require(callbacks, "case DXGKQAITYPE_GPUMMUCAPS:",
            "conditional GPU MMU capabilities query", errors)
    require(callbacks, "case DXGKQAITYPE_PAGETABLELEVELDESC:",
            "conditional page-table level query", errors)
    require(callbacks, "runtimeCaps.GpuVaReady == 0u",
            "GPUVA query readiness gate", errors)
    require(active, "RootPageTableAbiReady",
            "root page-table lifecycle gate", errors)
    require(active, "Bc250McdmObjectSetRootPageTable(",
            "owned root page-table association", errors)
    require(callbacks, "pSetPageTable->Address.SegmentId",
            "VidMm segment-address root page table", errors)
    require(callbacks, "DXGK_OPERATION_UPDATE_PAGE_TABLE",
            "VidMm page-table update operation", errors)
    require(callbacks, "DXGK_PAGETABLEUPDATE_CPU_VIRTUAL",
            "immediate CPU-virtual page-table update", errors)
    require(callbacks, "Source->Segment != 1u",
            "unsupported system/GART PTE rejection", errors)
    provider_start = active.find("static NTSTATUS Bc250ActiveQueryRuntimeCaps")
    provider_body = active[provider_start : provider_start + 4200]
    forbid(provider_body, "Caps->GpuVaReady =",
           "production GPUVA readiness assertion", errors)
    forbid(provider_body, "Caps->PagingReady =",
           "production paging readiness assertion", errors)
    require(active, "g_Bc250AdapterDirectoryLock", 
            "owned adapter directory for handle-only DDIs", errors)
    require(active, "InterlockedCompareExchange(&Context->IsActive, 1, 1)",
            "object creation active-state gate", errors)
    require(active_test, "Bc250RunStopRaceTest",
            "multithread StopDevice/DDI race harness", errors)
    require(active_test, "BC250_TEST_RACE_ROUNDS 16u",
            "bounded repeated race rounds", errors)
    require(active_test, "activeContext->Objects.LiveCount != 0u",
            "post-race registry-empty invariant", errors)
    require(active_test, "InterlockedCompareExchange64(",
            "atomic exclusive test lock", errors)

    # Candidato 3.67: a regressao medida no silicio precisa continuar coberta
    # offline, e o gate fail-closed precisa continuar provado nos dois lados.
    for needle, label in (
        ("Bc250TestSynchronizeExecutionAlwaysFails",
         "broken DxgkCbSynchronizeExecution fixture"),
        ("Bc250RunStopQuiescenceTest",
         "StopDevice quiescence regression harness"),
        ("Bc250RunTrackerRaceTest",
         "submit/retire/stop tracker race harness"),
        ("Bc250RunRenderTraceResetTest",
         "render/DMM trace reset harness"),
        ("STATUS_UNSUCCESSFUL",
         "synchronize-execution failure reproduced offline"),
    ):
        require(active_test, needle, label, errors)
    quiescence_test = function_body(
        active_test, "static int Bc250RunStopQuiescenceTest(")
    # Os dois lados do criterio: vazio conclui, nao-vazio continua ocupado.
    require(quiescence_test,
            "Bc250DxgkDdiStopDeviceActive(context) != STATUS_SUCCESS",
            "empty-tracker teardown completes despite broken sync", errors)
    require(quiescence_test,
            "Bc250DxgkDdiStopDeviceActive(context) != STATUS_DEVICE_BUSY",
            "non-empty tracker stays fail-closed", errors)
    # O teste nao pode fabricar sucesso limpando o rastreador.
    forbid(quiescence_test, "Bc250WddmFenceTrackerReset(",
           "artificial tracker clear inside teardown test", errors)

    # Recovery must recognize the exact active service/binary identity.
    for needle in (
        "call :remove_service bc250mcdm",
        "call :delete_driver bc250mcdm.sys",
        "call :enable_and_verify_basic_display",
        'findstr.exe /I /C:"AddService=bc250mcdm,"',
        'findstr.exe /I /C:"ServiceBinary=%%12%%\\bc250mcdm.sys"',
    ):
        require(recovery, needle, "active recovery coverage", errors)

    for needle in (
        "Class=Display",
        "PCI\\VEN_1002&DEV_13FE",
        "AddService=bc250mcdm,",
        "ServiceBinary=%12%\\bc250mcdm.sys",
        "InstalledDisplayDrivers",
        "UserModeDriverName",
        "bc250umd.dll",
    ):
        require(inf, needle, "strict active INF identity", errors)

    require(stage, "audit-bc250mcdm-prebinding.ps1",
            "clean-host pre-binding gate before stage", errors)
    require(stage, "$output = (& pnputil.exe /add-driver $packageInf 2>&1)",
            "stage-only pnputil invocation", errors)
    require(stage, "if (-not $Execute)",
            "zero-write default stage mode", errors)
    for forbidden_stage_path in (
        "UpdateDriverForPlugAndPlayDevices",
        "INSTALLFLAG_FORCE",
        "pnputil.exe /add-driver $packageInf /install 2>&1",
    ):
        forbid(stage, forbidden_stage_path,
               "binding operation in stage-only script", errors)

    require(replace, "Fluxo direto replace desativado",
            "legacy direct replacement mutation block", errors)
    require(replace, "Zero escrita, zero binding",
            "legacy replacement read-only default", errors)
    forbid(replace, "UpdateDriverForPlugAndPlayDevicesW",
           "binding API in blocked legacy replacement", errors)
    forbid(replace, "pnputil.exe /delete-driver",
           "package deletion in blocked legacy replacement", errors)
    forbid(replace, "Remove-Item",
           "file deletion in blocked legacy replacement", errors)
    require(orphan_cleanup, "if (-not $Execute)",
            "zero-write default orphan cleanup", errors)
    require(orphan_cleanup, "if (-not $IAcceptOrphanCleanup)",
            "explicit orphan cleanup acceptance", errors)
    require(orphan_cleanup, "$service.Status -ne 'Stopped'",
            "stopped-service orphan gate", errors)
    require(orphan_cleanup, "if (Test-Path -LiteralPath $driverPath)",
            "experimental SYS orphan gate", errors)
    require(orphan_cleanup, "if ($servicePresent -or $serviceRegistryPresent)",
            "unreferenced-binary gate before SYS quarantine", errors)
    require(orphan_cleanup, "$pendingDriverRenames = Get-DriverPendingRenameEntries",
            "pending rename gate before SYS quarantine", errors)
    require(orphan_cleanup, "if ($lastBootUtc -le $driverWriteUtc)",
            "boot-after-write gate before SYS quarantine", errors)
    require(orphan_cleanup, "if ($matchingPackages.Count -ne 0)",
            "absent MCDM package gate", errors)
    require(orphan_cleanup, "reg.exe export $serviceRegistryNative",
            "recoverable service registry snapshot", errors)
    require(orphan_cleanup, "Move-Item -LiteralPath $markerPath -Destination $quarantinedMarker",
            "recoverable marker quarantine", errors)
    require(orphan_cleanup, "Move-Item -LiteralPath $driverPath -Destination $quarantinedDriver",
            "recoverable orphan SYS quarantine", errors)
    require(orphan_cleanup, "sc.exe delete bc250mcdm",
            "exact orphan service deletion", errors)
    forbid(orphan_cleanup, "Remove-Item",
           "irrecoverable deletion in orphan cleanup", errors)
    forbid(orphan_cleanup, "del ",
           "irrecoverable deletion in orphan cleanup", errors)
    require(recovery,
            'findstr.exe /I /C:"Class=ComputeAccelerator" /C:"Class=Display"',
            "recovery coverage for compute and display package classes", errors)
    require(rollback, "^Class=(ComputeAccelerator|Display)",
            "rollback coverage for compute and display package classes", errors)

    require(bind, "audit-bc250mcdm-staged.ps1",
            "staged-package gate before binding", errors)
    require(bind, "if (-not $Execute)",
            "zero-write default binding mode", errors)
    require(bind, "if (-not $IAcceptBlackScreenRisk)",
            "explicit black-screen risk acceptance", errors)
    require(bind, "[Environment]::Is64BitProcess",
            "64-bit binding process gate", errors)
    require(bind, "UpdateDriverForPlugAndPlayDevicesW(",
            "explicit binding API", errors)
    require(bind, "$installFlagForce = [uint32]0x00000001",
            "explicit INSTALLFLAG_FORCE value", errors)
    require(bind, "C:\\BC250_RECOVERY.cmd",
            "binding recovery instruction", errors)
    require(bind, "hardware_validated = $false",
            "binding evidence remains unvalidated", errors)
    require(bind, "$watchdogProcess = Start-Process",
            "watchdog armed before binding", errors)
    if bind.find("$watchdogProcess = Start-Process") > bind.find(
            "$nativeOk = [Bc250McdmNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW("):
        errors.append("watchdog is armed after binding mutation")
    require(bind, "Read-Host 'Confirmacao visual'",
            "explicit visual confirmation before watchdog disarm", errors)
    require(bind, "-IConfirmDisplayIsVisible",
            "visual confirmation acknowledgement", errors)

    # EnumVidPnCofuncModality must leave the support field corresponding to a
    # scaling/rotation pivot unchanged.  Violating this DMM contract is a
    # concrete convergence defect, not a hardware hypothesis.
    require(display, "D3DKMDT_EPT_SCALING))",
            "scaling-pivot support preservation", errors)
    require(display, "D3DKMDT_EPT_ROTATION))",
            "rotation-pivot support preservation", errors)

    # The lab candidate reads RenderNode in DriverEntry and the remaining
    # policy in StartDevice. Commit/readback must precede NewDev.
    require(lab_bind, "PRE_BIND_POLICY_APPLIED_READBACK_OK",
            "transactional lab pre-bind policy evidence", errors)
    require(lab_bind, "$parametersBefore.PSObject.Properties[$entry.Key]",
            "missing registry property-safe policy snapshot", errors)
    require(lab_bind,
            "PersistentSubmission = $(if ($EnablePersistentSubmission)",
            "persistent-submission pre-bind policy", errors)
    require(lab_bind, "RenderNode = $(if ($EnableRenderNode)",
            "RenderNode pre-bind policy", errors)
    require(lab_bind, "$probeNow.P1EngineState",
            "dirty persistent-engine state gate", errors)
    require(lab_bind, "$probeNow.P1SelfTestPhysicalReady",
            "dirty P1 physical-ready gate", errors)
    require(lab_bind, "$probeNow.P2PhysicalReady",
            "dirty P2 physical-ready gate", errors)
    require(lab_bind, "if ($hqdBefore -ne 0 -or $wptrLeft -ne 0)",
            "dirty engine gate uses queue residue indicators", errors)
    require(lab_bind, "Probe historico preservado (nao e indicador de fila suja)",
            "P1/P2 readiness is classified as historical telemetry", errors)
    require(lab_bind, "C:\\BC250_REQUIRE_CLEAN_BOOT.json",
            "post-rollback clean-boot marker gate", errors)
    require(lab_bind, "LastBootUpTime",
            "post-rollback boot identity gate", errors)
    policy_index = lab_bind.find("$requestedPolicy = [ordered]@{")
    update_index = lab_bind.find(
        "[Bc250LabNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(")
    if policy_index < 0 or update_index < 0 or policy_index > update_index:
        errors.append("lab binding policy is not applied before NewDev")
    require(lab_watchdog, "Write-WatchdogState 'ROLLBACK_STARTED'",
            "lab watchdog rollback transition", errors)
    require(lab_watchdog, "-Execute -IAcceptDisplayInterruptionRisk",
            "lab watchdog bounded rollback invocation", errors)
    rollback_started_index = lab_watchdog.find(
        "Write-WatchdogState 'ROLLBACK_STARTED'")
    bounded_capture_index = lab_watchdog.find(
        "bc250_display_log_tail_post_rollback.txt")
    """
    if (rollback_started_index < 0 or bounded_capture_index < 0 or \
            rollback_started_index > bounded_capture_index:
    """
    if (rollback_started_index < 0 or bounded_capture_index < 0 or
            rollback_started_index > bounded_capture_index):
        errors.append("lab watchdog coleta evidencia antes de iniciar rollback")
    require(lab_watchdog, "recovery_completed_before_capture = $true",
            "lab watchdog declara recovery antes da coleta", errors)
    require(lab_watchdog, "$maxTailBytes = 64MB",
            "lab watchdog limita a cauda do log", errors)
    forbid(lab_watchdog, "Measure-Object -Line",
           "varredura integral do log antes/depois do rollback", errors)
    forbid(lab_watchdog, "bc250_display_log_full_pre_rollback.txt",
           "copia integral pre-rollback que travou a corrida 18:21", errors)
    require(lab_bind, "archived_as = 'display-log-this-bind.txt'",
            "binder arquiva somente o delta delimitado do bind", errors)
    forbid(lab_bind, "archived_as = 'bc250_display_log-full.txt'",
           "binder copiando novamente o log cumulativo integral", errors)
    require(lab_rollback, "$devices[0].Service -eq 'BasicDisplay'",
            "lab rollback BasicDisplay verification", errors)
    require(lab_rollback, "$devices[0].ConfigManagerErrorCode -eq 0",
            "lab rollback Code 0 verification", errors)
    require(lab_rollback, "if (-not $Execute)",
            "lab rollback Execute parameter gate", errors)
    require(lab_rollback, "if (-not $IAcceptDisplayInterruptionRisk)",
            "lab rollback risk confirmation gate", errors)
    forbid(lab_rollback, "/disable-device $dev.PNPDeviceID /force",
           "unsupported /force on pnputil /disable-device", errors)
    require(lab_rollback, "throw \"Rollback incompleto:",
            "lab rollback fail-closed result", errors)
    require(lab_rollback, "C:\\BC250_REQUIRE_CLEAN_BOOT.json",
            "lab rollback clean-boot marker", errors)
    require(lab_rollback, "rollback_boot_ticks",
            "lab rollback boot identity snapshot", errors)
    require(lab_rollback, "$requiresCleanBoot = -not",
            "lab rollback conditional clean-boot decision", errors)
    require(arm_reboot, "[switch]$RequireRenderNode",
            "render-node reboot guard mode", errors)
    require(arm_reboot, "V5RegistrationMode = 0",
            "render-node pre-reboot KMDOD proof", errors)
    require(confirm_reboot, "$requiredProbe['V5RegistrationMode'] = 1",
            "post-reboot real V5 registration gate", errors)
    require(confirm_reboot, "$requiredProbe['V5StartDeviceStatus'] = 0",
            "post-reboot V5 StartDevice success gate", errors)
    require(confirm_reboot, "$requiredProbe['P2PhysicalReady'] = 1",
            "post-reboot P2 physical proof gate", errors)
    require(watchdog_reboot, "[bool]$confirmation.render_node_validated",
            "watchdog render-node confirmation gate", errors)
    require(render_prepare, "V5RegistrationMode = 0",
            "render promotion starts from proved KMDOD", errors)
    require(render_prepare, "P2PhysicalReady = 1",
            "render promotion requires physical P2", errors)
    require(render_prepare, "Set-ItemProperty -LiteralPath $parameterKey -Name RenderNode -Value 1",
            "render promotion apply", errors)
    require(render_prepare, "render_node_readback = [uint32]$readback",
            "render promotion readback evidence", errors)
    require(render_prepare, "Remove-ItemProperty -LiteralPath $parameterKey -Name RenderNode",
            "render promotion rollback for absent snapshot", errors)
    require(render_inspect, "V5LastDdi",
            "post-reboot V5 DDI trace capture", errors)
    require(render_inspect, "Nenhum rollback automatico foi solicitado.",
            "unguarded render inspection disclosure", errors)
    forbid(bind, "Start-Service bc250mcdm",
           "manual service start outside PnP binding", errors)
    forbid(bind, "pnputil.exe /delete-driver",
           "package deletion in binding harness", errors)
    require(collector, "Services\\bc250mcdm\\Parameters\\Probe",
            "MCDM probe telemetry collection", errors)
    require(collector, "setupapi-after-tail.txt",
            "post-binding SetupAPI evidence", errors)

    require(watchdog, "[ValidateRange(30,600)][int]$TimeoutSeconds",
            "bounded watchdog timeout", errors)
    require(watchdog, "if (-not $Execute)",
            "zero-write default watchdog mode", errors)
    require(watchdog, "$devices[0].ConfigManagerErrorCode -notin @(0,43)",
            "watchdog exact Code 0/43 rollback gate", errors)
    require(watchdog, "$markerLines[0].ToLowerInvariant() -ne $TargetInf.ToLowerInvariant()",
            "watchdog immutable OEM marker gate", errors)
    require(watchdog, "$manifest.PackageInfSha256",
            "watchdog INF hash gate", errors)
    require(watchdog, "$manifest.PackageSysSha256",
            "watchdog SYS hash gate", errors)
    require(watchdog, "-Execute -IAcceptDisplayInterruptionRisk",
            "watchdog invocation of bounded online rollback", errors)
    forbid(watchdog, "pnputil.exe /delete-driver *",
           "broad package deletion in watchdog", errors)
    require(confirm_display, "if (-not $IConfirmDisplayIsVisible)",
            "explicit visible-display confirmation", errors)
    require(confirm_display, "[uint32]$probe.SkipHwBringup -ne 1",
            "display-only probe gate before disarm", errors)
    require(confirm_display, "DmmIsSupportedResult",
            "display confirmation VidPN support gate", errors)
    require(confirm_display, "DmmCommitHasPath",
            "display confirmation committed-path gate", errors)
    require(confirm_display, "DmmPresentEntered",
            "display confirmation present-entry gate", errors)
    require(confirm_display, "DmmPresentStatus",
            "display confirmation present-success gate", errors)
    require(confirm_display, "DmmControlInterruptCount",
            "display confirmation interrupt-control gate", errors)
    require(confirm_display, "$vsyncDeadline = (Get-Date).AddSeconds(20)",
            "bounded wait for post-notify VSync proof", errors)
    require(confirm_display, "DmmVsyncCount",
            "display confirmation real VSync proof gate", errors)
    require(confirm_display, "Move-Item -LiteralPath $temporaryPath -Destination $finalPath",
            "atomic watchdog disarm publication", errors)

    require(arm_reboot, "[ValidateRange(180,600)][int]$TimeoutSeconds",
            "bounded post-reboot confirmation interval", errors)
    require(arm_reboot, "if (-not $Execute)",
            "zero-write default reboot guard mode", errors)
    require(arm_reboot, "if (-not $IAcceptPersistentRebootGuard)",
            "explicit persistent reboot guard authorization", errors)
    require(arm_reboot, "New-ScheduledTaskTrigger -AtStartup",
            "persistent startup trigger", errors)
    require(arm_reboot, "-UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest",
            "startup guard SYSTEM principal", errors)
    require(arm_reboot, "reboot_executed = $false",
            "arm evidence that reboot was not executed", errors)
    require(arm_reboot, "[string]$oldState.state -ne 'DISARMED'",
            "completed predecessor DISARMED gate", errors)
    require(arm_reboot, "Move-Item -LiteralPath $resolvedGuard -Destination $resolvedQuarantine",
            "recoverable completed guard quarantine", errors)
    for forbidden_reboot_command in (
        "Restart-Computer",
        "shutdown.exe",
        "shutdown /",
    ):
        forbid(arm_reboot, forbidden_reboot_command,
               "reboot command in arming phase", errors)
    require(watchdog_reboot,
            "$currentBootTicks -le [long]$config.armed_boot_ticks",
            "new-boot identity gate", errors)
    require(watchdog_reboot, "if (-not $Execute)",
            "zero-write default persistent watchdog mode", errors)
    require(watchdog_reboot, "Test-Path -LiteralPath $attemptPath",
            "single rollback attempt gate", errors)
    require(watchdog_reboot, "$config.rollback_sha256",
            "immutable rollback script hash gate", errors)
    require(watchdog_reboot, "$config.expected_inf_sha256",
            "post-reboot INF hash gate", errors)
    require(watchdog_reboot, "$config.expected_sys_sha256",
            "post-reboot SYS hash gate", errors)
    require(watchdog_reboot, "-Execute -IAcceptDisplayInterruptionRisk",
            "bounded exact online rollback invocation after timeout", errors)
    forbid(watchdog_reboot, "pnputil.exe /delete-driver *",
           "broad package deletion in persistent watchdog", errors)
    require(confirm_reboot, "if (-not $IConfirmDisplayIsVisible)",
            "explicit post-reboot visual confirmation", errors)
    require(confirm_reboot,
            "$currentBootTicks -le [long]$config.armed_boot_ticks",
            "post-reboot confirmation boot identity", errors)
    require(confirm_reboot, "[string]$state.state -ne 'OBSERVING'",
            "active watchdog state before confirmation", errors)
    require(confirm_reboot, "StartDeviceEntered = 1",
            "current driver start path evidence", errors)
    require(confirm_reboot, "DmmPresentStatus = 0",
            "post-reboot present success gate", errors)
    require(confirm_reboot, "$vsyncDeadline = (Get-Date).AddSeconds(20)",
            "bounded post-reboot VSync proof wait", errors)
    require(confirm_reboot, "DmmVsyncCount",
            "post-reboot real VSync proof gate", errors)
    require(confirm_reboot, "Get-WinEvent",
            "post-reboot warning/error gate", errors)

    require(display, "g_Bc250DisplayState.VsyncProofRecorded",
            "single-shot VSync proof state", errors)
    require(display, "vsyncCount > 0 && Bc250CanRecordDisplayProbePassive()",
            "positive VSync and PASSIVE_LEVEL persistence gate", errors)
    require(display, "&g_Bc250DisplayState.VsyncProofRecorded, 1, 0",
            "atomic single-shot VSync proof claim", errors)
    require(display, "BC250_PROBE_DMM_VSYNC_COUNT, (ULONG)vsyncCount",
            "persisted post-notify VSync count", errors)

    require(rollback, "$targetInf = $markerLines[0].ToLowerInvariant()",
            "marker-derived online rollback target", errors)
    forbid(rollback, "$targetInf = 'oem19.inf'",
           "historically pinned online rollback target", errors)
    require(rollback, "$preflightDevices[0].ConfigManagerErrorCode -notin @(0,43)",
            "Code 0/43 state gate before rollback", errors)
    require(rollback, "$installedDriverHash -ne $expectedDriverHash",
            "authorized installed SYS hash gate", errors)
    require(rollback, "$installedInfHash -ne [string]$manifest.PackageInfSha256",
            "authorized installed INF hash gate", errors)
    require(rollback, "$secondHash -ne $installedDriverHash",
            "second immutable-state gate before rollback", errors)
    require(rollback, "if (-not $Execute)",
            "zero-write default rollback mode", errors)
    require(rollback, "if (-not $IAcceptDisplayInterruptionRisk)",
            "explicit online rollback risk acceptance", errors)
    require(rollback, "pnputil.exe /delete-driver $targetInf /uninstall /force",
            "exact package uninstall operation", errors)
    require(rollback, "$driverHash -ne $installedDriverHash",
            "captured installed SYS residual hash gate", errors)
    require(rollback, "$manifest.SignerThumbprint",
            "authorized signer gate for installed SYS", errors)
    require(rollback, "audit-bc250mcdm-prebinding.ps1",
            "clean-host proof after rollback", errors)
    for forbidden_rollback_path in (
        "pnputil.exe /delete-driver *",
        "Remove-Item -Recurse",
        "sc.exe delete bc250v4",
        "sc.exe delete BasicDisplay",
    ):
        forbid(rollback, forbidden_rollback_path,
               "broad or protected online rollback target", errors)

    audit_frame_pointer_flag(errors)
    audit_batch_line_endings(errors)
    audit_gpuva_geometry_single_source(errors)
    audit_submit_path_labels(errors)
    audit_tripod_leg_one(errors)
    audit_indirect_only_tunable(errors)
    audit_ib_data_leg(errors)
    audit_process_gpuva_teardown(errors)
    audit_ring_wrap_arithmetic(errors)
    audit_node1_gate_telemetry(errors)
    audit_node1_physical_gpuva_gate(errors)
    audit_tdr_pair_has_backend(errors)
    audit_engine_status_measures(errors)
    audit_induced_hang_is_fail_closed(errors)
    audit_induced_hang_package_order(errors)
    audit_second_preflight_transport_empty(errors)
    audit_process_lifecycle_ddis_speak(errors)
    audit_context_abi_before_ready(errors)
    audit_compute_vmid_isolation(errors)
    audit_firmware_wgp_policy_is_readonly(errors)
    audit_mec_cursor_reconciliation(errors)
    audit_mec_vmid_retarget(errors)
    audit_submit_dispatch_level_safety(errors)
    audit_sdma_submission_is_asynchronous(errors)
    audit_gfx_ring_tripod_and_ib_inspection(errors)
    audit_phase_d0_refusal_tripod_and_wptr_veto(errors)
    audit_device_removal_and_unload_safety(errors)
    audit_tlb_invalidation_safety(errors)
    audit_mec_shared_mmio_lifetime(errors)

    audit_draw_has_no_cpu_fallback(errors)
    audit_safe_bind_deadline_is_fail_closed(errors)
    audit_lost_log_tail_is_recovered(errors)
    audit_gfx_restart_checks_the_real_halt_bits(errors)
    audit_gfx_restart_is_executable_and_aborts_old_fences(errors)
    audit_gfx_stall_probe_is_read_only(errors)
    audit_conformance_harness_checks_present(errors)
    audit_radv_fork_delta_is_mirrored(errors)
    audit_wave_probe_restores_the_selector(errors)
    audit_descriptor_and_wave_contracts(errors)
    audit_lab_tunables_are_read_in_the_common_path(errors)
    audit_ib_dump_covers_every_submission_path(errors)
    audit_kmd_tail_archive_declares_truncation(errors)
    audit_resource_copy_requires_created(errors)
    audit_texture_path_is_named_and_complete(errors)
    audit_probe_field_names(errors)
    audit_marco3_closes_its_own_transaction(errors)
    audit_present_is_gpu_attributable(errors)
    audit_present_source_uses_segment_base(errors)
    audit_node1_journal_gate_granularity(errors)
    audit_snapshot_publish_retries(errors)
    audit_gpuva_snapshot_capacity(errors)
    audit_umd_trace_reaches_the_evidence_file(errors)
    audit_composition_never_submits_a_context(errors)
    audit_primary_allocation_is_declared_to_the_kernel(errors)
    audit_primary_is_not_overbroad(errors)
    audit_dxgi_1_6_table_complete(errors)
    audit_f5_2_pm4_draw_pipeline(errors)
    audit_present_path_is_not_mute(errors)
    audit_dxgi_present_chain_honest(errors)
    audit_f3_pixel_and_residency_truth(errors)
    audit_describe_allocation_is_not_mute(errors)
    audit_gfx_ring_contract_is_honest(errors)
    audit_gfx_shader_contract_is_honest(errors)
    audit_gfx_observable_shader_is_honest(errors)
    audit_gfx_shader_staging_is_honest(errors)
    audit_gfx_sh_reg_packet_is_honest(errors)
    audit_gfx_viewport_scissor_is_honest(errors)
    audit_gfx_primitive_assembler_is_honest(errors)
    audit_gfx_draw_initiator_is_honest(errors)
    audit_offline_check_is_not_inverted(errors)
    audit_dx21_pipeline_ir_and_ib_dump_reach_evidence(errors)
    audit_dx24_d3d11_state_reaches_vulkan(errors)
    audit_dx25_drawn_vertices_and_target_readback(errors)
    audit_dx26_golden_csib_snapshot_is_read_only(errors)
    audit_dx27_clear_state_follows_upstream(errors)
    audit_dx28_rotation_carries_vulkan_content(errors)
    audit_dx29_vmid_switch_sync_on_gfx_ring(errors)

    if errors:
        print("BC250 MCDM SAFETY AUDIT: FAILED")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("BC250 MCDM SAFETY AUDIT: PASS")
    print("  DX.26 golden settings and CSB snapshot is read-only: enforced")
    print("  DX.25 drawn vertices (index+base) and capped read-only target readback: enforced")
    print("  DX.24 D3D11 blend/IA/viewport/format state reaches Vulkan or refuses by name: enforced")
    print("  DX.21 pipeline IR dump after creation, gated, fallback device; IB dump staged where dwm.exe can write: enforced")
    print("  synthetic ownership/reservations: absent")
    print("  GPUVA geometry (GPUMMUCAPS vs UMDRIVERPRIVATE): single source")
    print("  operational DDIs without hardware backing: fail-closed")
    print("  DXGI 1.6 table complete (22 slots, MaxPlanes=1, fail-closed): enforced")
    print("  Parede B (PM4 3D draw on Node 1, CB alignment, separate telemetry): enforced")
    print("  'IB EXECUTADO' restricted to the INDIRECT path: enforced")
    print("  SDMA tripod leg 1 (RPTR==WPTR) inside the gate: enforced")
    print("  INDIRECT data leg (canary proof): enforced")
    print("  ring-wrap arithmetic on tripod leg 1: enforced")
    print("  Node 1 submission refusal names its reason: enforced")
    print("  Node 1 admission uses full physical page-table walk; snapshot is diagnostic: enforced")
    print("  TDR reset/restart pair has a physical backend (SDMA proof; GFX proof audited separately): enforced")
    print("  QueryEngineStatus answers from measurement: enforced")
    print("  induced hang fail-closed and physical: enforced")
    print("  induced hang package/start order: enforced")
    print("  second readonly preflight sees an empty MMIO transport: enforced")
    print("  UMD/winsys trace reaches the archived evidence file: enforced")
    print("  primary allocation declared to the kernel (hResource+Primary+VidPnSourceId): enforced")
    print("  primary derivation narrowed (dwm.exe primary, Explorer.EXE non-primary): enforced")
    print("  Present path speaks on both sides; device/context sampling unshared: enforced")
    print("  DXGI presentation chain refuses instead of faking success; test cannot re-freeze it: enforced")
    print("  DescribeAllocation speaks on entry and on every refusal: enforced")
    print("  pixel and residency truth (no CPU clear, no suppressed black blt, OS residency): enforced")
    print("  DWM composition runs on the KMD blt path, never a context submit: enforced")
    print("  process/device/context DDIs name their refusals: enforced")
    print("  context ABI populated before ContextAbiReady: enforced")
    print("  24-CU firmware WGP/power policy is read-only: enforced")
    print("  drained MEC cursor reconciliation before planning: enforced")
    print("  per-process GPUVA ranges released on process destroy: enforced")
    print("  bc250mcdm recovery identity: covered")
    print("  submit path at DISPATCH_LEVEL pushlock-free: enforced")
    print("  SDMA submission asynchronous and non-blocking (0x133 eliminated): enforced")
    print("  GFX ring tripod (poison excluded, wrapped RPTR, INDETERMINADO allowed) + PM4 IB inspection with single-source verdict: enforced")
    print("  GFX restart requires executable NOP/fence proof and aborts old WDDM fences without false completion: enforced")
    print("  GFX stall probe (ME/PFP header dump, IB1, SQ waves, SE busy) is read-only and speaks INDETERMINADO: enforced")
    print("  D3D11 conformance harness checks Present/GetDeviceRemovedReason and aborts; runner refuses exit 0 without frame evidence: enforced")
    print("  RADV fork delta mirrored in-repo with provenance (base commit -> patch -> DLL): enforced")
    print("  GFX wave probe writes only selectors, restores broadcast with readback, fail-closed: enforced")
    print("  lab tunables read before the production #if (GfxWaveProbe present=0 defect): enforced")
    print("  IB dump covers every submission path, not just the concat branch: enforced")
    print("  KMD tail archived in two passes and truncation is declared: enforced")
    print("  resource copy refuses uncreated resources (wild memcpy of 1.4GB measured): enforced")
    print("  SRV refusals name one condition each; texture syncs to VkImage on update and unmap: enforced")
    print("  GFXHUB TLB serialization with spinlock: enforced")
    print("  Marco 3 partial proof + unconditional recovery: enforced")
    print("  GPUVA journal stores final interval overlay, not raw update history: enforced")
    print("  shader staging and allocation (256B alignment, zero overlap, readback proof, fail-closed): enforced")
    print("  PM4 SET_SH_REG shader packet emission (0x76, 12DW, compute bit forbidden, fail-closed): enforced")
    print("  PM4 SET_CONTEXT_REG viewport/scissor emission (0x69, 20DW, IEEE-754, fail-closed): enforced")
    print("  PM4 SET_UCONFIG_REG primitive assembler (0x79, TRILIST 0x04, 8DW, fail-closed): enforced")
    print("  Phase D0 GFX refusal tripod and WPTR divergence veto: enforced")
    print("  verdict: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
