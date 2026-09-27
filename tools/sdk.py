"""Pin a project-local Lua SDK; record intentional local forks without downloading files."""
import argparse
import hashlib
import json
from pathlib import Path
import re

MANIFEST='shiny-sdk.json'


def digest(path):
    # Text-only SDK files have the same identity across Git LF/CRLF checkouts.
    try:text=path.read_text(encoding='utf-8').replace('\r\n','\n')
    except UnicodeError as error:raise OSError(f'{path}: SDK files must be UTF-8 text') from error
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def files(project):
    return sorted([*(project/'lib/shiny').rglob('*.lua'),
                   *(path for path in [project/'lib/shiny/LICENSE.txt',project/'docs/api.lua'] if path.is_file())])


def write(project,version):
    validate_version(version)
    records={path.relative_to(project).as_posix():digest(path) for path in files(project)}
    if 'lib/shiny/LICENSE.txt' not in records:raise OSError('SDK requires lib/shiny/LICENSE.txt')
    record={**version,'hash':'sha256-text-lf','files':records}
    if (project/MANIFEST).exists():
        old=read(project/MANIFEST)
        if old.get('version')==record['version'] and old!=record:
            raise OSError('changed SDK files or requirements need a new --version')
    (project/MANIFEST).write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return record


def validate_version(value):
    if (not isinstance(value,dict) or type(value.get('format')) is not int or value['format']!=1
            or type(value.get('contract_version')) is not int or value['contract_version']<1):
        raise OSError('SDK manifest has an unsupported format/contract version')
    for field in ('version','engine_version'):
        if not isinstance(value.get(field),str) or not re.fullmatch(r'[A-Za-z0-9_.+-]{1,128}',value[field]):
            raise OSError(f'SDK {field} requires a version identifier')


def read(path):
    try:return json.loads(path.read_text(encoding='utf-8'))
    except (ValueError,UnicodeError) as error:raise OSError(f'{path.name}: invalid SDK JSON') from error


def audit(project,metadata):
    actual=files(project)
    if not (project/MANIFEST).exists():
        if any(path.is_relative_to(project/'lib/shiny') and path.suffix=='.lua' for path in actual):
            raise OSError('project-local Lua standard modules require shiny-sdk.json; see tools/sdk.py')
        return None
    record=read(project/MANIFEST);validate_version(record)
    if set(record)-{'format','version','engine_version','contract_version','hash','files'}:
        raise OSError('unknown SDK manifest fields')
    if record['engine_version']!=metadata['version'] or record['contract_version']!=metadata.get('contract_version'):
        raise OSError(f"SDK {record['version']} requires engine {record['engine_version']} / contract {record['contract_version']}")
    hashes=record.get('files')
    if record.get('hash')!='sha256-text-lf' or not isinstance(hashes,dict):raise OSError('invalid SDK file hashes')
    for name,value in hashes.items():
        if (not re.fullmatch(r'lib/shiny/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+\.lua|lib/shiny/LICENSE\.txt|docs/api\.lua',name)
                or not isinstance(value,str) or not re.fullmatch(r'[0-9a-f]{64}',value)):
            raise OSError(f'invalid SDK file/hash: {name}')
    present={}
    for path in actual:
        if not path.resolve().is_relative_to(project.resolve()):raise OSError('SDK file escapes project')
        name=path.relative_to(project).as_posix();value=digest(path)
        if hashes.get(name)!=value:
            raise OSError(f'{name}: SDK content changed or is unrecorded; assign an explicit local SDK version with tools/sdk.py')
        present[name]=value
    if 'lib/shiny/LICENSE.txt' not in present:raise OSError('SDK license is missing')
    return {**record,'files':present}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('project',type=Path)
    parser.add_argument('--version',required=True,help='New SDK version identifying these exact local files')
    parser.add_argument('--engine-version',required=True,help='Required engine --api version')
    parser.add_argument('--contract-version',type=int,default=1)
    args=parser.parse_args()
    try:
        write(args.project.resolve(),{'format':1,'version':args.version,'engine_version':args.engine_version,
                                      'contract_version':args.contract_version})
    except (OSError,UnicodeError) as error:parser.exit(1,f'error: {error}\n')
    print('Recorded '+str(args.project/MANIFEST))


if __name__=='__main__':main()
