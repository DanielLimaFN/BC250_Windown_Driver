import re, os
from datetime import datetime

log_path = r"docs\evidence\2026-09-25_11-54-52-DESKTOP-HQAG6T4-lab-binding\bc250_umd_log_1212-FINAL.txt"

# Let's inspect 10 consecutive presents between frame 100 and 110
presents_lines = []
with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
    lines = f.readlines()

p_indices = []
for idx, line in enumerate(lines):
    if "DXGI Present: Bc250UmdBridgeLinearToWddm" in line:
        p_indices.append(idx)

print(f"Total Present events: {len(p_indices)}")

# Print lines between Present 100 and Present 102
start = p_indices[100]
end = p_indices[102]
for i in range(start, end + 1):
    print(lines[i].strip())
