"""Check authored entity fields against the executable's native contract tables."""
import importlib.util
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve()
del sys.argv[1]
spec = importlib.util.spec_from_file_location('api_docs', ROOT/'tools/api_docs.py')
api_docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api_docs)


def lua(value):
    if isinstance(value, dict):
        return '{'+','.join('['+json.dumps(k)+']='+lua(v) for k, v in value.items())+'}'
    if isinstance(value, list):
        return '{'+','.join(map(lua, value))+'}'
    return json.dumps(value)


class Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.api = json.loads(subprocess.check_output([str(BINARY), '--api'], encoding='utf-8'))
        cls.patch = cls.api['types']['ScEntityPatch']

    def run_lua(self, source, scene=False):
        with tempfile.TemporaryDirectory(prefix='shiny-contract-') as temp:
            project = Path(temp)
            text = source if scene else 'return {init=function()\n'+source+'\nend}'
            (project/'main.lua').write_text(text, encoding='utf-8')
            result = subprocess.run([str(BINARY), '--headless', str(project), '--frames', '0'],
                                    capture_output=True, text=True, encoding='utf-8', timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_generated_annotation_and_reference_are_current(self):
        annotations = (ROOT/'docs/api.lua').read_text(encoding='utf-8')
        if self.api['modules']['network'] and self.api['modules']['streaming']:
            api_docs.generate([BINARY], ROOT/'docs/api.lua', ROOT/'docs/api-reference.md', check=True)
        else:
            # A minimal binary cannot regenerate declarations for disabled modules.
            # Its field contracts and structured functions must still match exactly.
            with tempfile.TemporaryDirectory() as temp:
                output, reference = Path(temp)/'api.lua', Path(temp)/'api-reference.md'
                output.write_text(annotations, encoding='utf-8')
                api_docs.generate([BINARY], output, reference)
                self.assertEqual(output.read_text(encoding='utf-8').split(api_docs.MARKER)[0], annotations.split(api_docs.MARKER)[0])
                available = {entry['name'].replace('ScNetSession:', 'session:') for entry in self.api['functions']}
                def available_reference(text):
                    text=''.join(section for section in re.split(r'(?=^## )',text,flags=re.M)
                                 if not section.startswith('## Sc') or section.splitlines()[0][3:] in self.api['types'])
                    return '\n'.join(line for line in text.splitlines()
                                     if not line.startswith(('| sc.', '| session:'))
                                     or line.split('|')[1].strip() in available)
                self.assertEqual(reference.read_text(encoding='utf-8').rstrip(),
                                 available_reference((ROOT/'docs/api-reference.md').read_text(encoding='utf-8')).rstrip())
        for entry in self.api['functions']:
            if entry.get('contract'):
                self.assertIn('\n'.join(api_docs.function_annotations(entry['name'].replace('ScNetSession:', 'session:'), entry)), annotations)
        for name,record in self.api['types'].items():
            block=re.search(r'^---@class '+re.escape(name)+r'\b[^\n]*\n-- Fields generated from native --api\.\n((?:---@field[^\n]*\n)*)',annotations,re.M)
            self.assertIsNotNone(block,name)
            self.assertEqual(set(re.findall(r'^---@field (\w+)\?? ',block[1],re.M)),
                             {field['name'] for field in record['fields']},name)
        for path in (ROOT/'examples').glob('*/docs/api.lua'):
            self.assertEqual(path.read_bytes(), (ROOT/'docs/api.lua').read_bytes(), str(path))

    def test_generation_preserves_declarations_after_type_fields(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'api.lua'
            output.write_text('''---@class ScAudioOptions
---@field volume? number
sc.audio = {}
---@alias PreservedAlias 'value'
---@param text string
function sc.preserved(text) end

---@class PreservedType
---@field item string
''',encoding='utf-8')
            api_docs.generate([BINARY],output)
            text=output.read_text(encoding='utf-8')
            for declaration in ['sc.audio = {}',"---@alias PreservedAlias 'value'",
                                '---@param text string','function sc.preserved(text) end',
                                '---@class PreservedType','---@field item string']:
                self.assertEqual(text.splitlines().count(declaration),1,declaration)
            api_docs.generate([BINARY],output,check=True)

    def test_function_generation_preserves_adjacent_types(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'api.lua'
            output.write_text('''---@class ScRayHit
---@field id integer
---Old ray description.
---@param x number
function sc.physics.ray(x,y,dx,dy) end
---@alias ScJointId integer
---Old joint description.
function sc.physics.joint(...) end
---@class PreservedType
---@field value string
---@return table
function sc.physics.contacts() end
''',encoding='utf-8')
            api_docs.generate([BINARY],output)
            text=output.read_text(encoding='utf-8')
            block=text.split('---@class ScRayHit\n',1)[1].split('function sc.physics.ray',1)[0]
            for field in self.api['types']['ScRayHit']['fields']:
                self.assertIn('---@field '+field['name']+' ',block)
            self.assertEqual(text.count('---@alias ScJointId integer'),1)
            self.assertIn('---@class PreservedType\n---@field value string\n',text)
            self.assertNotIn('Old ray description',text)
            api_docs.generate([BINARY],output,check=True)
        self.assertEqual(api_docs.literal(float(2**52-1)),str(2**52-1))

    def test_particle_emitter_fields(self):
        fields=self.api['types']['ScParticleEmitterSpec']['fields']
        self.assertEqual({f['name'] for f in fields},{'speed_min','speed_max','life_min','life_max','angle_min','angle_max','gravity','curve','texture','blend'})
        defaults={f['name']:f['default'] for f in fields if 'default' in f}
        checks=[]
        for field in fields:
            if 'minimum' not in field: continue
            for value in [field['minimum']-1,field['maximum']+1,'wrong',False]:
                patch={field['name']:value}
                checks.append('assert(not pcall(sc.particles.define,'+lua(patch)+'))')
        checks.extend(['assert(sc.particles.stats().emitters==0)',
                       'assert(sc.particles.define('+lua(defaults)+')>0)',
                       'assert(sc.particles.stats().emitters==1)'])
        self.run_lua('\n'.join(checks))

    def test_function_phases_match_execution(self):
        functions = {f['name']: f['contract'] for f in self.api['functions'] if f.get('contract') and f['name'].count('.') == 1}
        mutations = {'sc.spawn', 'sc.spawn_many', 'sc.set', 'sc.set_many', 'sc.destroy'}
        self.assertEqual(len(functions), 10)
        for name, contract in functions.items():
            self.assertTrue(self.api['modules'][contract['module']])
            self.assertEqual('draw' in contract['phases'], name not in mutations)
            self.assertTrue(all(p['required'] for p in contract['parameters']))
        self.run_lua('''local id=sc.spawn({tag="contract",x=3})
local function read()
    assert(sc.get(id).x==3 and sc.get_many({id})[1].id==id)
    assert(sc.find("contract")==id and sc.find_all("contract")[1]==id)
    assert(sc.overlap(id,id))
end
read()
return {init=read,draw=function()
    read()
    for _,call in ipairs({function() sc.spawn({}) end,function() sc.spawn_many({}) end,function() sc.set(id,{x=9}) end,
        function() sc.set_many({{id=id,patch={x=9}}}) end,function() sc.destroy(id) end}) do
        local ok,error=pcall(call); assert(not ok and error:find("forbidden"))
    end
    read()
end}''', scene=True)

    def test_field_errors_identify_the_authored_field(self):
        self.run_lua('''local id=sc.spawn({})
for name,value in pairs({w=0,layer=.5,flip_x=3,tag=42}) do
    local ok,error=pcall(sc.set,id,{[name]=value})
    assert(not ok and error:find(name),error)
end''')

    def test_contract_names_and_defaults_match_runtime(self):
        fields = self.patch['fields']
        self.assertEqual({f['name'] for f in fields}, set(self.api['entity_fields']))
        self.assertEqual(len(fields), len(self.api['entity_fields']))
        read_fields = self.api['types']['ScEntity']['fields']
        self.assertEqual({f['name'] for f in read_fields}, set(self.api['entity_fields']+self.api['entity_readonly_fields']))
        defaults = {f['name']: f['default'] for f in fields}
        defaults['color'] = defaults['color'].lower()
        self.run_lua('local id=sc.spawn({}); local e=sc.get(id)\n'+
                     f'for name,value in pairs({lua(defaults)}) do assert(e[name]==value,"default: "..name) end\n'+
                     'assert(e.id==id and not e.grounded and e.support==0 and e.normal_x==0 and e.normal_y==0)\n'+
                     'local copies=sc.get_many({id}); copies[1].x=40; assert(sc.get(id).x==0)\n'+
                     'sc.set(id,{flip_x=true,flip_y=true}); assert(sc.get(id).flip_x and sc.get_many({id})[1].flip_y)')

    def test_numeric_limits_types_and_atomic_failures(self):
        good, bad = [], []
        for field in self.patch['fields']:
            if 'minimum' not in field:
                continue
            name = field['name']
            for value in (field['minimum'], field['maximum']):
                patch = {name: value}
                if name in ('frame_w', 'frame_h'):
                    patch['frame_w'] = patch['frame_h'] = value
                good.append({'field': name, 'value': value, 'patch': patch})
            for value in (field['minimum']-1, field['maximum']+1, True, '1'):
                bad.append({'x': 100, name: value})
            if field['type'] == 'integer':
                bad.append({'x': 100, name: 0.5})
        self.run_lua('''local id=sc.spawn({}); local other=sc.spawn({x=4})
local function rejected(patch)
    assert(not pcall(sc.set_many,{{id=other,patch={x=99}},{id=id,patch=patch}}))
    assert(sc.get(other).x==4 and sc.get(id).x==0, "partial batch commit")
end
'''+f'''for _,case in ipairs({lua(good)}) do
    sc.set(id,case.patch)
    local read=sc.get(id)[case.field]
    assert(math.abs(read-case.value)<=math.max(1e-7,math.abs(case.value)*1e-6),case.field)
end
sc.set(id,{{x=0,frame_w=0,frame_h=0}})
for _,patch in ipairs({lua(bad)}) do rejected(patch) end
'''+'''for _,value in ipairs({0/0,math.huge,-math.huge}) do rejected({x=value}) end
rejected({frame_w=16}); rejected({frame_h=16}); rejected({unknown=2})
rejected({id=id}); rejected({grounded=true}); rejected(setmetatable({x=5},{}))
assert(not pcall(sc.set_many,{{id=id,patch={x=5}},{id=id,patch={y=5}}}))
assert(sc.get(id).x==0)
sc.destroy(id); assert(not pcall(sc.get,id))
''')

    def test_text_color_boolean_and_body_constraints(self):
        self.run_lua('''local id=sc.spawn({})
sc.set(id,{tag=string.rep("x",47),color="#12ABef",body=false})
assert(sc.get(id).tag==string.rep("x",47) and sc.get(id).color=="#12abefff")
for _,patch in ipairs({{tag=string.rep("x",48)},{tag="a\\0b"},{tag=42},
    {sprite=string.rep("x",128)},{sprite="../out.png"},{sprite="a/./b.png"},
    {color="#FFGGFF"},{color=3},{solid=1},{flip_x=1},{body=true},
    {body={type="alien"}},{body={shape="polygon",vertices={0,0,1,1,2,2}}}}) do
    assert(not pcall(sc.set,id,patch))
end
sc.set(id,{dynamic=true,body=false}); assert(not sc.get(id).dynamic and sc.get(id).body==false)
sc.set(id,{body={type="kinematic"}}); assert(sc.get(id).body.type=="kinematic")
sc.set(id,{x=4}); assert(sc.get(id).body.type=="kinematic")
''')


if __name__ == '__main__':
    unittest.main()
