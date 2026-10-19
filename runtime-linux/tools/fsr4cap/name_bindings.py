#!/usr/bin/env python3
# bbport: names the resources of FSR 4.1.1 passes whose DXIL lost its reflection names (DLLs
# with rewritten shaders, e.g. performance overrides): fsr411.cpp binds resources by name.
#
#   name_bindings.py <dxil-spirv> <AMD's original amd_fidelityfx_upscaler_dx12.dll> <asset dir>
#   name_bindings.py --check <asset dir>   exit 0 when every resource has a name, 3 otherwise
# (spirv-dis, spirv-as from SPIRV-Tools on PATH.)
#
# The original DLL's DXIL containers are read (not run): each pass's binding -> name table comes
# from them, translated with the flags extract.py uses. Every .spv in <asset dir>/*/ whose bound
# variables have no name gets the original name of its binding; the code is not changed.
import glob, os, re, struct, subprocess, sys, tempfile

if sys.argv[1] == '--check':
    for spv in glob.glob(os.path.join(sys.argv[2], '*', '*.spv')):
        if os.path.basename(spv) == 'postpass_orig.spv':
            continue  # kept for reference; the runtime loads postpass.spv
        asm = subprocess.run(['spirv-dis', spv], check=True, capture_output=True, text=True).stdout
        names = dict(re.findall(r'OpName (%\w+) "([^"]*)"', asm))
        if any(not names.get(v) for v in re.findall(r'OpDecorate (%\w+) Binding \d+', asm)):
            print(f'unnamed resources in {spv}')
            sys.exit(3)
    print('every resource has a name')
    sys.exit(0)

dxil_spirv, original_dll, assets = sys.argv[1:4]
FLAGS =['--enable-shader-i8-dot', '--ssbo-uav', '--ssbo-srv', '--class-bindings', '--use-reflection-names']
PREFIX = 'fsr4_model_v07_fp8_no_scale_'


def pass_name(blob):
    for s in re.findall(rb'[A-Za-z_][A-Za-z0-9_]{5,80}', blob):
        s = s.decode()
        if s.startswith(PREFIX):
            return s[len(PREFIX):]
        if s.startswith('fsr_rcas'):
            return 'rcas'
    return None


def binding_names(asm):
    """{binding: name} of the bound variables of a disassembled module ('' when unnamed)."""
    names = dict(re.findall(r'OpName (%\w+) "([^"]*)"', asm))
    out = {}
    for var, binding in re.findall(r'OpDecorate (%\w+) Binding (\d+)', asm):
        out.setdefault(int(binding), set()).add(names.get(var, ''))
    return out


def disassemble(spv):
    return subprocess.run(['spirv-dis', spv], check=True, capture_output=True, text=True).stdout


# The original pass shaders: DXBC containers in the DLL ('DXBC', hash[16], version, size).
data = open(original_dll, 'rb').read()
tables = {}
with tempfile.TemporaryDirectory() as tmp:
    for m in re.finditer(rb'DXBC', data):
        start = m.start()
        size = struct.unpack_from('<I', data, start + 24)[0]
        if not 32 < size < 4 << 20 or start + size > len(data):
            continue
        blob = data[start:start + size]
        name = pass_name(blob)
        if name is None:
            continue
        dxil, spv = os.path.join(tmp, 'p.dxil'), os.path.join(tmp, 'p.spv')
        open(dxil, 'wb').write(blob)
        if subprocess.run([dxil_spirv, dxil, *FLAGS, '--output', spv],
                          stderr=subprocess.DEVNULL).returncode:
            continue
        for binding, found in binding_names(disassemble(spv)).items():
            for n in found - {''}:
                tables.setdefault(name, {}).setdefault(binding, set()).add(re.sub(r'_1$', '', n))

conflicts = 0
for name, table in tables.items():
    for binding, found in table.items():
        if len(found) > 1:
            conflicts += 1
            print(f'{name} binding {binding}: names differ between variants: {sorted(found)}')
if conflicts:
    sys.exit(1)

named = 0
for spv in sorted(glob.glob(os.path.join(assets, '*', '*.spv'))):
    name = os.path.splitext(os.path.basename(spv))[0]
    if name == 'postpass_orig':
        continue
    asm = disassemble(spv)
    names = dict(re.findall(r'OpName (%\w+) "([^"]*)"', asm))
    missing = [(var, int(b)) for var, b in re.findall(r'OpDecorate (%\w+) Binding (\d+)', asm)
               if not names.get(var)]
    if not missing:
        continue
    table = tables.get(name)
    if table is None:
        print(f'{spv}: unnamed resources and no original {name} pass')
        sys.exit(1)
    for var, binding in missing:
        if binding not in table:
            print(f'{spv}: binding {binding} has no original name')
            sys.exit(1)
        new = next(iter(table[binding]))
        line = f'OpName {var} "{new}"'
        if re.search(rf'OpName {re.escape(var)} "[^"]*"', asm):
            asm = re.sub(rf'OpName {re.escape(var)} "[^"]*"', line, asm, count=1)
        else:
            asm = asm.replace(f'OpName %main "main"', f'OpName %main "main"\n{line}', 1)
    subprocess.run(['spirv-as', '--preserve-numeric-ids', '--target-env', 'spv1.3', '-', '-o', spv],
                   input=asm, check=True, text=True)
    named += 1
print(f'names restored in {named} shaders (from {sum(len(t) for t in tables.values())} original '
      f'bindings in {len(tables)} passes)')
