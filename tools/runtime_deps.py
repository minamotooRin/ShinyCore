"""Inspect native imports without loading them; bundle only explicitly supplied runtimes."""
from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import posixpath
import re
import shutil
import struct
import subprocess
import tempfile


WINDOWS_SYSTEM=set(('advapi32 avrt bcrypt cfgmgr32 comctl32 comdlg32 crypt32 d3d9 d3d11 d3d12 '
    'dinput8 dwmapi dxgi gdi32 glu32 hid imm32 iphlpapi kernel32 mpr msacm32 msvcrt ncrypt '
    'netapi32 ntdll ole32 oleaut32 opengl32 powrprof propsys psapi rpcrt4 secur32 setupapi '
    'shell32 shlwapi ucrtbase user32 userenv usp10 uxtheme version winhttp wininet winmm '
    'winspool ws2_32 wtsapi32').split())
LINUX_SYSTEM=re.compile(r'(?:lib(?:c|m|dl|pthread|rt|resolv|util|anl|stdc\+\+|gcc_s|asan|ubsan)\.so\.[0-9]+|'
    r'lib(?:GL|EGL|GLX|OpenGL|X11|X11-xcb|Xrandr|Xinerama|Xcursor|Xi|Xext|Xrender|Xfixes|'
    r'Xxf86vm|xcb|xkbcommon|wayland-client|wayland-cursor|wayland-egl|asound)\.so\.[0-9]+|'
    r'ld-linux[^/]*\.so\.[0-9]+|ld-musl-[^/]+\.so\.1)\Z')


@dataclass(frozen=True)
class Native:
    kind: str
    architectures: tuple[str,...]
    imports: tuple[str,...]
    delay_imports: tuple[str,...]=()
    rpaths: tuple[str,...]=()
    interpreter: str=""
    arch_rpaths: tuple[tuple[str,tuple[str,...]],...]=()


def command(*args: str) -> str:
    tool=shutil.which(args[0])
    if not tool: raise OSError(f'native dependency audit requires {args[0]}')
    try:
        result=subprocess.run([tool,*args[1:]],capture_output=True,text=True,encoding='utf-8',errors='replace',
                              timeout=30,env={**os.environ,'LC_ALL':'C'})
    except subprocess.TimeoutExpired as error:
        raise OSError(f'{args[0]} exceeded the 30 second audit limit') from error
    if result.returncode: raise OSError(f'{args[0]} failed: {result.stderr.strip()}')
    return result.stdout


def pe(data: bytes) -> Native:
    def unpack(fmt,offset):
        if offset<0 or offset+struct.calcsize(fmt)>len(data): raise OSError('truncated PE headers/imports')
        return struct.unpack_from(fmt,data,offset)
    header=unpack('<I',0x3c)[0]
    if data[header:header+4]!=b'PE\0\0': raise OSError('invalid PE signature')
    machine,count=unpack('<HH',header+4)
    size=unpack('<H',header+20)[0]; optional=header+24
    magic=unpack('<H',optional)[0]
    if magic not in (0x10b,0x20b) or not 1<=count<=96: raise OSError('unsupported PE header')
    directory=optional+(112 if magic==0x20b else 96)
    directories=unpack('<I',directory-4)[0]
    if directory+min(directories,16)*8>optional+size: raise OSError('truncated PE data directory')
    image_base=unpack('<Q' if magic==0x20b else '<I',optional+(24 if magic==0x20b else 28))[0]
    headers=unpack('<I',optional+60)[0]
    sections=[]
    for i in range(count):
        virtual_size,rva,raw_size,raw=unpack('<IIII',optional+size+i*40+8)
        sections.append((rva,raw_size,raw))
    def offset(rva,length):
        if 0<=rva<headers and rva+length<=min(headers,len(data)): return rva
        for start,amount,raw in sections:
            if start<=rva and rva+length<=start+amount and raw+rva-start+length<=len(data): return raw+rva-start
        raise OSError('PE import RVA outside file-backed data')
    def name(rva):
        chars=[]
        for i in range(260):
            value=data[offset(rva+i,1)]
            if value==0: break
            if value>127: raise OSError('non-ASCII PE dependency name')
            chars.append(chr(value))
        else: raise OSError('unterminated PE dependency name')
        value=''.join(chars)
        if not re.fullmatch(r'[A-Za-z0-9_.+-]+\.dll',value,re.I): raise OSError(f'invalid PE dependency name: {value!r}')
        return value
    def imports(index,width):
        if directories<=index:return ()
        rva,amount=unpack('<II',directory+index*8)
        if not rva and not amount:return ()
        if not rva or amount<width or amount>1024*1024:raise OSError('invalid PE import directory')
        result=[]
        for start in range(0,min(amount,4096*width)-width+1,width):
            fields=unpack('<'+'I'*(width//4),offset(rva+start,width))
            if not any(fields): return tuple(sorted(set(result),key=str.casefold))
            if index==1: name_rva=fields[3]
            else:
                if fields[0]&~1: raise OSError('unsupported PE delay import attributes')
                name_rva=fields[1] if fields[0]&1 else fields[1]-image_base
            result.append(name(name_rva))
        raise OSError('unterminated PE import directory')
    return Native('PE',(f'{machine:04x}:{magic:04x}',),imports(1,20),imports(13,32))


def inspect(path: Path) -> Native:
    with path.open('rb') as source: header=source.read(64)
    if header[:2]==b'MZ': return pe(path.read_bytes())
    if header[:4]==b'\x7fELF':
        if len(header)<20 or header[4] not in (1,2) or header[5] not in (1,2): raise OSError('invalid ELF header')
        machine=struct.unpack_from('<H' if header[5]==1 else '>H',header,18)[0]
        dynamic=command('readelf','--dynamic','--wide',str(path))
        fields=re.findall(r'\((NEEDED|RPATH|RUNPATH)\).*?\[(.*?)\]',dynamic)
        imports=tuple(value for key,value in fields if key=='NEEDED')
        paths=tuple(part for key,value in fields if key!='NEEDED' for part in value.split(':'))
        program=command('readelf','--program-headers','--wide',str(path))
        interpreter=re.search(r'Requesting program interpreter:\s*([^\]]+)\]',program)
        return Native('ELF',(f'{header[4]}:{header[5]}:{machine}',),imports,rpaths=paths,
                      interpreter=interpreter.group(1) if interpreter else '')
    if header[:4] in (b'\xcf\xfa\xed\xfe',b'\xce\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xfe\xed\xfa\xce',
                      b'\xca\xfe\xba\xbe',b'\xca\xfe\xba\xbf',b'\xbe\xba\xfe\xca',b'\xbf\xba\xfe\xca'):
        identifiers={line.strip() for line in command('otool','-arch','all','-D',str(path)).splitlines() if not line.endswith(':')}
        imports=[]
        for line in command('otool','-arch','all','-L',str(path)).splitlines():
            if ' (compatibility version ' in line:
                name=line.strip().split(' (compatibility version ',1)[0]
                if name not in identifiers:imports.append(name)
        architectures=tuple(sorted(command('lipo','-archs',str(path)).split()))
        if not architectures:raise OSError('Mach-O architecture inspection failed')
        arch_paths=[]
        for architecture in architectures:
            paths=[]
            output=command('otool','-arch',architecture,'-l',str(path))
            for block in re.split(r'^Load command \d+\s*$',output,flags=re.M):
                if not re.search(r'^\s*cmd LC_RPATH\s*$',block,re.M):continue
                match=re.search(r'^\s*path (.+) \(offset \d+\)\s*$',block,re.M)
                if not match:raise OSError('malformed Mach-O LC_RPATH output')
                paths.append(match[1])
            arch_paths.append((architecture,tuple(paths)))
        paths=tuple(dict.fromkeys(path for _,paths in arch_paths for path in paths))
        return Native('Mach-O',architectures,tuple(sorted(set(imports))),rpaths=paths,arch_rpaths=tuple(arch_paths))
    raise OSError(f'unsupported native executable format: {path.name}')


def system_library(kind: str, name: str) -> bool:
    if kind=='PE':
        lower=name.lower()
        return lower.startswith(('api-ms-win-','ext-ms-win-')) or lower.removesuffix('.dll') in WINDOWS_SYSTEM
    if kind=='ELF':return bool(LINUX_SYSTEM.fullmatch(name))
    return posixpath.normpath(name).startswith(('/usr/lib/','/System/Library/'))


def audit(binary: Path, runtimes: tuple[tuple[Path,Path],...]=()) -> dict:
    """All supplied runtimes are explicit roots, including libraries loaded by game code."""
    native=inspect(binary); providers={}
    key=lambda name:name.casefold() if native.kind=='PE' else name
    for library,notice in runtimes:
        if not library.is_file() or not notice.is_file():raise OSError(f'runtime and license must exist: {library}, {notice}')
        name=library.name
        if not re.fullmatch(r'[A-Za-z0-9_.+-]+',name) or key(name) in {key(binary.name),'shiny','shiny.exe','launch'}:
            raise OSError(f'invalid runtime filename: {name}')
        if native.kind=='PE' and not name.lower().endswith('.dll'):raise OSError('Windows runtime files must be DLLs')
        if key(name) in providers:raise OSError(f'duplicate runtime filename: {name}')
        if system_library(native.kind,name):raise OSError(f'OS runtime must not be redistributed: {name}')
        providers[key(name)]=library
    nodes=[]
    for path in [binary,*providers.values()]:
        info=native if path==binary else inspect(path)
        if info.kind!=native.kind or not set(native.architectures)<=set(info.architectures):
            raise OSError(f'runtime architecture mismatch: {path.name}')
        edges=[]
        for name in sorted(set(info.imports+info.delay_imports)):
            if system_library(info.kind,name):category='system';target=name
            else:
                target=Path(name).name
                provider=providers.get(key(target))
                if not provider:raise OSError(f'{path.name}: missing runtime {name}; supply --runtime LIBRARY LICENSE')
                category='bundled';target=provider.name
            edges.append({'name':name,'kind':category,'target':target,'delayed':name in info.delay_imports})
        if info.interpreter and not system_library('ELF',Path(info.interpreter).name):
            raise OSError(f'unsupported ELF interpreter: {info.interpreter}')
        nodes.append({'file':path.name,'architectures':list(info.architectures),'imports':edges,
                      'rpaths':list(info.rpaths),'interpreter':info.interpreter,
                      'arch_rpaths':{arch:list(paths) for arch,paths in info.arch_rpaths}})
    return {'format':native.kind,'nodes':nodes,'runtime_files':[path.name for path in providers.values()],
            'scope':'static and delayed imports plus explicitly supplied dynamic roots; arbitrary dlopen/LoadLibrary names are not discovered'}


def relocate_macho(path: Path, bundled: list[dict], paths, needs_search: bool) -> bool:
    arguments=[]
    for edge in bundled:
        target='@loader_path/'+edge['target']
        if edge['name']!=target:arguments.extend(('-change',edge['name'],target))
    for search in dict.fromkeys(paths):
        if search!='@loader_path':arguments.extend(('-delete_rpath',search))
    if needs_search and '@loader_path' not in paths:arguments.extend(('-add_rpath','@loader_path'))
    if arguments:command('install_name_tool',*arguments,str(path))
    return bool(arguments)


def relocate(binary: Path, report: dict) -> None:
    """Only copied files are rewritten; source executables and libraries stay intact."""
    if report['format']=='PE':return
    for index,node in enumerate(report['nodes']):
        path=binary if index==0 else binary.parent/node['file']
        bundled=[edge for edge in node['imports'] if edge['kind']=='bundled']
        if report['format']=='ELF':
            if report.get('runtime_files') or bundled or node['rpaths']:
                for edge in bundled:
                    if edge['name']!=edge['target']:command('patchelf','--replace-needed',edge['name'],edge['target'],str(path))
                command('patchelf','--set-rpath','$ORIGIN',str(path))
        else:
            arch_paths=node.get('arch_rpaths',{})
            if len({tuple(paths) for paths in arch_paths.values()})>1:
                # install_name_tool requires a deleted RPATH to exist in every slice.
                with tempfile.TemporaryDirectory(prefix='.shiny-relocate-',dir=path.parent) as directory:
                    parts=[]
                    for index,(architecture,paths) in enumerate(arch_paths.items()):
                        part=Path(directory)/str(index);parts.append(str(part))
                        command('lipo',str(path),'-thin',architecture,'-output',str(part))
                        relocate_macho(part,bundled,paths,bool(report.get('runtime_files')))
                    merged=Path(directory)/'merged'
                    command('lipo','-create',*parts,'-output',str(merged))
                    shutil.copymode(path,merged)
                    merged.replace(path)
                changed=True
            else:
                changed=relocate_macho(path,bundled,node['rpaths'],bool(report.get('runtime_files')))
            if changed:command('codesign','--force','--sign','-',str(path))


def verify_relocated(report: dict) -> None:
    """Validate a fresh audit of copied files, never the pre-rewrite report."""
    kind=report['format']
    if kind=='PE':return
    expected='$ORIGIN' if kind=='ELF' else '@loader_path'
    for node in report['nodes']:
        paths=node['rpaths']
        if any(path!=expected for path in paths):
            raise OSError(f"{node['file']}: relocation retained a non-package search path: {paths}")
        per_arch=node.get('arch_rpaths',{})
        if kind=='Mach-O' and set(per_arch)!=set(node['architectures']):
            raise OSError(f"{node['file']}: relocation missing architecture search-path audit")
        if report['runtime_files'] and (expected not in paths or any(expected not in values for values in per_arch.values())):
            raise OSError(f"{node['file']}: relocation missing package search path {expected}")
        for edge in node['imports']:
            if edge['kind']!='bundled':continue
            target=edge['target'] if kind=='ELF' else '@loader_path/'+edge['target']
            if edge['name']!=target:
                raise OSError(f"{node['file']}: relocation retained dependency {edge['name']}; expected {target}")
