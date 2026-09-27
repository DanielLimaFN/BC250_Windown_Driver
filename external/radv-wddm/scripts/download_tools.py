import urllib.request
import json
import zipfile
import os

tools_dir = r"C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\tools"
os.makedirs(tools_dir, exist_ok=True)
zip_path = os.path.join(tools_dir, "glslang.zip")

req = urllib.request.Request(
    "https://api.github.com/repos/KhronosGroup/glslang/releases/latest",
    headers={"User-Agent": "Mozilla/5.0"}
)

print("Consultando API do GitHub para glslang...")
with urllib.request.urlopen(req) as resp:
    data = json.loads(resp.read().decode('utf-8'))

download_url = None
for asset in data.get("assets", []):
    name = asset.get("name", "")
    if "windows" in name.lower() and "x64" in name.lower() and name.endswith(".zip"):
        download_url = asset.get("browser_download_url")
        print(f"Asset selecionado: {name} -> {download_url}")
        break

if not download_url:
    # Fallback to any windows zip
    for asset in data.get("assets", []):
        name = asset.get("name", "")
        if "windows" in name.lower() and name.endswith(".zip"):
            download_url = asset.get("browser_download_url")
            print(f"Asset fallback: {name} -> {download_url}")
            break

if download_url:
    print("Baixando:", download_url)
    req2 = urllib.request.Request(download_url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req2) as response, open(zip_path, 'wb') as out_file:
        out_file.write(response.read())

    print("Extraindo ZIP...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(tools_dir)
    print("Sucesso!")
    for root, dirs, files in os.walk(tools_dir):
        for f in files:
            if "glslangvalidator" in f.lower():
                print("glslangValidator localizado em:", os.path.join(root, f))
