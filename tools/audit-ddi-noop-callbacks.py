#!/usr/bin/env python3
"""Audit anunciada-mas-vazia: DDIs que devolvem sucesso sem produzir efeito.

Motivacao
---------
O 3.74 anunciava `DxgkDdiSetDisplayPrivateDriverFormat` com um corpo que
ignorava os dois argumentos e devolvia `STATUS_SUCCESS`.  Isso e' um falso
positivo puro: o dxgkrnl registra a DDI como suportada, o driver nao faz
nada, e a divergencia so aparece muito depois, como comportamento
inexplicavel no silicio.

Corrigir aquele caso especifico nao resolve a classe.  Esta auditoria varre a
classe inteira, de forma automatica, e falha o build quando ela reaparece.

Criterio de REPROVACAO (confirmado)
-----------------------------------
Uma funcao e' reprovada quando as tres condicoes valem ao mesmo tempo:

  1. ela e' ANUNCIADA — atribuida a um campo `DxgkDdi*` de alguma estrutura
     de inicializacao no codigo do driver;
  2. TODOS os seus parametros aparecem exclusivamente dentro de
     `UNREFERENCED_PARAMETER(...)`, isto e', nenhum argumento e' lido;
  3. todo `return` do corpo devolve `STATUS_SUCCESS`.

Uma funcao que ignora tudo que recebe e mesmo assim declara sucesso nao pode
ter produzido efeito verificavel: nao preencheu saida, nao atualizou estado
derivado de entrada, nao programou hardware.

Saidas corretas para um caso reprovado, nesta ordem de preferencia:
  - implementar a semantica exigida pelo contrato;
  - devolver um erro documentado (`STATUS_NOT_SUPPORTED`) quando a DDI e'
    obrigatoria mas o backend nao existe;
  - NAO anuncia-la, quando ela e' opcional no contrato selecionado.

Criterio de AVISO (revisar)
---------------------------
Funcao anunciada que le argumentos apenas para validacao, nao chama nada, nao
atribui nada e devolve `STATUS_SUCCESS`.  Pode ser legitima — uma DDI cuja
resposta correta e' "nenhuma capacidade" — mas precisa de justificativa
escrita.  Avisos nao reprovam o build.

Uso
---
    python tools/audit-ddi-noop-callbacks.py
    python tools/audit-ddi-noop-callbacks.py --list

Codigo de saida 0 quando nenhuma funcao anunciada e' reprovada.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = [ROOT / "driver" / "wdk", ROOT / "driver" / "core"]

# --------------------------------------------------------------------------
# Divida tecnica aceita TEMPORARIAMENTE, com prazo e dono.
#
# Estas DDIs sao falsos sucessos confirmados.  Elas foram retiradas no 3.75 e
# REINTRODUZIDAS no 3.76 de proposito: a retirada mudava a tabela KMDOD no
# mesmo candidato que estreava o perfil RenderOnlyLab, e quando aquela corrida
# terminou em tela preta e Codigo 39 nao foi possivel atribuir a falha a um
# unico eixo.
#
# Condicao de saida: fechado o Gate 1.5, remover as tres entradas daqui E as
# atribuicoes correspondentes no driver, num candidato exclusivo de limpeza.
#
# Enquanto estiverem aqui a auditoria PASSA, mas IMPRIME a divida em todas as
# execucoes.  Silencio nao e' opcao: divida invisivel vira permanente.
# --------------------------------------------------------------------------
ACCEPTED_DEBT = {
    "Bc250DxgkDdiSetDisplayPrivateDriverFormat":
        "3.76: tabela KMDOD congelada na baseline para isolar o Gate 1.5",
    "Bc250DxgkDdiSetPalette":
        "3.76: tabela KMDOD congelada na baseline para isolar o Gate 1.5",
    "Bc250DxgkDdiCollectDbgInfo":
        "3.76: stub KMDOD restaurado para isolar o Gate 1.5",
}

# Atribuicao de um campo DxgkDdi* de qualquer estrutura de inicializacao.
ANNOUNCE_RE = re.compile(
    r"(?:->|\.)\s*(DxgkDdi\w+)\s*=\s*([A-Za-z_]\w*)\s*;")

# Definicao de funcao no estilo do repositorio: tipo, nome, lista de
# parametros e chave de abertura.  Exige NTSTATUS/VOID para nao capturar
# chamadas nem prototipos.
DEFINITION_RE = re.compile(
    r"^(?:NTSTATUS|VOID|void)\s+(?:APIENTRY\s+)?"
    r"(?P<name>Bc250\w+)\s*\((?P<params>[^)]*)\)\s*$",
    re.MULTILINE)

UNREFERENCED_RE = re.compile(r"UNREFERENCED_PARAMETER\s*\(\s*(\w+)\s*\)")
RETURN_RE = re.compile(r"\breturn\b([^;]*);")
CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
ASSIGN_RE = re.compile(r"[^=!<>+\-*/%&|^]=[^=]")

# Chamadas que nao constituem efeito observavel para o chamador da DDI.
INERT_CALLS = {
    "UNREFERENCED_PARAMETER",
    "if", "while", "for", "switch", "return", "sizeof",
}


def read_sources():
    files = []
    for directory in SOURCE_DIRS:
        if directory.is_dir():
            files.extend(sorted(directory.glob("*.c")))
    return [(path, path.read_text(encoding="utf-8", errors="replace"))
            for path in files]


def collect_announced(sources):
    """Nome da funcao -> conjunto de campos DxgkDdi* aos quais foi atribuida."""
    announced = {}
    for path, text in sources:
        for field, func in ANNOUNCE_RE.findall(text):
            if func in ("NULL", "0"):
                continue
            announced.setdefault(func, set()).add(field)
    return announced


def extract_body(text, open_brace_index):
    """Devolve o corpo entre chaves balanceadas a partir de '{'."""
    depth = 0
    for index in range(open_brace_index, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace_index + 1:index]
    return None


def parameter_names(params):
    names = []
    for chunk in params.split(","):
        chunk = chunk.strip()
        if not chunk or chunk == "void" or chunk == "VOID":
            continue
        # O nome e' o ultimo identificador do fragmento (ignora [] e *).
        identifiers = re.findall(r"[A-Za-z_]\w*", chunk.replace("[]", ""))
        if identifiers:
            names.append(identifiers[-1])
    return names


def collect_definitions(sources):
    definitions = {}
    for path, text in sources:
        for match in DEFINITION_RE.finditer(text):
            brace = text.find("{", match.end())
            if brace == -1:
                continue
            body = extract_body(text, brace)
            if body is None:
                continue
            line = text.count("\n", 0, match.start()) + 1
            definitions[match.group("name")] = {
                "path": path,
                "line": line,
                "params": parameter_names(match.group("params")),
                "body": body,
            }
    return definitions


def classify(definition):
    """Devolve ('fail'|'warn', motivo) ou None.

    A distincao que importa e' entre "nao fez nada" e "nao usou o que
    recebeu".  Sao coisas diferentes:

      - uma DDI que ignora tudo E nao produz efeito algum nao pode ter
        cumprido contrato nenhum: e' falso sucesso, reprova;
      - uma DDI de notificacao pode legitimamente ignorar seus argumentos e
        ainda assim agir (quiesce, contador, flag).  Isso merece revisao, nao
        reprovacao — ignorar um argumento como RemovalType pode ser uma
        simplificacao aceitavel ou um bug, e so' a leitura humana decide.
    """
    body = definition["body"]
    params = definition["params"]

    returns = [value.strip() for value in RETURN_RE.findall(body)]
    if not returns:
        return None
    if any(value != "STATUS_SUCCESS" for value in returns):
        return None

    calls = {name for name in CALL_RE.findall(body)} - INERT_CALLS
    has_effect = bool(calls) or bool(ASSIGN_RE.search(body))

    ignored = set(UNREFERENCED_RE.findall(body))
    # Um parametro esta' "lido" se aparece fora de UNREFERENCED_PARAMETER.
    stripped = UNREFERENCED_RE.sub("", body)
    read = {name for name in params
            if re.search(r"\b%s\b" % re.escape(name), stripped)}
    all_ignored = bool(params) and not read and ignored >= set(params)

    if all_ignored and not has_effect:
        return ("fail", "ignora TODOS os argumentos e devolve "
                        "STATUS_SUCCESS sem produzir efeito")

    if all_ignored:
        return ("warn", "ignora TODOS os argumentos, mas produz efeito; "
                        "confirme que nenhum argumento deveria alterar a acao")

    if not has_effect:
        return ("warn", "le argumentos apenas para validacao e devolve "
                        "STATUS_SUCCESS sem efeito; justifique ou implemente")

    return None


def main(argv):
    sources = read_sources()
    if not sources:
        print("ERRO: nenhuma fonte encontrada em driver/wdk ou driver/core.")
        return 2

    announced = collect_announced(sources)
    definitions = collect_definitions(sources)

    failures = []
    warnings = []
    debt = []

    for func in sorted(announced):
        definition = definitions.get(func)
        if definition is None:
            continue
        verdict = classify(definition)
        if verdict is None:
            continue
        level, reason = verdict
        relative = definition["path"].relative_to(ROOT).as_posix()
        entry = (func, sorted(announced[func]), relative,
                 definition["line"], reason)
        if func in ACCEPTED_DEBT:
            debt.append((func, sorted(announced[func]), relative,
                         definition["line"], ACCEPTED_DEBT[func]))
        elif level == "fail":
            failures.append(entry)
        else:
            warnings.append(entry)

    if "--list" in argv:
        print("Callbacks DxgkDdi* anunciados: %d" % len(announced))
        for func in sorted(announced):
            where = definitions.get(func)
            location = ("%s:%d" % (where["path"].relative_to(ROOT).as_posix(),
                                   where["line"])) if where else "(externo)"
            print("  %-46s %s" % (func, location))
        print()

    for func, fields, relative, line, reason in debt:
        print("DIVIDA %s (%s)" % (func, ", ".join(fields)))
        print("       %s:%d - falso sucesso ACEITO temporariamente: %s."
              % (relative, line, reason))
        print("       Remover apos fechar o Gate 1.5, em candidato proprio.")

    for func, fields, relative, line, reason in warnings:
        print("AVISO  %s (%s)" % (func, ", ".join(fields)))
        print("       %s:%d - %s." % (relative, line, reason))

    for func, fields, relative, line, reason in failures:
        print("FALHA  %s (%s)" % (func, ", ".join(fields)))
        print("       %s:%d - %s." % (relative, line, reason))
        print("       Implemente a semantica, devolva erro documentado, ou "
              "nao anuncie a DDI.")

    print()
    print("Anunciados: %d | Divida aceita: %d | Avisos: %d | Falhas: %d"
          % (len(announced), len(debt), len(warnings), len(failures)))

    if failures:
        print("REPROVADO: falso sucesso anunciado ao dxgkrnl.")
        return 1

    print("APROVADO: nenhuma DDI anunciada ignora tudo e declara sucesso.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
