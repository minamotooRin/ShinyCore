"""File selection must keep update-only modules and every indexed stream chunk."""
from pathlib import Path
import json
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from package_content import closure, requires


class ContentTests(unittest.TestCase):
    def setUp(self):
        directory=tempfile.TemporaryDirectory(prefix='shiny-content-')
        self.addCleanup(directory.cleanup);self.root=Path(directory.name)
        self.file('project.lua','return {}')

    def file(self,name,value):
        path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(value,encoding='utf-8');return path

    def manifest(self,**fields):
        self.file('package.json',json.dumps({'format':1,**fields}))

    def test_lua_comments_literals_and_update_only_module(self):
        self.manifest(scripts=['main.lua'],files=['docs/api.lua'])
        self.file('docs/api.lua','function require(name) end')
        self.file('main.lua',"""-- require('absent')
local text=[=[ require('also_absent') ]=]
--[==[ require('missing') ]==]
return {update=function() require('game.later') end}
""")
        self.file('game/later.lua',"return require [[shiny.ui]]")
        self.file('lib/shiny/ui.lua','return {}');self.file('lib/shiny/unused.lua','return {}')
        report=closure(self.root);files={item['path'] for item in report['files']}
        self.assertIn('game/later.lua',files);self.assertIn('lib/shiny/ui.lua',files)
        self.assertEqual(report['omitted_files'],['lib/shiny/unused.lua'])

    def test_dynamic_alias_and_escaped_names_need_declaration(self):
        for source in ('require(prefix..name)','local load=require; load(name)',r'require("game.\x61")'):
            with self.subTest(source=source),self.assertRaisesRegex(OSError,'dynamic/aliased require'):
                requires(source,'main.lua',[])
            self.assertEqual(requires(source,'main.lua',['game.a']),['game.a'])
        self.manifest(scripts=['main.lua'],dynamic_modules={'main.lua':['game.a']})
        self.file('main.lua','local load=require; return load("game.a")')
        self.file('game/a.lua','return {}')
        self.assertIn('game/a.lua',{item['path'] for item in closure(self.root)['files']})

    def test_missing_module_and_unused_dynamic_owner(self):
        self.manifest(scripts=['main.lua']);self.file('main.lua','return require("absent")')
        with self.assertRaisesRegex(OSError,'main.lua: missing.*absent.lua'):closure(self.root)
        self.file('main.lua','return {}')
        self.manifest(scripts=['main.lua'],dynamic_modules={'absent.lua':['game.a']})
        with self.assertRaisesRegex(OSError,'owners are not shipped'):closure(self.root)

    def test_stream_dependencies_and_bad_chunk_size(self):
        self.manifest(stream_maps=['maps/index.json'])
        self.file('maps/0_0.json','{}');self.file('assets/tiles.png','image fixture')
        index={'format':3,'chunk_size':32,'chunks':[{'path':'0_0.json','bytes':2}],
               'tilesets':[{'image':'assets/tiles.png'}],'layers':[]}
        self.file('maps/index.json',json.dumps(index))
        files={item['path'] for item in closure(self.root)['files']}
        self.assertTrue({'maps/0_0.json','assets/tiles.png'}<=files)
        self.file('maps/0_0.json','corrupt')
        with self.assertRaisesRegex(OSError,'byte size'):closure(self.root)

    def test_invalid_manifest_and_paths(self):
        for fields in ({'unknown':True},{'format':True},{'scripts':'main.lua'},
                       {'files':['../outside']},{'files':['/absolute']},{'files':['C:/outside']},
                       {'files':['nul\0file']},{'files':['missing']}):
            with self.subTest(fields=fields):
                self.manifest(**fields)
                with self.assertRaises(OSError):closure(self.root)

    def test_stream_object_files_and_collection_tiles(self):
        self.manifest(stream_maps=['maps/index.json'])
        self.file('dialogue/npc.txt','hello');self.file('sprites/npc.png','image fixture')
        content={'objects':[{'id':7,'properties':[{'name':'dialogue','type':'file','value':'dialogue/npc.txt'},
                                                {'name':'optional','type':'file','value':''}]}]}
        def chunk():
            written=self.file('maps/0_0.json',json.dumps(content))
            self.file('maps/index.json',json.dumps({'format':3,'chunk_size':32,
                'chunks':[{'path':'0_0.json','bytes':written.stat().st_size}],
                'tilesets':[{'tiles':[{'id':5,'image':'sprites/npc.png'}]}],'layers':[]}))
        chunk()
        files={item['path'] for item in closure(self.root)['files']}
        self.assertTrue({'dialogue/npc.txt','sprites/npc.png'}<=files)
        content['objects'][0]['properties'][0]['value']='../outside.txt';chunk()
        with self.assertRaisesRegex(OSError,'object 7 property dialogue'):closure(self.root)


if __name__=='__main__':unittest.main(verbosity=2)
