"""Rejoin reservation boundaries, room state and actual replacement UDP connections."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

BINARY=Path(sys.argv[1]).resolve()
sys.argv[1:]=[]
ROOT=Path(__file__).resolve().parents[1]
NETWORK=json.loads(subprocess.check_output([str(BINARY),'--api'],encoding='utf-8'))['modules']['network']


class Rejoin(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix='shiny-rejoin-')
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)

    def project(self,name,source):
        path=self.root/name; path.mkdir()
        shutil.copytree(ROOT/'lua/shiny',path/'lib/shiny')
        (path/'main.lua').write_text(source,encoding='utf-8')
        return path

    def test_reservations_and_restore(self):
        project=self.project('contracts','''local R=require('shiny.rejoin')
local a,b,c,d=string.rep('a',32),string.rep('b',32),string.rep('c',32),string.rep('d',32)
return {init=function()
    local r=R.new()
    assert(R.join(r,11,'',a,0)==1); assert(R.join(r,12,'',b,0)==2); assert(R.join(r,13,'',c,0)==3)
    assert(not R.join(r,14,'',d,0)); assert(not R.join(r,14,a,nil,0))
    assert(R.detach(r,11,1)==1); assert(R.detach(r,11,5)==nil)
    assert(not R.join(r,14,string.rep('f',32),nil,5)); assert(not R.join(r,14,'bad',nil,5))
    assert(R.join(r,14,a,nil,30.999)==1); assert(R.player(r,11)==nil and R.player(r,14)==1)
    assert(R.detach(r,11,31)==nil); assert(R.player(r,14)==1)
    assert(R.detach(r,14,31)==1)
    assert(not R.join(r,15,a,nil,61)) -- Exact deadline is expired.
    local expired=R.expire(r,61); assert(#expired==1 and expired[1]==1)
    assert(not R.join(r,15,a,nil,61)); assert(R.join(r,15,'',d,61)==4)
    assert(R.detach(r,12,62)==2)
    sc.state.set('roster',r)
    local restored=R.restore(sc.state.get('roster'))
    assert(R.join(restored,16,b,nil,63)==2)
    assert(R.player(r,16)==nil and R.player(restored,16)==2)
    assert(not pcall(R.expire,restored,62)); assert(not pcall(R.expire,restored,0/0))
    local copy=sc.state.get('roster'); copy.version=0; assert(not pcall(R.restore,copy))
    copy=sc.state.get('roster'); copy.slots[1].token=copy.slots[2].token; assert(not pcall(R.restore,copy))
    copy=sc.state.get('roster'); copy.slots[1].player=copy.slots[2].player; assert(not pcall(R.restore,copy))
    copy=sc.state.get('roster'); copy.slots[1].peer=copy.slots[3].peer; assert(not pcall(R.restore,copy))
    copy=sc.state.get('roster'); copy.slots[2].expires=10000; assert(not pcall(R.restore,copy))
    copy=sc.state.get('roster'); copy.slots[2]=nil; assert(not pcall(R.restore,copy))
    assert(not pcall(R.new,0)); assert(not pcall(R.new,33))
    sc.debug.watch('verified',true)
end}''')
        run=subprocess.run([str(BINARY),'--headless',str(project),'--frames','0'],capture_output=True,text=True,encoding='utf-8',timeout=15)
        self.assertEqual(run.returncode,0,run.stderr)
        self.assertTrue(json.loads(run.stdout)['watches']['verified'])

    @unittest.skipUnless(NETWORK,'requires network-enabled engine')
    def test_check_rejects_entropy_and_socket_side_effects(self):
        project=self.project('check','''return {init=function()
    for _,call in ipairs({function() sc.net.token() end,
                         function() sc.net.host('127.0.0.1',0) end,
                         function() sc.net.join('127.0.0.1',1) end}) do
        local ok,err=pcall(call); assert(not ok and err:find('forbidden during check'))
    end
end}''')
        run=subprocess.run([str(BINARY),'--check',str(project)],capture_output=True,text=True,encoding='utf-8',timeout=15)
        self.assertEqual(run.returncode,0,run.stderr)

    @unittest.skipUnless(NETWORK,'requires network-enabled engine')
    def test_tokens_do_not_advance_gameplay_randomness(self):
        results=[]
        for name,issuance in (('baseline',''),('tokens','for i=1,8 do assert(sc.net.token()) end')):
            project=self.project(name,'''local numbers={}
return {update=function()
    numbers[#numbers+1]=sc.random(1,1000000)
    ISSUE
    numbers[#numbers+1]=sc.random(1,1000000)
    sc.debug.watch('random',numbers)
end}'''.replace('ISSUE',issuance))
            run=subprocess.run([str(BINARY),'--headless',str(project),'--frames','4','--seed','42'],
                               capture_output=True,text=True,encoding='utf-8',timeout=15)
            self.assertEqual(run.returncode,0,run.stderr)
            results.append(json.loads(run.stdout))
        self.assertEqual(results[0]['watches'],results[1]['watches'])
        self.assertEqual(results[0]['hash'],results[1]['hash'])

    @unittest.skipUnless(NETWORK,'requires network-enabled engine')
    def test_session_state_isolated_from_saves_and_candidate_mutation(self):
        project=self.project('isolated','''local h=sc.net.bind('coop')
return {update=function()
    if not h then
        h=assert(sc.net.host('127.0.0.1',0)); assert(h:persist('coop'))
        assert(h:state({token='PRIVATE_PROTOCOL_MARKER',peer=42,clock=sc.net.time()}))
        sc.state.set('coins',7); assert(sc.save.write('slot')); sc.scene('next.lua')
    else
        assert(h:state().token=='PRIVATE_PROTOCOL_MARKER')
        assert(sc.state.get('coins')==7)
        assert(h:state(nil)); assert(h:stats().state_bytes==0)
        h:close(); sc.debug.watch('verified',true); sc.app.quit()
    end
end}''')
        (project/'project.lua').write_text('return {id="session.state",rooms={"main.lua","next.lua"}}',encoding='utf-8')
        (project/'next.lua').write_text('''local h=assert(sc.net.bind('coop'))
return {init=function()
    assert(h:state().token=='PRIVATE_PROTOCOL_MARKER')
    local ok,err=pcall(h.state,h,{token='wrong'}); assert(not ok and err:find('candidate initialization'))
    assert(not pcall(h.state,h,nil)); assert(h:state().peer==42)
    local allowed,problem=pcall(sc.net.token); assert(not allowed and problem:find('candidate initialization'))
end,update=function()
    local saved=assert(sc.save.read('slot')); assert(saved.state.coins==7 and saved.state.token==nil)
    assert(sc.save.load('slot'))
end,draw=function()
    assert(h:state().peer==42)
    local ok,err=pcall(h.state,h,{}); assert(not ok and err:find('forbidden in draw'))
    ok,err=pcall(sc.net.token); assert(not ok and err:find('forbidden in draw'))
end}''',encoding='utf-8')
        saves=self.root/'saves'; trace=self.root/'trace.jsonl'
        run=subprocess.run([str(BINARY),'--headless',str(project),'--frames','6','--save-dir',str(saves),'--trace',str(trace)],
                           capture_output=True,text=True,encoding='utf-8',timeout=15)
        self.assertEqual(run.returncode,0,run.stderr)
        self.assertTrue(json.loads(run.stdout)['watches']['verified'])
        self.assertNotIn('PRIVATE_PROTOCOL_MARKER',run.stdout+trace.read_text(encoding='utf-8'))
        records=list(saves.rglob('*.json')); self.assertTrue(records)
        for record in records:
            self.assertNotIn('PRIVATE_PROTOCOL_MARKER',record.read_text(encoding='utf-8'))

    @unittest.skipUnless(NETWORK,'requires network-enabled engine')
    def test_two_process_token_rejoin_across_host_room(self):
        host_source='''local R=require('shiny.rejoin')
local h,r
local initial=INITIAL
return {init=function()
    if initial then r=R.new() else
        h=assert(sc.net.bind('coop')); r=R.restore(h:state())
        assert(sc.net.time()>=r.time)
    end
end,update=function()
    local now=assert(sc.net.time()); assert(now==sc.net.time())
    if not h then h=assert(sc.net.host('127.0.0.1',0)); assert(h:persist('coop')); sc.log('PORT '..h:port()) end
    assert(#R.expire(r,now)==0)
    while true do
        local e,err=h:poll(); assert(not err); if not e then break end
        if e.type=='receive' then
            if e.data=='new' then
                assert(initial); local token=assert(sc.net.token()); assert(R.join(r,e.peer,'',token,now)==1)
                assert(h:send(e.peer,'welcome:'..token)); assert(h:disconnect(e.peer))
            elseif e.data:sub(1,7)=='resume:' then
                assert(not initial)
                local player=R.join(r,e.peer,e.data:sub(8),nil,now)
                if player then
                    assert(player==1 and e.peer>1); assert(h:send(e.peer,'ok'))
                else assert(h:send(e.peer,'denied')) end
            elseif e.data=='done' then
                assert(R.player(r,e.peer)==1); sc.log('RESUMED'); sc.app.quit()
            else error('bad protocol') end
        elseif e.type=='disconnect' and initial then
            assert(R.detach(r,e.peer,now)==1); assert(h:state(r)); sc.scene('next.lua')
        end
    end
    assert(h:flush())
end}'''
        host_project=self.project('host',host_source.replace('INITIAL','true'))
        (host_project/'next.lua').write_text(host_source.replace('INITIAL','false'),encoding='utf-8')
        (host_project/'project.lua').write_text('return {rooms={"main.lua","next.lua"}}',encoding='utf-8')
        log_path=self.root/'host.log'
        with log_path.open('w',encoding='utf-8') as log:
            host=subprocess.Popen([str(BINARY),'--headless','--realtime',str(host_project),'--frames','240'],stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+5
                while 'PORT ' not in log_path.read_text(encoding='utf-8'):
                    if host.poll() is not None or time.monotonic()>deadline:
                        self.fail(log_path.read_text(encoding='utf-8'))
                    time.sleep(.01)
                import re
                port=re.search(r'PORT (\d+)',log_path.read_text(encoding='utf-8'))[1]
                client_project=self.project('client','''local h,token,peer
return {update=function()
    if not h then h=assert(sc.net.join('127.0.0.1',PORT)); assert(h:persist('coop')) end
    while true do
        local e,err=h:poll(); assert(not err); if not e then break end
        if e.type=='connect' then
            peer=e.peer
            local wrong=token and ((token:sub(1,1)=='f' and 'e' or 'f')..token:sub(2))
            assert(h:send(peer,wrong and 'resume:'..wrong or 'new'))
        elseif e.type=='receive' then
            if e.data:sub(1,8)=='welcome:' then token=e.data:sub(9)
            elseif e.data=='denied' then assert(token); assert(h:send(peer,'resume:'..token))
            elseif e.data=='ok' then
                assert(h:send(peer,'done')); assert(h:flush()); sc.log('REJOIN COMPLETE'); sc.app.quit()
            else error('bad reply') end
        elseif e.type=='disconnect' then assert(token); h:close(); h=nil; break end
    end
    if h then assert(h:flush()) end
end}'''.replace('PORT',port))
                client=subprocess.run([str(BINARY),'--headless','--realtime',str(client_project),'--frames','210'],
                                      capture_output=True,text=True,encoding='utf-8',timeout=10)
                self.assertEqual(client.returncode,0,client.stderr)
                self.assertIn('REJOIN COMPLETE',client.stderr)
                self.assertEqual(host.wait(timeout=10),0,log_path.read_text(encoding='utf-8'))
                self.assertIn('RESUMED',log_path.read_text(encoding='utf-8'))
            finally:
                if host.poll() is None: host.kill()
                host.wait()


if __name__=='__main__': unittest.main()
