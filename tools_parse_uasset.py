import struct, sys

def read_fstring(f):
    """UE FString: int32 len; 0=empty; >0 ansi (len bytes incl null); <0 utf16 (-len code units incl null)."""
    raw = f.read(4)
    if len(raw) < 4:
        return ''
    n = struct.unpack('<i', raw)[0]
    if n == 0:
        return ''
    if n < 0:
        n = -n
        data = f.read(n * 2)
        s = data.decode('utf-16-le', errors='replace')
        return s.rstrip('\x00')
    data = f.read(n)
    s = data.decode('utf-8', errors='replace')
    return s.rstrip('\x00')

def parse_summary(f):
    f.seek(0)
    tag = struct.unpack('<i', f.read(4))[0]
    legacy = struct.unpack('<i', f.read(4))[0]
    ue3 = struct.unpack('<i', f.read(4))[0]
    ver_ue4 = struct.unpack('<i', f.read(4))[0]
    ver_ue5 = struct.unpack('<i', f.read(4))[0]
    licensee = struct.unpack('<i', f.read(4))[0]
    n_custom = struct.unpack('<i', f.read(4))[0]
    for _ in range(n_custom):
        f.read(16 + 4)
    total_header_size = struct.unpack('<i', f.read(4))[0]
    package_name = read_fstring(f)
    package_flags = struct.unpack('<I', f.read(4))[0]
    name_count = struct.unpack('<i', f.read(4))[0]
    name_offset = struct.unpack('<i', f.read(4))[0]
    softobj_count = struct.unpack('<i', f.read(4))[0]
    softobj_offset = struct.unpack('<i', f.read(4))[0]
    local_id = read_fstring(f)
    gatherable_count = struct.unpack('<i', f.read(4))[0]
    gatherable_offset = struct.unpack('<i', f.read(4))[0]
    export_count = struct.unpack('<i', f.read(4))[0]
    export_offset = struct.unpack('<i', f.read(4))[0]
    import_count = struct.unpack('<i', f.read(4))[0]
    import_offset = struct.unpack('<i', f.read(4))[0]
    # version 1013: no cells, no metadata offset
    return dict(ver_ue4=ver_ue4, ver_ue5=ver_ue5,
                name_count=name_count, name_offset=name_offset,
                export_count=export_count, export_offset=export_offset,
                import_count=import_count, import_offset=import_offset,
                total_header_size=total_header_size, package_name=package_name)

def parse_names(f, s):
    f.seek(s['name_offset'])
    names = []
    for _ in range(s['name_count']):
        raw = f.read(4)
        if len(raw) < 4:
            names.append('<EOF>')
            continue
        n = struct.unpack('<i', raw)[0]
        if n < 0:
            n = -n
            data = f.read(n * 2)
            names.append(data.decode('utf-16-le', errors='replace'))
        else:
            data = f.read(n)
            names.append(data.decode('utf-8', errors='replace'))
        f.read(4)  # skip 2x uint16 hashes
    return names

def parse_imports(f, s, names):
    f.seek(s['import_offset'])
    imports = []
    for i in range(s['import_count']):
        cls_pkg = struct.unpack('<I', f.read(4))[0]
        cls_pkg_num = struct.unpack('<i', f.read(4))[0]
        cls_name = struct.unpack('<I', f.read(4))[0]
        cls_name_num = struct.unpack('<i', f.read(4))[0]
        outer_idx = struct.unpack('<i', f.read(4))[0]
        obj_name = struct.unpack('<I', f.read(4))[0]
        obj_num = struct.unpack('<i', f.read(4))[0]
        pkg_name = struct.unpack('<I', f.read(4))[0]
        pkg_num = struct.unpack('<i', f.read(4))[0]
        b_optional = struct.unpack('<B', f.read(1))[0]
        def nm(idx, num):
            if 0 <= idx < len(names):
                base = names[idx]
                return base if num == 0 else f"{base}_{num-1}"
            return f"<#{idx}_{num}>"
        imports.append(dict(cls_pkg=nm(cls_pkg, cls_pkg_num),
                             cls_name=nm(cls_name, cls_name_num),
                             outer_idx=outer_idx,
                             obj_name=nm(obj_name, obj_num),
                             pkg_name=nm(pkg_name, pkg_num),
                             optional=b_optional))
    return imports

def main(path):
    with open(path, 'rb') as f:
        s = parse_summary(f)
        names = parse_names(f, s)
        imports = parse_imports(f, s, names)
    print(f"=== {path} ===")
    print(f"  ver_ue4={s['ver_ue4']} ver_ue5={s['ver_ue5']} names={s['name_count']} imports={s['import_count']} exports={s['export_count']}")
    print(f"  package_name={s['package_name']!r}")
    for i, imp in enumerate(imports):
        target = ''
        obj = imp['obj_name']
        if 'MMDModels' in obj or 'YYB' in obj or 'body_png' in obj or 'Body' in obj or 'miku' in obj or '猫' in obj:
            target = '  <<< TARGET'
        print(f"  [{i}] {imp['cls_pkg']}.{imp['cls_name']} obj={obj} outer={imp['outer_idx']} pkg={imp['pkg_name']}{target}")

if __name__ == '__main__':
    for p in sys.argv[1:]:
        main(p)
        print()
