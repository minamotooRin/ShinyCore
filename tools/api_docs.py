#!/usr/bin/env python3
"""Generate supplemental LuaLS declarations from one or more native --api builds."""
import argparse
import json
from pathlib import Path
import re
import subprocess

MARKER = '-- BEGIN GENERATED COMPLETE API'


def generate(binaries, destination):
    source = destination.read_text(encoding='utf-8').split(MARKER)[0].rstrip()
    declared = set(re.findall(r'function\s+([\w.:]+)\(', source))
    contracts = {}
    for binary in binaries:
        metadata = json.loads(subprocess.check_output([str(binary.resolve()), '--api'], encoding='utf-8'))
        for contract in metadata['functions']:
            contracts[contract['name'].replace('ScNetSession:', 'session:')] = contract
    lines = [source, '', MARKER, '-- Regenerate with tools/api_docs.py using full and lightweight builds.']
    namespaces = sorted({name.rsplit('.', 1)[0] for name in contracts if name.startswith('sc.') and name.count('.') > 1})
    for namespace in namespaces:
        if not re.search(re.escape(namespace)+r'\s*=', source):
            lines.append(namespace+' = {}')
    for name, contract in sorted(contracts.items()):
        if name in declared:
            continue
        lines.append('---'+contract.get('description', contract.get('summary', '')))
        lines.append('---'+contract.get('signature', ''))
        # The source signature remains visible even for overloads and rich tables.
        lines.append('function '+name+'(...) end')
    destination.write_text('\n'.join(lines)+'\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binaries', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/'docs/api.lua')
    args = parser.parse_args()
    generate(args.binaries, args.output)
