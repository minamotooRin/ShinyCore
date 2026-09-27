"""Verify baked portals and world-coordinate coarse routes in a real Lua VM."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve()


def module(name):
    spec = importlib.util.spec_from_file_location(name, root / f'tools/{name}.py')
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


bake = module('nav_bake')
graph = module('nav_graph')
with tempfile.TemporaryDirectory(prefix='shiny-nav-route-') as temp:
    project = Path(temp)
    masks = project / 'masks.json'
    bake.bake(binary, root / 'examples/wayfarer/maps/world', masks, None, 8, 0)
    compiled = graph.build(masks, project / 'wayfarer.lua')
    graph.build(masks, project / 'wayfarer-copy.lua')
    assert (project / 'wayfarer.lua').read_bytes() == (project / 'wayfarer-copy.lua').read_bytes()
    assert compiled['node_count'] == 16 and len(compiled['edges']) == 24
    assert compiled['edges'][0] == (1, 2, 31, 16, 32, 16)

    split = {'format': 1, 'chunk_size': 32, 'tilewidth': 1, 'tileheight': 1,
             'cell_size': 8, 'radius': 0, 'bounds': [-1, 0, 0, 0],
             'source_sha256': '0'*64, 'bake_sha256': '1'*64,
             'chunks': [{'x': -1, 'y': 0, 'rows': ['....', '####', '....', '####']},
                        {'x': 0, 'y': 0, 'rows': ['....', '####', '####', '####']}]}
    (project / 'split.json').write_text(json.dumps(split), encoding='utf-8')
    separated = graph.build(project / 'split.json', project / 'split.lua')
    assert separated['node_count'] == 3 and len(separated['edges']) == 1
    shutil.copytree(root / 'lua/shiny', project / 'lib/shiny')
    (project / 'main.lua').write_text('''local Route=require('shiny.stream_route')
local World=require('shiny.stream_world')
return {init=function()
  local forest=Route.new(require('wayfarer'))
  local path=Route.route(forest,12,12,780,780)
  assert(path.status=='ok' and #path.points>4)
  for i=2,#path.points-2,2 do
    local a,b=path.points[i],path.points[i+1]
    assert(math.abs(a.x-b.x)+math.abs(a.y-b.y)==8)
  end
  assert(Route.route(forest,0,0,780,780).status=='unreachable')
  assert(Route.route(forest,12,12,2000,2000).status=='unloaded')
  assert(Route.route(forest,12,12,780,780,1).status=='budget_exhausted')
  local split=Route.new(require('split'))
  local crossing=Route.route(split,-28,4,4,4)
  assert(crossing.status=='ok' and #crossing.points==4)
  assert(crossing.points[2].x==-4 and crossing.points[3].x==4)
  assert(Route.route(split,-28,20,4,4).status=='unreachable')
  sc.navigation.region(-32,0,{'........','########','........','########'},8)
  local mask=sc.navigation.mask()
  assert(Route.refresh(split,mask,{{x=-1,y=0},{x=0,y=0}})==1)
  assert(split.revision==1 and Route.route(split,-28,20,4,20).status=='ok')
  assert(Route.route(split,-28,20,4,20).revision==1)
  local bad=sc.navigation.mask(); bad.radius=1
  assert(not pcall(Route.refresh,split,bad,{{x=0,y=0}}))
  assert(not pcall(Route.refresh,split,mask,{{x=0,y=0},{x=0,y=0}}))
  assert(not pcall(Route.refresh,split,mask,{[1]={x=0,y=0},[3]={x=-1,y=0}}))
  local incomplete={x=-32,y=0,cell_size=8,radius=0,width=4,height=4,
      rows={'####','####','....','####'}}
  assert(not pcall(Route.refresh,split,incomplete,{{x=-1,y=0},{x=0,y=0}}))
  assert(split.revision==1 and Route.route(split,-28,20,4,20).status=='ok')
  sc.navigation.region(-32,0,{'....####','########','........','########'},8)
  local world={navigation=true,nav_region={cell_size=8},region={width=32,height=32},
      chunks={['0:0']={x=0,y=0},['-2:0']={x=-2,y=0}}}
  assert(World.refresh_route(world,split)==1 and split.revision==2)
  assert(Route.route(split,-28,4,4,4).status=='unreachable')
  assert(Route.route(split,-28,20,4,20).status=='ok')
  world.region.pending={}
  assert(not pcall(World.refresh_route,world,split) and split.revision==2)
  world.region.pending=nil
  sc.navigation.region(-32,0,{'........','########','....####','########'},8)
  assert(World.refresh_route(world,split)==1 and split.revision==3)
  assert(Route.route(split,-28,4,4,4).status=='ok')
  assert(Route.route(split,-28,20,4,20).status=='unreachable')
  local linear=Route.new{format=1,cell_size=8,chunk_width=32,chunk_height=32,
      cells_x=4,cells_y=4,radius=0,bounds={0,0,2,0},node_count=3,
      chunks={['0:0']={full=true,component=1},['1:0']={full=true,component=2},
          ['2:0']={full=true,component=3}},
      edges={{a=1,b=2,ax=3,ay=0,bx=4,by=0},{a=2,b=3,ax=7,ay=0,bx=8,by=0}}}
  assert(Route.invalidate(linear,{{x=1,y=0}}) and linear.revision==1)
  local unknown=Route.route(linear,4,4,68,4)
  assert(unknown.status=='unverified' and unknown.pending.x==1 and unknown.pending.y==0)
  assert(not pcall(Route.invalidate,linear,{{x=2,y=0},{x=2,y=0}}) and linear.revision==1)
  sc.navigation.region(0,0,{'............','............','............','............'},8)
  assert(Route.refresh(linear,sc.navigation.mask(),{{x=1,y=0}})==0 and linear.revision==2)
  assert(Route.route(linear,4,4,68,4).status=='ok')
  assert(Route.invalidate(linear,{{x=1,y=0}}) and linear.revision==3)
  assert(Route.route(linear,4,4,68,4).status=='unverified')
  assert(Route.refresh(linear,sc.navigation.mask(),{{x=1,y=0}})==0 and linear.revision==4)
  local middle=Route.new{format=1,cell_size=8,chunk_width=32,chunk_height=32,
      cells_x=4,cells_y=4,radius=0,bounds={0,0,1,0},node_count=2,
      chunks={['0:0']={full=true,component=1},['1:0']={full=true,component=2}},
      edges={{a=1,b=2,ax=3,ay=0,bx=4,by=0}}}
  sc.navigation.region(0,0,{'........','.#......','........','........'},8)
  assert(Route.refresh(middle,sc.navigation.mask(),{{x=0,y=0}})==1)
  assert(Route.route(middle,4,20,44,20).points[2].y==20)
  local wide_data=require('split'); wide_data.radius=6
  local wide=Route.new(wide_data)
  local wide_mask=sc.navigation.mask(6)
  assert(World.refresh_route(world,wide)==0 and wide.revision==0)
  assert(not pcall(Route.refresh,wide,wide_mask,{{x=0,y=0}}))
  sc.debug.watch('route',{status=path.status,portals=#path.points-2,split=crossing.status})
end}
''', encoding='utf-8')
    result = subprocess.run([str(binary), '--headless', str(project), '--frames', '0'],
                            capture_output=True, text=True, encoding='utf-8')
    assert result.returncode == 0, result.stderr + result.stdout
    watches = json.loads(result.stdout)['watches']['route']
    assert watches['status'] == watches['split'] == 'ok' and watches['portals'] >= 2

print('Stream route: baked graph, native-mask refresh, published-world gate and disconnection passed')
