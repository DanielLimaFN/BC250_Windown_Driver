import winreg

def read_key(path):
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path)
        print(f"=== {path} ===")
        i = 0
        while True:
            try:
                name, val, typ = winreg.EnumValue(key, i)
                if typ == winreg.REG_DWORD or typ == winreg.REG_QWORD:
                    print(f"  {name}: 0x{val:X} ({val})")
                else:
                    print(f"  {name}: {val}")
                i += 1
            except OSError:
                break
        winreg.CloseKey(key)
    except Exception as e:
        print(f"Erro ao abrir {path}: {e}")

read_key(r"SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe")
read_key(r"SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Driver")
