@echo off
set "PATH=C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V2\package;%PATH%"
set "VK_ICD_FILENAMES=C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V2\package\vulkan_radeon.json"
set "VK_LOADER_DEBUG=all"
set "RADV_DEBUG=startup,info"
echo ===================================================
echo Testando Vulkaninfo com Mesa RADV (WDDM2 + ACO)
echo ===================================================
vulkaninfo --summary
