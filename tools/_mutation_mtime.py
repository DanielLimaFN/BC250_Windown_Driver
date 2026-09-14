"""Preserva o mtime dos fontes que as suites de mutacao reescrevem.

POR QUE ISTO EXISTE (14/09, medido):

As suites de mutacao gravam o fonte mutado, rodam o portao e restauram o
conteudo original. O CONTEUDO volta identico, mas o MTIME nao -- e
tools/package-bc250mcdm-lab.ps1 tem guarda de obsolescencia POR DATA:

    SYS de laboratorio esta obsoleto em relacao as fontes.
    Rode tools\\build-bc250mcdm-lab-bringup.ps1 -Execute ... e empacote de novo.

Resultado medido: rodar a suite de mutacao DEPOIS de construir a imagem de
laboratorio invalidava o pacote sem que uma linha de codigo tivesse mudado, e
exigia um rebuild que so' o dono do projeto pode autorizar (flag destrutiva).

A guarda por data esta CERTA e nao deve ser afrouxada -- ela e' que impede
empacotar um SYS velho. Quem estava errado era a suite, que mexia no relogio
do arquivo sem mexer no arquivo.

USO:

    from _mutation_mtime import preserve_mtimes
    with preserve_mtimes(SRC_FILE, TXN_FILE, ...):
        ...  # muta, audita, restaura conteudo
"""

import os
from contextlib import contextmanager


@contextmanager
def preserve_mtimes(*paths):
    """Restaura atime/mtime dos arquivos ao sair, com resolucao de nanossegundo."""
    stamps = {}
    for path in paths:
        try:
            st = os.stat(path)
            stamps[path] = (st.st_atime_ns, st.st_mtime_ns)
        except OSError:
            # Arquivo ausente na entrada: nao ha carimbo a preservar.
            pass
    try:
        yield
    finally:
        for path, (atime_ns, mtime_ns) in stamps.items():
            try:
                os.utime(path, ns=(atime_ns, mtime_ns))
            except OSError:
                pass
