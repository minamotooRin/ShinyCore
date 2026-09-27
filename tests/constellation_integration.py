"""Exercise the playable four-player example through actual native processes."""
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

BINARY=Path(sys.argv[1]).resolve()
NATIVE='--native' in sys.argv
WEAK='--weak' in sys.argv
sys.argv[1:]=[]
ROOT=Path(__file__).resolve().parents[1]
NETWORK=json.loads(subprocess.check_output([str(BINARY),'--api'],encoding='utf-8'))['modules']['network']


class Constellation(unittest.TestCase):
    def test_project_and_protocol(self):
        check=subprocess.run([str(BINARY),'--check-all',str(ROOT/'examples/constellation')],capture_output=True,text=True,encoding='utf-8',timeout=15)
        if NETWORK: self.assertEqual(check.returncode,0,check.stderr)
        else:
            self.assertNotEqual(check.returncode,0)
            self.assertIn('required module unavailable: network',check.stdout+check.stderr)
        for name in ('snapshot','rejoin'):
            self.assertEqual((ROOT/f'examples/constellation/lib/shiny/{name}.lua').read_bytes(),(ROOT/f'lua/shiny/{name}.lua').read_bytes())
        with tempfile.TemporaryDirectory(prefix='shiny-coop-codec-') as temp:
            project=Path(temp)/'game'; shutil.copytree(ROOT/'examples/constellation',project)
            # Codec validation needs no transport, including in the lightweight build.
            (project/'project.lua').write_text('return {id="shiny.coop-codec"}',encoding='utf-8')
            (project/'main.lua').write_text('''local P=require('game.protocol')
return {init=function()
    assert(P.read(P.hello(''),'reliable').token=='')
    assert(P.read(P.welcome(2,2,1,1,string.rep('a',32)),'reliable').id==2)
    assert(P.read(P.input(1,1,-1,1),'state').x==-1)
    assert(P.read(P.room(2,2),'reliable').room==2)
    assert(P.read(P.leave(),'reliable').kind==P.LEAVE)
    assert(P.read(P.reject(3),'reliable').code==3)
    local encoded=P.state({tick=3,epoch=1,room=1,charge=.5,complete=false,
        players={{id=1,seat=1,x=100,y=140,active=true},{id=2,seat=2,x=110,y=190,active=false}}})
    local decoded=assert(P.read(encoded,'state')); assert(#decoded.objects==2 and decoded.charge==500)
    for _,data in ipairs({'',encoded:sub(1,-2),encoded..'x',P.input(1,1,2,0)}) do
        assert(not P.read(data,'state'))
    end
    assert(not P.read(P.hello('bad'),'reliable'))
    for _,data in ipairs({P.welcome(2,2,1,1,string.rep('a',32)),P.room(2,2),P.leave(),P.reject(3)}) do
        for n=0,#data-1 do assert(not P.read(data:sub(1,n),'reliable')) end
        assert(not P.read(data..'x','reliable'))
    end
    assert(not P.read(encoded,'reliable'))
    assert(P.newer(0,0xffffffff) and not P.newer(0xffffffff,0) and not P.newer(4,4))
end}''',encoding='utf-8')
            run=subprocess.run([str(BINARY),'--headless',str(project),'--frames','0'],capture_output=True,text=True,encoding='utf-8',timeout=15)
            self.assertEqual(run.returncode,0,run.stderr)

    @unittest.skipUnless(NETWORK,'requires network-enabled engine')
    def test_four_player_rooms_and_rejoin(self):
        temporary=tempfile.TemporaryDirectory(prefix='shiny-constellation-')
        self.addCleanup(temporary.cleanup)
        root=Path(temporary.name)
        flavor=('native-' if NATIVE else '')+('weak' if WEAK else 'loopback')
        artifacts=BINARY.parent/'verification-constellation'/flavor
        artifacts.mkdir(parents=True,exist_ok=True)
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
        processes=[]; files=[]
        def wait_text(path,marker,process):
            deadline=time.monotonic()+6
            while marker not in path.read_text(encoding='utf-8'):
                if process.poll() is not None or time.monotonic()>deadline:
                    self.fail(path.read_text(encoding='utf-8'))
                time.sleep(.01)
        def launch(name,host=False,client_port=None,reconnect=False):
            project=root/name; shutil.copytree(ROOT/'examples/constellation',project)
            config=project/'game/config.lua'
            config.write_text(config.read_text(encoding='utf-8').replace('port=7778',f'port={port if host else client_port}'),encoding='utf-8')
            replay=project/'replays'/('host.jsonl' if host else 'rejoin.jsonl' if reconnect else 'client.jsonl')
            log_path=root/(name+'.log'); result_path=root/(name+'.json')
            log=log_path.open('w',encoding='utf-8'); result=result_path.open('w',encoding='utf-8'); files.extend([log,result])
            command=[str(BINARY),str(project),'--frames','960' if host else '900','--replay',str(replay)]
            if NATIVE and host: command+=['--capture',str(artifacts/'native.png')]
            else: command+=['--headless','--realtime']
            process=subprocess.Popen(command,stdout=result,stderr=log); processes.append(process)
            return process,log_path,result_path
        try:
            host,host_log,host_result=launch('host',True)
            wait_text(host_log,'CONSTELLATION listening',host)
            client_port=port
            proxy=None
            if WEAK:
                proxy_log_path=root/'proxy.log'; proxy_log=proxy_log_path.open('w',encoding='utf-8'); files.append(proxy_log)
                proxy=subprocess.Popen([sys.executable,str(ROOT/'tools/net_proxy.py'),'--target',f'127.0.0.1:{port}',
                                        '--duration','17','--report',str(artifacts/'proxy.json'),'--seed','734','--duplicate','.03'],
                                       stdout=proxy_log,stderr=proxy_log)
                processes.append(proxy); wait_text(proxy_log_path,'"event": "ready"',proxy)
                client_port=json.loads(proxy_log_path.read_text(encoding='utf-8').splitlines()[0])['listen'][1]
            clients=[launch(f'client{i}',client_port=client_port,reconnect=i==0) for i in range(3)]
            states={}
            for name,(process,log_path,result_path) in [('host',(host,host_log,host_result)),*[(f'client{i}',v) for i,v in enumerate(clients)]]:
                self.assertEqual(process.wait(timeout=20),0,log_path.read_text(encoding='utf-8'))
                log=log_path.read_text(encoding='utf-8')
                state=json.loads(result_path.read_text(encoding='utf-8'))['watches'].get('coop',{})
                states[name]=state
                self.assertTrue(state.get('complete'),name+': '+log+'\n'+str(state))
                self.assertEqual(state['room'],2)
                self.assertEqual(len(state['players']),4)
                self.assertIn('CONSTELLATION complete',log)
            self.assertEqual(states['host']['rejoins'],1)
            self.assertEqual(states['host']['invalid'],0)
            if proxy:
                self.assertEqual(proxy.wait(timeout=5),0)
                stats=json.loads((artifacts/'proxy.json').read_text(encoding='utf-8'))
                self.assertEqual(stats['clients'],4) # Replacement connection has a new source port.
                for direction in stats['directions'].values():
                    self.assertGreater(direction['lost'],0); self.assertGreater(direction['duplicated'],0)
                    self.assertEqual(direction['overflow'],0)
            name=('native-' if NATIVE else '')+('weak' if WEAK else 'loopback')
            (artifacts/(name+'.json')).write_text(json.dumps(states,indent=2)+'\n',encoding='utf-8')
        finally:
            for process in processes:
                if process.poll() is None: process.kill()
                process.wait()
            for file in files: file.close()
            # Preserve failure logs too; test directories can then be safely discarded.
            for log in root.glob('*.log'): shutil.copy2(log,artifacts/log.name)


if __name__=='__main__': unittest.main()
