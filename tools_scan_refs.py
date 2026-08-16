import os, sys

NEEDLE_ASCII = b'/Game/MMDModels'
NEEDLE_UTF16 = '/Game/MMDModels'.encode('utf-16-le')

def find_refs(path):
    with open(path, 'rb') as f:
        data = f.read()
    refs = set()
    # UTF-16LE 路径(中文贴图/模型名)
    idx = 0
    while True:
        i = data.find(NEEDLE_UTF16, idx)
        if i < 0:
            break
        j = i
        while j < len(data) - 1:
            cp = data[j] | (data[j+1] << 8)
            if cp == 0 or cp < 0x20:
                break
            j += 2
        refs.add(data[i:j].decode('utf-16-le', 'ignore'))
        idx = i + 2
    # ASCII 路径
    idx = 0
    while True:
        i = data.find(NEEDLE_ASCII, idx)
        if i < 0:
            break
        j = i
        while j < len(data) and 0x20 <= data[j] < 0x7f:
            j += 1
        refs.add(data[i:j].decode('ascii', 'ignore'))
        idx = i + 1
    return refs

for root in sys.argv[1:]:
    print(f"===== {root} =====")
    n = 0
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if not fn.endswith('.uasset'):
                continue
            p = os.path.join(dirpath, fn)
            refs = find_refs(p)
            if refs:
                n += 1
                print(f"  {os.path.relpath(p)}")
                for r in sorted(refs):
                    print(f"      -> {r}")
    print(f"  (total {n} assets with MMDModels refs)")
