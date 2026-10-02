#include <windows.h>
#include <d3d10umddi.h>
#include <dxgiddi.h>
#include <stdio.h>

int main() {
    printf(" DXGI 1.0: %zu\n\, sizeof(DXGI_DDI_BASE_FUNCTIONS));
 printf(\DXGI 1.1: %zu\n\, sizeof(DXGI1_1_DDI_BASE_FUNCTIONS));
 printf(\DXGI 1.2: %zu\n\, sizeof(DXGI1_2_DDI_BASE_FUNCTIONS));
 printf(\DXGI 1.3: %zu\n\, sizeof(DXGI1_3_DDI_BASE_FUNCTIONS));
 printf(\DXGI 1.4: %zu\n\, sizeof(DXGI1_4_DDI_BASE_FUNCTIONS));
 printf(\DXGI 1.5: %zu\n\, sizeof(DXGI1_5_DDI_BASE_FUNCTIONS));
 printf(\DXGI 1.6: %zu\n\, sizeof(DXGI1_6_1_DDI_BASE_FUNCTIONS));
 return 0;
}
