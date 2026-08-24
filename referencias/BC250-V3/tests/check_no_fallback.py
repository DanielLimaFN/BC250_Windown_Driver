#!/usr/bin/env python3
"""T8 - Detector de fallback por software (Fase 0).

Roda offline, sem hardware. Falha o build se encontrar no codigo-fonte os
padroes que a regra BC-250 proibe:

  1. Loop aritmetico em CPU dentro de funcao de Dispatch/Compute/Submit.
     (Foi assim que 'pC[k] = pA[k] + pB[k]' viveu dentro do KMD durante meses,
      produzindo o "resultado correto" que a suite exibia como aceleracao.)

  2. Retorno de sucesso acompanhado de texto que declara execucao no silicio,
     sem verificacao de fence/RPTR por perto.

  3. Telemetria com valor literal plausivel devolvida junto de codigo de
     sucesso (o classico 48000 mC / 42 W do atiadlxx.c).

  4. Reintroducao de offsets de registrador comprovadamente invalidos.

Uso:
    python tests/check_no_fallback.py            # a partir da raiz do BC250-V3
Saida:
    0 = limpo, 1 = violacao encontrada
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent

ARQUIVOS = [
    "wddm_hw.c",
    "wddm_driver.c",
    "opencl/bc250_opencl.c",
    "opencl/opencl_loader.c",
    "adl/atiadlxx.c",
]

# Offsets que a Fase 0 provou invalidos neste silicio.
OFFSETS_PROIBIDOS = {
    "0x03040": "GRBM_STATUS antigo (lia 0x00000000 constante); correto e' 0x08010",
    "0x08684": "CP_STAT antigo (lia fill 0xDEF4DEF4); correto e' 0x08680",
    "0x05C3C": "SPI_PG antigo (lia 0 sempre); correto e' 0x0935C",
    "0x09C1C": "CC_GC_SHADER_ARRAY antigo; correto e' 0x089BC",
    "0x080E0": "COMPUTE_DISPATCH_INITIATOR antigo; correto e' 0x0B800",
}

# Funcoes cujo corpo nao pode conter aritmetica elemento-a-elemento em CPU.
FUNC_SENSIVEL = re.compile(
    r"\b\w*(Dispatch|Compute|SubmitPm4|SubmitIb|SubmitCompute)\w*\s*\(", re.I
)

# c[i] = a[i] <op> b[i]  — soma/mult/sub/div elemento a elemento
ARITMETICA_CPU = re.compile(
    r"\w+\s*\[\s*\w+\s*\]\s*=\s*\w+\s*\[\s*\w+\s*\]\s*[\+\-\*/]\s*\w+\s*\[\s*\w+\s*\]"
)

TELEMETRIA_FABRICADA = re.compile(
    r"(lpTemperature|iTemperature|lpCurrentValue|PowerWatts|GfxClockMhz|GfxTemperatureC)"
    r"\s*(->\s*\w+\s*)?=\s*(\d{2,6})\s*;"
)

TEXTO_TRIUNFALISTA = re.compile(
    r"(100%\s*(OPERACIONAL|VALIDADO)|SUCESSO\s+TOTAL|Build Success|"
    r"COMPUTAM COM SUCESSO|VALIDADO EM SILICIO REAL)", re.I
)


COMENTARIO = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def sem_comentarios(texto: str) -> str:
    """Remove comentarios C preservando numeros de linha.

    Comentarios sao documentacao, nao codigo: descrever o fallback que foi
    removido nao pode disparar o detector. Cada comentario vira espacos, e as
    quebras de linha sao mantidas para que os numeros de linha continuem certos.
    """
    def branco(m: re.Match) -> str:
        return "".join("\n" if c == "\n" else " " for c in m.group(0))
    return COMENTARIO.sub(branco, texto)


def corpo_de_funcoes_sensiveis(texto: str):
    """Rende (nome, trecho) de cada funcao cujo nome casa FUNC_SENSIVEL."""
    for m in FUNC_SENSIVEL.finditer(texto):
        inicio = texto.find("{", m.end())
        if inicio < 0:
            continue
        nivel, i = 0, inicio
        while i < len(texto):
            if texto[i] == "{":
                nivel += 1
            elif texto[i] == "}":
                nivel -= 1
                if nivel == 0:
                    break
            i += 1
        yield texto[m.start():m.end()], texto[inicio:i]


def linha_de(texto: str, pos: int) -> int:
    return texto.count("\n", 0, pos) + 1


def binarios_desatualizados() -> list[str]:
    """Fail-closed: binario que nao corresponde ao codigo atual invalida a suite.

    AI_INSTRUCTIONS.md, secao "Testes fail-closed": a suite deve falhar
    globalmente quando "o binario nao corresponder ao codigo atual". Rodar
    executaveis velhos contra um driver novo ja' produziu resultado enganoso
    nesta sessao mais de uma vez.
    """
    testes = RAIZ / "tests"
    binario = testes / "bin"
    if not binario.is_dir():
        return ["tests/bin ausente - rode build_tests.bat"]

    fontes = list(testes.glob("*.c")) + list(testes.glob("*.h"))
    if not fontes:
        return []
    fonte_mais_nova = max(fontes, key=lambda f: f.stat().st_mtime)
    t_fonte = fonte_mais_nova.stat().st_mtime

    velhos = []
    for exe in binario.glob("*.exe"):
        if exe.stat().st_mtime < t_fonte:
            velhos.append(
                "%s e' mais antigo que %s" % (exe.name, fonte_mais_nova.name)
            )
    return velhos


def main() -> int:
    violacoes: list[str] = []

    for v in binarios_desatualizados():
        violacoes.append("binario desatualizado: " + v)

    for rel in ARQUIVOS:
        caminho = RAIZ / rel
        if not caminho.exists():
            print(f"  aviso: {rel} nao encontrado, pulando")
            continue
        bruto = caminho.read_text(encoding="utf-8", errors="replace")
        texto = sem_comentarios(bruto)

        # 1. aritmetica de CPU em funcao de dispatch/submit
        for assinatura, corpo in corpo_de_funcoes_sensiveis(texto):
            for m in ARITMETICA_CPU.finditer(corpo):
                violacoes.append(
                    f"{rel}: aritmetica elemento-a-elemento em CPU dentro de "
                    f"'{assinatura.strip()}' -> {m.group(0).strip()}"
                )

        # 2. offsets invalidos reintroduzidos
        for off, motivo in OFFSETS_PROIBIDOS.items():
            for m in re.finditer(re.escape(off) + r"[uU]?\b", texto):
                ctx = texto[max(0, m.start() - 200):m.start()]
                if "LEGACY" in ctx.upper() or "PROIBID" in ctx.upper():
                    continue  # referencia deliberada em bloco de regressao
                violacoes.append(
                    f"{rel}:{linha_de(texto, m.start())}: offset proibido {off} — {motivo}"
                )

        # 3. telemetria com literal plausivel
        if rel.endswith("atiadlxx.c"):
            for m in TELEMETRIA_FABRICADA.finditer(texto):
                valor = int(m.group(3))
                if valor in (0,):
                    continue
                violacoes.append(
                    f"{rel}:{linha_de(texto, m.start())}: possivel telemetria "
                    f"fabricada -> {m.group(0).strip()}"
                )

        # 4. texto triunfalista no driver/UMD
        for m in TEXTO_TRIUNFALISTA.finditer(texto):
            violacoes.append(
                f"{rel}:{linha_de(texto, m.start())}: declaracao de sucesso "
                f"sem prova -> {m.group(0).strip()!r}"
            )

    print("=" * 65)
    print(" T8 - Detector de fallback por software (BC250-V3, Fase 0)")
    print("=" * 65)

    if violacoes:
        print(f"\n[FALHOU] {len(violacoes)} violacao(oes) da regra BC-250:\n")
        for v in violacoes:
            print(f"  - {v}")
        print("\nNenhuma dessas construcoes pode existir no codigo. Corrija antes")
        print("de compilar. Ver ANALISE_REFERENCIAS_E_PLANO.md secao 3.")
        return 1

    print("\n[OK] Nenhum fallback de software, offset invalido ou declaracao")
    print("     de sucesso sem prova encontrado nos arquivos verificados.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
