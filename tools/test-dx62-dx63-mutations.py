"""DX.62/DX.63: cada mutacao tem de reprovar a auditoria de seguranca.

Restauracao por bytes com conferencia de SHA-256 por arquivo.
"""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
ACTIVE = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
CALLBACKS = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
DISPLAY = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
UMD = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"

MUTATIONS = [
    ("DX.62 recusa do Open volta a ser amostrada", ACTIVE,
     b'"[ActiveOpenAlloc] RECUSADO: size/alignment mismatch',
     b'"[ActiveOpenAlloc] size/alignment mismatch'),
    ("DX.62 LEAVE com falha volta a ser mudo", ACTIVE,
     b'Bc250DisplayLog("[ActiveOpenAlloc] RECUSADO LEAVE:',
     b'Bc250DisplayLog("[ActiveOpenAlloc] LEAVE (falha):'),
    ("DX.62 Open nao anotado por token", ACTIVE,
     b"noteEntry->OpenLastStatus = (UINT32)result;",
     b"noteEntry->OpenLastStatus = 0u;"),
    ("DX.62 Destroy nao anotado", ACTIVE,
     b"Bc250AllocQueryNoteDestroy(DestroyAllocation->pAllocationList[index], result);",
     b"(void)DestroyAllocation->pAllocationList[index];"),
    ("DX.62 wrapper do Open mudo", CALLBACKS,
     b'Bc250DisplayLog("[McdmOpenAllocation] RECUSADO: sem backend -> STATUS_NOT_SUPPORTED\\n");',
     b"/* mudo */"),
    ("DX.62 UMD nao imprime o seguimento", UMD,
     b"OpenCalls=%u OpenStatus=0x%08X DestroyCalls=%u DestroyStatus=0x%08X",
     b"OpenCalls=%u OpenStatus=0x%08X DestroyCalls=%u Destroy=0x%08X"),
    ("DX.63 Blt volta ao scanout (so' subjanela)", DISPLAY,
     b"BOOLEAN bltToDestination = (pPresent->Flags.Blt != 0u) ? TRUE : FALSE;",
     b"BOOLEAN bltToDestination = FALSE;"),
    ("DX.63 destino pode sobrepor o scanout", DISPLAY,
     b'return "DESTINO_SOBREPOE_O_SCANOUT";',
     b'(void)0;'),
    ("DX.63 escala aceita", DISPLAY,
     b'return "RETANGULOS_VAZIOS_OU_COM_ESCALA";',
     b'(void)0;'),
    ("DX.63 supressao de branco aplicada ao Blt", DISPLAY,
     b"BOOLEAN skipBlankUnderEscape = (!bltToDestination &&",
     b"BOOLEAN skipBlankUnderEscape = (TRUE &&"),
    ("DX.63 byte proof do scanout aplicado ao Blt", DISPLAY,
     b"                        if (bltToDestination) {\r\n                            /* DX.63: o Blt nao escreveu",
     b"                        if (FALSE) {\r\n                            /* DX.63: o Blt nao escreveu"),
    ("DX.63b staging sem geometria", CALLBACKS,
     b"pStaging != NULL ? &geometry : NULL);",
     b"NULL);"),
    ("DX.63b primaria ganha geometria", CALLBACKS,
     b"BC250_STD_ALLOC_TOKEN_PRIMARY, bytes, NULL);",
     b"BC250_STD_ALLOC_TOKEN_PRIMARY, bytes, &g_Bc250Unused);"),
    ("DX.63b geometria sem conferir tamanho", CALLBACKS,
     b"(UINT64)Geometry->Pitch * Geometry->Height <= aligned",
     b"TRUE"),
    ("DX.63b formato GDI inventado", CALLBACKS,
     b"geometry.Format = (UINT32)pGdi->Format;",
     b"geometry.Format = 21u;"),
    ("DX.64 copia supoe COLOR_ATTACHMENT na origem", UMD,
     b"barriers[0].oldLayout = srcOld;",
     b"barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;"),
    ("DX.64 destino nao grava layout", UMD,
     b"    dst->VkLayout = dstFinal;",
     b"    (void)dstFinal;"),
    ("DX.64 declara conteudo sem origem escrita", UMD,
     b"dstFinal = VK_IMAGE_LAYOUT_UNDEFINED;   /* nada",
     b"dstFinal = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;   /* nada"),
    ("DX.64 barreira so' do subrecurso 0", UMD,
     b"barriers[1].subresourceRange.levelCount = dst->MipLevels ? dst->MipLevels : 1u;",
     b"barriers[1].subresourceRange.levelCount = 1;"),
    ("DX.65 batimento removido", CALLBACKS,
     b"record.Reserved = ++writerTick;",
     b"(void)++writerTick;"),
    ("DX.65 checksum nao recalculado", CALLBACKS,
     b"        record.Checksum = Bc250PagingTraceChecksum(&record);",
     b"        (void)0;"),
    ("DX.63 resolvedor nao instalado", ACTIVE,
     b"    Bc250DisplayInstallAllocationGeometryResolver(Bc250ActiveResolveAllocationGeometry);",
     b"    /* resolvedor removido */"),
]


def audit() -> bool:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True).returncode == 0


def main() -> int:
    originals = {p: p.read_bytes() for p in {m[1] for m in MUTATIONS}}
    digests = {p: hashlib.sha256(b).hexdigest() for p, b in originals.items()}
    if not audit():
        print("DX.62/63: arvore inicial nao passou na auditoria")
        return 1
    failures = 0
    for name, path, good, bad in MUTATIONS:
        original = originals[path]
        if original.count(good) != 1:
            print(f"{name}: alvo ausente ou ambiguo ({original.count(good)})")
            failures += 1
            continue
        try:
            path.write_bytes(original.replace(good, bad, 1))
            caught = not audit()
        finally:
            path.write_bytes(original)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digests[path]:
            print(f"RESTAURACAO FALHOU: {path} mutado em disco")
            return 2
        print(f"{name}: {'REPROVOU' if caught else 'FALHA DO PORTAO'}")
        failures += 0 if caught else 1
    if not audit():
        print("DX.62/63: auditoria nao voltou a passar apos restaurar")
        return 1
    print(f"DX.62/63: {len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
