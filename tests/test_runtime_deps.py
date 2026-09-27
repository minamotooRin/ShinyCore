"""Bounded native import parsing and explicit redistribution contract."""
from pathlib import Path
import copy
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import runtime_deps as deps


def executable(imports=(), delayed=(), machine=0x8664):
    data=bytearray(4096)
    def put(offset,fmt,*values):struct.pack_into('<'+fmt,data,offset,*values)
    data[:2]=b'MZ';put(0x3c,'I',0x80);data[0x80:0x84]=b'PE\0\0'
    put(0x84,'HH',machine,1);put(0x94,'H',240)
    optional=0x98;put(optional,'H',0x20b);put(optional+24,'Q',0x140000000)
    put(optional+60,'I',512);put(optional+108,'I',16)
    put(optional+240+8,'IIII',3584,0x1000,3584,512)
    name_offset=2048
    for index,width,names,start in ((1,20,imports,512),(13,32,delayed,1024)):
        if not names:continue
        put(optional+112+index*8,'II',0x1000+start-512,(len(names)+1)*width)
        for i,name in enumerate(names):
            encoded=name.encode('ascii')+b'\0';data[name_offset:name_offset+len(encoded)]=encoded
            rva=0x1000+name_offset-512
            if index==1:put(start+i*width,'IIIII',0,0,0,rva,0)
            else:put(start+i*width,'IIIIIIII',1,rva,0,0,0,0,0,0)
            name_offset+=len(encoded)
    return data


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        directory=tempfile.TemporaryDirectory(prefix='shiny-imports-')
        self.addCleanup(directory.cleanup);self.root=Path(directory.name)
        self.notice=self.root/'LICENSE';self.notice.write_text('fixture notice',encoding='utf-8')

    def binary(self,name,imports=(),delayed=(),machine=0x8664):
        path=self.root/name;path.write_bytes(executable(imports,delayed,machine));return path

    def test_normal_and_delayed_imports(self):
        binary=self.binary('shiny.exe',['KERNEL32.dll','Audio.dll'],['Codec.dll'])
        audio=self.binary('Audio.dll',['Codec.dll'])
        codec=self.binary('Codec.dll',['Audio.dll','ucrtbase.dll'])
        report=deps.audit(binary,((audio,self.notice),(codec,self.notice)))
        self.assertEqual(report['runtime_files'],['Audio.dll','Codec.dll'])
        self.assertEqual(len(report['nodes']),3)
        self.assertEqual(report['nodes'][0]['imports'][1],
                         dict(name='Codec.dll',kind='bundled',target='Codec.dll',delayed=True))

    def test_missing_transitive_import_and_license(self):
        binary=self.binary('shiny.exe',['audio.dll'])
        audio=self.binary('audio.dll',['missing.dll'])
        with self.assertRaisesRegex(OSError,'audio.dll: missing runtime missing.dll'):
            deps.audit(binary,((audio,self.notice),))
        with self.assertRaisesRegex(OSError,'license must exist'):
            deps.audit(binary,((audio,self.root/'absent'),))

    def test_architecture_and_system_runtime(self):
        binary=self.binary('shiny.exe')
        wrong=self.binary('wrong.dll',machine=0x14c)
        with self.assertRaisesRegex(OSError,'architecture mismatch'):deps.audit(binary,((wrong,self.notice),))
        system=self.binary('kernel32.dll')
        with self.assertRaisesRegex(OSError,'must not be redistributed'):deps.audit(binary,((system,self.notice),))
        self.assertFalse(deps.system_library('PE','VCRUNTIME140.dll'))

    def test_duplicate_runtime(self):
        binary=self.binary('shiny.exe');library=self.binary('codec.dll')
        with self.assertRaisesRegex(OSError,'duplicate runtime'):
            deps.audit(binary,((library,self.notice),(library,self.notice)))

    def test_malformed_pe(self):
        valid=executable(['good.dll'])
        invalid_rva=bytearray(valid);struct.pack_into('<I',invalid_rva,524,0xffffff00)
        unterminated=bytearray(valid);struct.pack_into('<I',unterminated,0x98+112+8+4,20)
        for data in (valid[:50],invalid_rva,unterminated,executable(['../escape.dll'])):
            with self.subTest(length=len(data)),self.assertRaises(OSError):deps.pe(data)

    def test_elf_inspection_and_relocation(self):
        binary=self.root/'shiny';header=bytearray(64);header[:6]=b'\x7fELF\x02\x01'
        struct.pack_into('<H',header,18,62);binary.write_bytes(header)
        with patch.object(deps,'command',side_effect=[
            '(NEEDED) Shared library: [libcodec.so.1]\n(RUNPATH) Library runpath: [/developer/lib]',
            '[Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]']):
            info=deps.inspect(binary)
        self.assertEqual(info.imports,('libcodec.so.1',));self.assertEqual(info.rpaths,('/developer/lib',))
        report={'format':'ELF','nodes':[{'file':'shiny','rpaths':info.rpaths,'imports':[
            {'kind':'bundled','name':'/developer/lib/libcodec.so.1','target':'libcodec.so.1'}]}]}
        with patch.object(deps,'command') as command:
            deps.relocate(binary,report)
            self.assertEqual(command.call_args_list[-1].args,('patchelf','--set-rpath','$ORIGIN',str(binary)))

    def test_macho_inspection_and_relocation(self):
        binary=self.root/'shiny';binary.write_bytes(b'\xcf\xfa\xed\xfe'+bytes(60))
        paths='Load command 0\n cmd LC_RPATH\n cmdsize 40\n path /developer/lib (offset 12)\n' \
              'Load command 1\n cmd LC_RPATH\n cmdsize 32\n path @loader_path (offset 12)\n'
        with patch.object(deps,'command',side_effect=['shiny:\n',
            'shiny:\n\t@rpath/libcodec.dylib (compatibility version 1.0.0, current version 1.0.0)\n',
            'arm64 x86_64',paths,paths]):info=deps.inspect(binary)
        self.assertEqual(info.architectures,('arm64','x86_64'))
        self.assertEqual(info.rpaths,('/developer/lib','@loader_path'))
        self.assertEqual(dict(info.arch_rpaths),{arch:info.rpaths for arch in info.architectures})
        report={'format':'Mach-O','runtime_files':['libcodec.dylib'],'nodes':[{'file':'shiny','rpaths':info.rpaths,'imports':[
            {'kind':'bundled','name':info.imports[0],'target':'libcodec.dylib'}]}]}
        with patch.object(deps,'command') as command:
            deps.relocate(binary,report)
            self.assertFalse(any('-add_rpath' in call.args for call in command.call_args_list))
            self.assertEqual(command.call_args_list[0].args,
                ('install_name_tool','-change','@rpath/libcodec.dylib','@loader_path/libcodec.dylib',
                 '-delete_rpath','/developer/lib',str(binary)))
            self.assertEqual(command.call_args_list[1].args,('codesign','--force','--sign','-',str(binary)))

    def test_relocation_verifies_fresh_paths_and_dependencies(self):
        for kind,origin,name in [('ELF','$ORIGIN','libcodec.so.1'),('Mach-O','@loader_path','libcodec.dylib')]:
            expected=name if kind=='ELF' else origin+'/'+name
            node=dict(file='shiny',architectures=['arm64','x86_64'],rpaths=[origin],
                      arch_rpaths={'arm64':[origin],'x86_64':[origin]} if kind=='Mach-O' else {},
                      imports=[dict(kind='bundled',name=expected,target=name)])
            report=dict(format=kind,runtime_files=[name],nodes=[node])
            deps.verify_relocated(report)
            with self.subTest(kind=kind):
                for field,value in [('rpaths',['/developer/lib']),('rpaths',[]),
                                    ('imports',[dict(kind='bundled',name='/developer/'+name,target=name)])]:
                    invalid=copy.deepcopy(report);invalid['nodes'][0][field]=value
                    with self.assertRaisesRegex(OSError,'relocation'):deps.verify_relocated(invalid)
                if kind=='Mach-O':
                    invalid=copy.deepcopy(report)
                    invalid['nodes'][0]['arch_rpaths']['x86_64']=[]
                    with self.assertRaisesRegex(OSError,'missing package search path'):deps.verify_relocated(invalid)
                    del invalid['nodes'][0]['arch_rpaths']['x86_64']
                    with self.assertRaisesRegex(OSError,'missing architecture'):deps.verify_relocated(invalid)

    def test_explicit_dynamic_roots_get_package_search_paths(self):
        binary=self.root/'shiny'
        for kind,name,expected in [('ELF','codec.so',('patchelf','--set-rpath','$ORIGIN')),
                                   ('Mach-O','codec.dylib',('install_name_tool','-add_rpath','@loader_path'))]:
            report=dict(format=kind,runtime_files=[name],nodes=[
                dict(file=file,rpaths=[],imports=[]) for file in ('shiny',name)])
            with patch.object(deps,'command') as command:
                deps.relocate(binary,report)
            for file in ('shiny',name):
                self.assertIn((*expected,str(self.root/file)),[call.args for call in command.call_args_list])

    def test_macho_distinct_architecture_paths(self):
        binary=self.root/'shiny';binary.write_bytes(b'original')
        report=dict(format='Mach-O',runtime_files=['codec.dylib'],nodes=[dict(
            file='shiny',imports=[],rpaths=['/developer/lib','@loader_path'],
            arch_rpaths={'arm64':['/developer/lib'],'x86_64':['@loader_path']})])
        def fake_command(*args):
            if args[0]=='lipo':Path(args[-1]).write_bytes(b'merged' if '-create' in args else b'slice')
            return ''
        with patch.object(deps,'command',side_effect=fake_command) as command:
            deps.relocate(binary,report)
        calls=[call.args for call in command.call_args_list]
        self.assertEqual(sum('-thin' in call for call in calls),2)
        edits=[call for call in calls if call[0]=='install_name_tool']
        self.assertEqual(len(edits),1)
        self.assertEqual(edits[0][1:-1],('-delete_rpath','/developer/lib','-add_rpath','@loader_path'))
        self.assertEqual(calls[-1],('codesign','--force','--sign','-',str(binary)))
        self.assertEqual(binary.read_bytes(),b'merged')
        self.assertEqual(list(self.root.glob('.shiny-relocate-*')),[])
        binary.write_bytes(b'original')
        def failed_merge(*args):
            if '-create' in args:raise OSError('merge failed')
            return fake_command(*args)
        with patch.object(deps,'command',side_effect=failed_merge),self.assertRaisesRegex(OSError,'merge failed'):
            deps.relocate(binary,report)
        self.assertEqual(binary.read_bytes(),b'original')
        self.assertEqual(list(self.root.glob('.shiny-relocate-*')),[])

    def test_macho_malformed_paths_and_system_traversal(self):
        binary=self.root/'shiny';binary.write_bytes(b'\xcf\xfa\xed\xfe'+bytes(60))
        with patch.object(deps,'command',side_effect=['shiny:\n','shiny:\n','arm64',
             'Load command 0\n cmd LC_RPATH\n missing path\n']),self.assertRaisesRegex(OSError,'malformed Mach-O'):
            deps.inspect(binary)
        self.assertTrue(deps.system_library('Mach-O','/usr/lib/libSystem.B.dylib'))
        self.assertFalse(deps.system_library('Mach-O','/usr/lib/../../tmp/codec.dylib'))


if __name__=='__main__':unittest.main(verbosity=2)
