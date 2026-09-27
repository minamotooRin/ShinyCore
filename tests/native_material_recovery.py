"""Opt-in hidden GPU failure checks: retained programs, reload and candidate rollback."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import threading

from PIL import Image


SHADER = '''#version 330
out vec4 finalColor;
void main() { finalColor=vec4(%s,1.0); }
'''
RED = SHADER % '.8,.2,.1'
GREEN = SHADER % '.1,.8,.2'
BROKEN = '#version 330\nvoid main() { invalid GLSL syntax; }\n'
MISMATCH = '''#version 330
uniform vec3 undeclared;
out vec4 finalColor;
void main() { finalColor=vec4(undeclared,1.0); }
'''
CASES = [('compile-retain', BROKEN, False), ('uniform-retain', MISMATCH, False),
         ('reload-recover', BROKEN, True), ('candidate-rollback', None, False),
         ('candidate-script', None, False), ('candidate-size', None, False),
         ('candidate-recover', None, False), ('candidate-small', None, False),
         ('candidate-debug', None, False)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', action='append', choices=[case[0] for case in CASES])
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'engine_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'cases': []}

    for name, failure, recover in CASES:
        if args.case and name not in args.case:
            continue
        project = output/name
        project.mkdir()
        (project/'active.frag').write_text(RED, encoding='utf-8')
        (project/'bad.frag').write_text(BROKEN, encoding='utf-8')
        (project/'good.frag').write_text(GREEN, encoding='utf-8')
        Image.new('RGB', (8, 8), 'white').save(project/'white.png')
        (project/'project.lua').write_text('''return {id="material.recovery",
display={width=320,height=180,scale="integer"},modules={"materials","devtools"},resources={
white={type="image",path="white.png"},active={type="shader",path="active.frag"},
bad={type="shader",path="bad.frag"},good={type="shader",path="good.frag"}}}''', encoding='utf-8')
        if failure:
            action = '''if sc.tick()==0 or sc.tick()==2 then sc.material.reload(material) end
    if sc.tick()==1 then sc.debug.watch("failed",sc.material.info(material)) end'''
        else:
            action = '''if sc.tick()==0 then sc.scene("bad.lua") end
    assert(sc.state.get("candidate")==nil)'''
            (project/'bad.lua').write_text('''return {width=320,height=180,ambient=1,
init=function()
    sc.state.set("candidate",true)
    sc.material.create{shader="bad"}
end}''', encoding='utf-8')
            if name == 'candidate-script':
                (project/'bad.lua').write_text('return {init=function() error("candidate script failure") end}')
            elif name == 'candidate-debug':
                (project/'bad.lua').write_text('''return {init=function()
    sc.state.set("candidate",true)
    local marker=42
    error("candidate debug failure")
end}''')
            elif name == 'candidate-size':
                (project/'bad.lua').write_text('return {width=321,height=180}')
            elif name == 'candidate-small':
                (project/'bad.lua').write_text('return {width=64,height=64,init=function() error("narrow diagnostic failure") end}')
            elif name == 'candidate-recover':
                action += '\n    if sc.tick()==1 then sc.scene("good.lua") end'
                (project/'good.lua').write_text('''local material
return {width=320,height=180,ambient=1,init=function()
    assert(sc.state.get("candidate")==nil)
    material=sc.material.create{shader="good"}
end,update=function() sc.debug.watch("current",sc.material.info(material)) end,
draw=function() sc.image("white",0,0,320,180,{material=material}) end}''')
        (project/'main.lua').write_text('''local material
return {width=320,height=180,gravity=0,ambient=1,
init=function() material=sc.material.create{shader="active"} end,
update=function()
    %s
    sc.debug.watch("current",sc.material.info(material))
end,
draw=function() sc.image("white",240,24,64,48,{material=material}) end}
''' % action, encoding='utf-8')
        if name == 'candidate-small':
            source = (project/'main.lua').read_text(encoding='utf-8')
            source = source.replace('width=320,height=180', 'width=64,height=64')
            source = source.replace('240,24,64,48', '16,0,32,8')
            (project/'main.lua').write_text(source, encoding='utf-8')
        replay = project/'idle.jsonl'
        replay.write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n')
        frames = 4 if recover or name == 'candidate-recover' else 2
        capture = project/'final.png'
        snapshot = project/'final.json'
        command = [str(binary), str(project), '--frames', str(frames), '--capture-hidden',
                   '--capture', str(capture), '--snapshot', str(snapshot), '--mute',
                   '--trace', str(project/'trace.jsonl'),
                   '--debug-stdio', '--replay', str(replay), '--save-dir', str(project/'saves')]
        if name == 'candidate-debug': command.append('--debug-load')
        messages = queue.Queue()
        transcript = []
        with (project/'stderr.log').open('w', encoding='utf-8') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=log, text=True, encoding='utf-8', bufsize=1)

            def receive():
                for line in process.stdout:
                    try:
                        messages.put(json.loads(line))
                    except ValueError as error:
                        messages.put(error)
                messages.put(EOFError('debugger ended before the expected response'))

            reader = threading.Thread(target=receive, daemon=True)
            reader.start()

            def message():
                value = messages.get(timeout=10)
                if isinstance(value, Exception):
                    raise value
                transcript.append(value)
                return value

            request = 0

            def send(name, **fields):
                nonlocal request
                request += 1
                process.stdin.write(json.dumps({'id': request, 'command': name, **fields})+'\n')
                process.stdin.flush()
                response = message()
                assert response.get('id') == request and response['ok'], response
                return response

            def step(count):
                send('step', count=count)
                event = message()
                assert event['event'] == 'stopped', event

            try:
                assert message()['event'] == 'ready'
                # The active program has already been compiled in candidate preparation.
                if failure:
                    (project/'active.frag').write_text(failure, encoding='utf-8')
                if name == 'candidate-debug':
                    initial = message()
                    assert initial['event']=='breakpoint' and not initial['candidate'] and initial['frame']==0, initial
                    assert initial['phase']==0, initial
                    send('breakpoints', file='bad.lua', lines=[4])
                    send('continue')
                    stopped = message()
                    assert stopped['event']=='breakpoint' and stopped['candidate'] and stopped['frame']==1, stopped
                    assert stopped['file']=='bad.lua' and stopped['line']==4 and stopped['phase']==0, stopped
                    assert send('state',path=['candidate'])['result']['value'] is True
                    locals_ = send('locals')['result']['items']
                    assert next(row for row in locals_ if row['name']=='marker')['value']=={'type':'integer','value':'42'}
                    send('continue')
                else:
                    step(frames if not recover else 2)
                if recover:
                    (project/'active.frag').write_text(GREEN, encoding='utf-8')
                    step(2)
                assert message()['event'] == 'terminated'
                assert process.wait(timeout=10) == 0
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)
                process.stdin.close()
                reader.join(timeout=1)
                process.stdout.close()
                (project/'protocol.json').write_text(json.dumps(transcript, indent=2)+'\n', encoding='utf-8')

        data = json.loads(snapshot.read_text(encoding='utf-8'))
        trace = [json.loads(line) for line in (project/'trace.jsonl').read_text(encoding='utf-8').splitlines()]
        assert [row['frames'] for row in trace] == list(range(1, frames+1))
        current = data['watches']['current']
        if failure:
            failed = data['watches']['failed']
            assert failed['status'] == 'failed' and failed['compiled_revision'] == 1
            assert failed['revision'] == 2 and 'active.frag' in failed['error']
            expected_error = 'undeclared or mismatched' if failure == MISMATCH else 'shader compilation'
            assert expected_error in failed['error'], failed
            if recover:
                assert current['status'] == 'ready' and current['compiled_revision'] == 3
                assert current['error'] == ''
        else:
            assert data['scene'] == ('good.lua' if name == 'candidate-recover' else 'main.lua')
            assert 'candidate' not in data['state']
            assert current['status'] == 'ready' and current['compiled_revision'] == 1
            expected_error = {'candidate-script': 'candidate script failure',
                              'candidate-debug': 'candidate debug failure',
                              'candidate-small': 'narrow diagnostic failure',
                              'candidate-size': 'scene view dimensions must match'}.get(name, 'bad.frag: shader compilation')
            assert expected_error in (project/'stderr.log').read_text(encoding='utf-8')
        with Image.open(capture) as image:
            actual = image.getpixel((144, 34) if name == 'candidate-small' else (272, 48))[:3]
            if name == 'candidate-recover':
                assert image.getpixel((20, 100))[:3] == actual, 'Old error overlay survived a successful room commit'
            elif not failure:
                text = [(x, y) for y in range(image.height) for x in range(image.width)
                        if image.getpixel((x, y))[:3] in {(255, 157, 142), (218, 188, 183)}]
                assert len(text) > 20, 'Failure diagnostic is not visible'
                bounds = (124, 64, 196, 138) if name == 'candidate-small' else (14, 95, 306, 172)
                assert all(bounds[0] <= x < bounds[2] and bounds[1] <= y < bounds[3] for x, y in text), 'Diagnostic text escaped its panel'
        expected = (26, 204, 51) if recover or name == 'candidate-recover' else (204, 51, 26)
        assert all(abs(a-b) <= 1 for a, b in zip(actual, expected)), (name, actual, expected)
        report['cases'].append({'name': name, 'command': command, 'pixel': actual,
                                'checks': 'passed', 'visual_review': 'pending'})
        (output/'manifest.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(f'{name}: {actual}, program revision {current["compiled_revision"]}', flush=True)


if __name__ == '__main__':
    main()
