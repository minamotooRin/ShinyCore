"""Opt-in hidden native pixel checks; saved screenshots still require visual review."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', action='append', choices=['normals', 'postprocess', 'shadows', 'materials'])
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    selected = args.case or ['normals', 'postprocess', 'shadows', 'materials']
    report = {'engine_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'cases': []}
    manifest = output/'manifest.json'

    def capture(name, project, keys=()):
        replay = output/f'{name}.jsonl'
        replay.write_text('\n'.join(json.dumps(row) for row in [
            {'version': 3}, {'frame': 0, 'keys': list(keys), 'gamepad': {'connected': False}},
            {'frame': 1, 'keys': [], 'gamepad': {'connected': False}}])+'\n', encoding='utf-8')
        command = [str(binary), str(project), '--frames', '3', '--mute', '--capture-hidden',
                   '--capture', str(output/f'{name}.png'), '--replay', str(replay),
                   '--save-dir', str(output/f'{name}-saves')]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=30)
        (output/f'{name}.log').write_text(result.stderr, encoding='utf-8')
        (output/f'{name}.json').write_text(result.stdout, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'{name}: {result.stderr}')
        row = {'name': name, 'command': command, 'visual_review': 'pending'}
        report['cases'].append(row)
        manifest.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        with Image.open(output/f'{name}.png') as source:
            return source.convert('RGB'), json.loads(result.stdout)['watches']

    if 'normals' in selected:
        # White diffuse surfaces isolate the normal direction from albedo/UV colors.
        probes = [
            ('right', (255, 128, 128), (112, 64), '', False, True),
            ('left', (0, 128, 128), (112, 64), '', False, False),
            ('down', (128, 255, 128), (64, 112), '', False, True),
            ('up', (128, 0, 128), (64, 112), '', False, False),
            ('flip', (255, 128, 128), (112, 64), ',{flip_x=true}', False, False),
            ('rotate', (255, 128, 128), (64, 112), '', True, True),
        ]
        for name, normal, light, options, entity, bright in probes:
            project = output/f'probe-{name}'
            project.mkdir()
            Image.new('RGB', (16, 16), 'white').save(project/'white.png')
            Image.new('RGB', (16, 16), normal).save(project/'normal.png')
            (project/'project.lua').write_text('''return {id="native.normals",modules={"normal_maps"},
display={width=384,height=384,scale="integer"},resources={
white={type="image",path="white.png"},normal={type="image",path="normal.png"}}}''', encoding='utf-8')
            spawn = 'sc.spawn{x=32,y=32,w=64,h=64,sprite="white",angle=math.pi/2}' if entity else ''
            draw = '' if entity else f'sc.image("white",32,32,64,64{options})'
            (project/'main.lua').write_text(f'''return {{width=128,height=128,gravity=0,ambient=.1,
init=function() sc.lighting.normal("white","normal"); {spawn} end,
update=function() sc.debug.watch("lighting",sc.lighting.stats()) end,
draw=function()
    {draw}
    sc.lighting.point{{x={light[0]},y={light[1]},radius=256,height=1,shadows=false}}
    sc.rect(0,0,128,8,"#7139AB",true)
end}}''', encoding='utf-8')
            image, watch = capture(f'normal-{name}', project)
            assert image.size == (384, 384)
            value = image.getpixel((192, 192))[0]
            assert (value > 170 if bright else value < 40), (name, value)
            report['cases'][-1]['center_luminance'] = value
            assert image.getpixel((12, 12)) == (113, 57, 171), 'Lighting changed screen UI'
            assert watch['lighting']['status'] == 'ready'
            assert watch['lighting']['normal_target_bytes'] == 128*128*4
            print(f'normal-{name}: center={value}', flush=True)
        capture('normal-example', ROOT/'examples/normal_maps')

    if 'postprocess' in selected:
        off, watch = capture('post-off', ROOT/'examples/postprocess', ['o'])
        assert watch['pipeline']['allocated_color_bytes'] == 0
        combined, watch = capture('post-combined', ROOT/'examples/postprocess')
        assert watch['pipeline']['status'] == 'ready' and len(watch['pipeline']['passes']) == 4
        assert watch['pipeline']['target_count'] == 2
        # UI glyphs are opaque; their transparent surroundings still show processed world pixels.
        text_pixels = [(x, y) for y in range(230, 270) for x in range(480)
                       if off.getpixel((x, y)) in {(177, 196, 223), (127, 154, 184)}]
        assert len(text_pixels) > 100
        assert all(off.getpixel(p) == combined.getpixel(p) for p in text_pixels)
        assert ImageChops.difference(off.crop((0, 50, 480, 220)), combined.crop((0, 50, 480, 220))).getbbox()

    if 'shadows' in selected:
        hard, watch = capture('shadow-hard', ROOT/'examples/geometry_shadows')
        assert watch['lighting']['status'] == 'ready' and watch['lighting']['shadow_lights'] == 1
        soft, watch = capture('shadow-soft', ROOT/'examples/geometry_shadows', ['s'])
        assert watch['lighting']['samples'] == 4
        assert ImageChops.difference(hard.crop((30, 50, 450, 240)), soft.crop((30, 50, 450, 240))).getbbox()

    if 'materials' in selected:
        _, watch = capture('material-example', ROOT/'examples/materials')
        assert watch['material']['status'] == 'ready'

    report['pixel_checks'] = 'passed'
    manifest.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Native pixel checks passed; screenshots await review: {output}')


if __name__ == '__main__':
    main()
