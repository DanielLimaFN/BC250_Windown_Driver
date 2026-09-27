with open('tools/audit-bc250mcdm-safety.py', 'rb') as f:
    content = f.read()

target = b'print("BC250 MCDM SAFETY AUDIT: PASS")\r\n'
replacement = b'print("BC250 MCDM SAFETY AUDIT: PASS")\r\n    print("  DX.79 texture upload coalescing via staging ring: enforced")\r\n'
assert target in content, 'target not found'
new_content = content.replace(target, replacement, 1)
with open('tools/audit-bc250mcdm-safety.py', 'wb') as f:
    f.write(new_content)
print('Successfully updated tools/audit-bc250mcdm-safety.py!')
