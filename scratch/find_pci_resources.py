import winreg

def inspect_device():
    base_path = r"SYSTEM\CurrentControlSet\Enum\PCI"
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, base_path) as pci_key:
        i = 0
        while True:
            try:
                sub = winreg.EnumKey(pci_key, i)
                i += 1
                if "VEN_1002&DEV_13FE" in sub:
                    print(f"Encontrado dispositivo: {sub}")
                    dev_path = f"{base_path}\\{sub}"
                    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, dev_path) as dev_key:
                        j = 0
                        while True:
                            try:
                                inst = winreg.EnumKey(dev_key, j)
                                j += 1
                                inst_path = f"{dev_path}\\{inst}"
                                print(f"  Instancia: {inst_path}")
                                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, inst_path) as inst_key:
                                    k = 0
                                    while True:
                                        try:
                                            val_name, val_data, val_type = winreg.EnumValue(inst_key, k)
                                            k += 1
                                            if "Location" in val_name or "Address" in val_name or "Config" in val_name or "Alloc" in val_name:
                                                print(f"    {val_name}: {val_data}")
                                        except OSError:
                                            break
                                    # Check subkeys like Control, Device Parameters, LogConf
                                    for subk in ["Control", "Device Parameters", "LogConf"]:
                                        try:
                                            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, f"{inst_path}\\{subk}") as sk:
                                                print(f"    Subchave [{subk}]:")
                                                m = 0
                                                while True:
                                                    try:
                                                        vn, vd, vt = winreg.EnumValue(sk, m)
                                                        m += 1
                                                        print(f"      {vn}: {vd}")
                                                    except OSError:
                                                        break
                                        except OSError:
                                            pass
                            except OSError:
                                break
            except OSError:
                break

if __name__ == "__main__":
    inspect_device()
