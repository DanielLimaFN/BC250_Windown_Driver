# TOOLCHAIN_VS2026_WDK28000 — evidência offline

Migração executada e validada em 2026-09-07, sem bind e sem acesso ao hardware.

```text
VS2026              18.9.1
cl.exe              19.51.36256.0
SDK/WDK             10.0.28000.2526 (layout 10.0.28000.0)
KMD full suite       PASS
UMD contracts        PASS
Lab build/link       PASS
Active package audit PASS
Lab package audit    PASS
Safety audit         PASS
Auxiliary builds     PASS
```

Hashes dos pacotes finais estão em `docs/173_TOOLCHAIN_VS2026_WDK28000.md` e nos
manifestos `driver/wdk/package/output_active.manifest.json` e
`driver/wdk/package/output_lab.manifest.json`.

Resultado no silício: **NÃO VALIDADO EM HARDWARE**.
