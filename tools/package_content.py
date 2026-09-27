"""Explicit runtime roots plus Lua and built stream-map dependency closure."""
from pathlib import Path, PurePosixPath
import json
import re


def strings(value, context):
    if not isinstance(value,list) or any(not isinstance(item,str) or not item for item in value):
        raise OSError(f'{context}: expected an array of nonempty strings')
    return value


def lua_tokens(source, context):
    """Only lex; never execute scripts or mistake comments/strings for require calls."""
    i=0;line=1
    while i<len(source):
        start=i;at=line;comment=source.startswith('--',i)
        if comment:i+=2
        long=re.match(r'\[(=*)\[',source[i:])
        if long:
            begin=i+len(long[0]);end=source.find(']'+long[1]+']',begin)
            if end<0:raise OSError(f'{context}:{at}: unterminated Lua long string/comment')
            value=source[begin:end].removeprefix('\n');i=end+len(long[1])+2
            if not comment:yield ('string',value,at)
        elif comment:
            end=source.find('\n',i);i=len(source) if end<0 else end+1
        elif source[i] in "\"'":
            quote=source[i];i+=1;begin=i;escaped=False
            while i<len(source) and source[i]!=quote:
                if source[i]=='\\':escaped=True;i+=1
                i+=1
            if i>=len(source):raise OSError(f'{context}:{at}: unterminated Lua string')
            yield ('string',None if escaped else source[begin:i],at);i+=1
        elif source[i].isspace():i+=1
        else:
            identifier=re.match(r'[A-Za-z_][A-Za-z_0-9]*',source[i:])
            if identifier:yield ('id',identifier[0],at);i+=len(identifier[0])
            else:yield ('symbol',source[i],at);i+=1
        line+=source[start:i].count('\n')


def requires(source, context, dynamic):
    tokens=list(lua_tokens(source,context));modules=set(dynamic)
    for i,(kind,value,line) in enumerate(tokens):
        if kind!='id' or value!='require':continue
        after=tokens[i+1:i+4];name=None
        if after and after[0][0]=='string':name=after[0][1]
        elif len(after)>=3 and after[0][1]=='(' and after[1][0]=='string' and after[2][1]==')':name=after[1][1]
        if name is not None:modules.add(name)
        elif not dynamic:
            raise OSError(f'{context}:{line}: dynamic/aliased require needs dynamic_modules[{context!r}]')
    return sorted(modules)


def closure(project: Path):
    """None retains conservative copy; a manifest makes omissions intentional and reported."""
    root=project.resolve();manifest=root/'package.json'
    if not manifest.exists():return None
    try:spec=json.loads(manifest.read_text(encoding='utf-8'))
    except (ValueError,UnicodeError) as error:raise OSError(f'package.json: {error}') from error
    if not isinstance(spec,dict) or set(spec)-{'format','scripts','files','stream_maps','dynamic_modules'}:
        raise OSError('package.json: unknown fields or invalid object')
    if type(spec.get('format')) is not int or spec['format']!=1:raise OSError('package.json: unsupported format')
    dynamic=spec.get('dynamic_modules',{})
    if not isinstance(dynamic,dict):raise OSError('package.json.dynamic_modules: expected an object')
    for name,values in dynamic.items():strings(values,f'package.json.dynamic_modules.{name}')
    selected={};pending=[];scanned=set()
    def add(relative,reason,script=False):
        if (not isinstance(relative,str) or not relative or '\\' in relative or ':' in relative
                or any(ord(char)<32 for char in relative)):
            raise OSError(f'{reason}: invalid project path {relative!r}')
        path=PurePosixPath(relative)
        if path.is_absolute() or any(part in ('','.','..') for part in relative.split('/')):
            raise OSError(f'{reason}: invalid project path {relative!r}')
        full=root/relative
        if not full.resolve().is_relative_to(root) or not full.is_file():
            raise OSError(f'{reason}: missing or outside-project file {relative}')
        selected.setdefault(relative,set()).add(reason)
        if script:pending.append(relative)
        return full
    add('package.json','manifest');add('project.lua','project',True)
    if (root/'shiny-sdk.json').is_file():
        add('shiny-sdk.json','SDK version');add('lib/shiny/LICENSE.txt','SDK license')
    for path in strings(spec.get('scripts',[]),'package.json.scripts'):
        if not path.endswith('.lua'):raise OSError(f'package.json.scripts: expected Lua file: {path}')
        add(path,'script root',True)
    for path in strings(spec.get('files',[]),'package.json.files'):add(path,'file root')
    for path in strings(spec.get('stream_maps',[]),'package.json.stream_maps'):
        index=add(path,'stream root')
        def file_properties(properties,owner):
            for prop in properties:
                if prop.get('type')=='file' and prop.get('value')!='':
                    add(prop.get('value'),f"{owner} property {prop.get('name','?')}")
        try:
            data=json.loads(index.read_text(encoding='utf-8'))
            if data.get('format')!=3 or data.get('chunk_size')!=32:raise ValueError('expected built stream format 3 / 32 tiles')
            file_properties(data.get('properties',[]),f"{path}: map")
            for group in data.get('groups',[]):
                file_properties(group.get('properties',[]),f"{path}: group {group.get('name','?')}")
            for chunk in data['chunks']:
                name=chunk['path']
                if not isinstance(name,str) or '/' in name or '\\' in name:raise ValueError('chunk path must be a filename')
                chunk_file=add((PurePosixPath(path).parent/name).as_posix(),path)
                if type(chunk.get('bytes')) is not int or chunk['bytes']!=chunk_file.stat().st_size:
                    raise ValueError(f'{name}: byte size does not match index')
                content=json.loads(chunk_file.read_text(encoding='utf-8'))
                for obj in content.get('objects',[]):
                    file_properties(obj.get('properties',[]),f"{chunk_file.name}: object {obj.get('id','?')}")
            for source in [*data['tilesets'],*data['layers']]:
                if 'image' in source:add(source['image'],path)
                file_properties(source.get('properties',[]),f"{path}: {source.get('name','layer')}")
                for tile in source.get('tiles',[]):
                    if 'image' in tile:add(tile['image'],path)
                    file_properties(tile.get('properties',[]),f"{path}: tile {tile.get('id','?')}")
        except (ValueError,KeyError,TypeError,AttributeError) as error:raise OSError(f'{path}: invalid stream index: {error}') from error
    while pending:
        path=pending.pop()
        if path in scanned:continue
        scanned.add(path)
        try:source=(root/path).read_text(encoding='utf-8')
        except UnicodeError as error:raise OSError(f'{path}: expected UTF-8 Lua source') from error
        for name in requires(source,path,dynamic.get(path,[])):
            if len(name)>127 or not re.fullmatch(r'[A-Za-z_0-9]+(?:\.[A-Za-z_0-9]+)*',name):
                raise OSError(f'{path}: invalid module name {name!r}')
            module=('lib/' if name.startswith('shiny.') else '')+name.replace('.','/')+'.lua'
            add(module,path,True)
    unused=set(dynamic)-scanned
    if unused:raise OSError('package.json: dynamic module owners are not shipped scripts: '+', '.join(sorted(unused)))
    return {'mode':'explicit closure','files':[{'path':path,'reasons':sorted(reasons)} for path,reasons in sorted(selected.items())],
            'omitted_files':[path.relative_to(root).as_posix() for path in sorted(root.rglob('*'))
                             if path.is_file() and path.relative_to(root).as_posix() not in selected]}
