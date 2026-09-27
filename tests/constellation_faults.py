"""Actual protocol rejection, silent-peer timeout and 30-second token expiry."""
import json
from pathlib import Path
import re
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


@unittest.skipUnless(NETWORK,'requires network-enabled engine')
class Faults(unittest.TestCase):
    def test_rejection_timeouts_and_expiry(self):
        with tempfile.TemporaryDirectory(prefix='shiny-coop-faults-') as temp:
            root=Path(temp)
            def project(name,source):
                target=root/name
                shutil.copytree(ROOT/'examples/constellation',target)
                (target/'main.lua').write_text(source,encoding='utf-8')
                return target
            host_project=project('host','''local Server=require('game.server')
local h,s
return {update=function()
    if not h then
        h=assert(sc.net.host('127.0.0.1',0,8)); assert(h:persist('coop'))
        s=Server.new(); sc.log('PORT '..h:port())
    end
    local changed,err=Server.update(s,h,0,0,sc.net.time()); assert(changed~=nil,err)
    assert(h:flush())
    local count=0; for _,p in ipairs(s.players) do if p.id>0 then count=count+1 end end
    sc.debug.watch('faults',{invalid=s.invalid,players=count,pending=next(s.pending)~=nil})
end}''')
            host_log=root/'host.log'; host_out=root/'host.json'
            with host_log.open('w',encoding='utf-8') as log, host_out.open('w',encoding='utf-8') as out:
                host=subprocess.Popen([str(BINARY),'--headless','--realtime',str(host_project),'--frames','2520'],stdout=out,stderr=log)
                try:
                    deadline=time.monotonic()+6
                    match=None
                    while not match:
                        match=re.search(r'PORT (\d+)',host_log.read_text(encoding='utf-8'))
                        if host.poll() is not None or time.monotonic()>deadline:
                            self.fail(host_log.read_text(encoding='utf-8'))
                        time.sleep(.01)
                    client_project=project('client','''local P=require('game.protocol')
local h,peer,token,connected,detached
local stage=1
return {update=function()
    local now=sc.net.time()
    if stage==4 and now-detached<30.2 then return end
    if not h then h=assert(sc.net.join('127.0.0.1',PORT)); assert(h:persist('probe')) end
    while true do
        local e,err=h:poll(); assert(not err,err); if not e then break end
        if e.type=='connect' then
            peer,connected=e.peer,now
            if stage==1 then assert(h:send(peer,P.input(1,1,2,0),'state'))
            elseif stage==3 then assert(h:send(peer,P.hello('')))
            elseif stage==4 then assert(h:send(peer,P.hello(token))) end
        elseif e.type=='receive' then
            local m=assert(P.read(e.data,e.channel))
            if stage==1 then assert(m.kind==P.REJECT and m.code==1)
            elseif stage==3 and m.kind==P.WELCOME then token=m.token
            elseif stage==4 then
                assert(m.kind==P.REJECT and m.code==2)
                sc.debug.watch('expiry_elapsed',now-detached)
                sc.debug.watch('verified',true); sc.app.quit()
            elseif m.kind~=P.STATE then error('unexpected host response') end
        elseif e.type=='disconnect' then
            if stage==2 then assert(now-connected>=2.9); sc.debug.watch('handshake_timeout',now-connected)
            elseif stage==3 then
                assert(token and now-connected>=4.9); detached=now
                sc.debug.watch('input_timeout',now-connected)
            end
            assert(stage<4); stage=stage+1; h:close(); h=nil; break
        end
    end
    if h then assert(h:flush()) end
end}'''.replace('PORT',match[1]))
                    run=subprocess.run([str(BINARY),'--headless','--realtime',str(client_project),'--frames','2460'],
                                       capture_output=True,text=True,encoding='utf-8',timeout=45)
                    self.assertEqual(run.returncode,0,run.stderr)
                    watches=json.loads(run.stdout)['watches']
                    self.assertTrue(watches.get('verified'),run.stderr)
                    self.assertGreaterEqual(watches['expiry_elapsed'],30.2)
                    self.assertEqual(host.wait(timeout=10),0,host_log.read_text(encoding='utf-8'))
                finally:
                    if host.poll() is None: host.kill()
                    host.wait()
            host_state=json.loads(host_out.read_text(encoding='utf-8'))['watches']['faults']
            self.assertEqual(host_state,{'invalid':2,'players':1,'pending':False})
            artifacts=BINARY.parent/'verification-constellation'
            artifacts.mkdir(exist_ok=True)
            (artifacts/'faults.json').write_text(json.dumps({'host':host_state,'client':watches},indent=2)+'\n',encoding='utf-8')


if __name__=='__main__': unittest.main()
