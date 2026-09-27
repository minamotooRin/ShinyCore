#!/usr/bin/env python3
"""Build deterministic atlases, animation data and bounded Tiled chunk files.

Usage: python tools/assets.py project/assets.build.json output-directory
Inputs are project-relative; output is an immutable content-addressed directory.
"""
from __future__ import annotations

import argparse
import base64
import gzip
import hashlib
import json
import math
import os
import re
from pathlib import Path
import struct
import tempfile
import zlib

VERSION = 16
CHUNK = 32


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def png_size(content, context, maximum=8192):
    if len(content)<24 or content[:8]!=b'\x89PNG\r\n\x1a\n' or content[12:16]!=b'IHDR':
        raise ValueError(f"{context} requires a PNG image")
    width,height=struct.unpack('>II',content[16:24])
    if not 1<=width<=maximum or not 1<=height<=maximum:
        raise ValueError(f"{context} dimensions must be 1..{maximum}")
    return width,height


def project_path(root, relative):
    candidate = (root / relative).resolve()
    if not candidate.is_relative_to(root.resolve()) or not candidate.is_file():
        raise ValueError(f"missing or outside-project resource: {relative}")
    return candidate


def decode_tiles(layer):
    count = layer.get("width", 0) * layer.get("height", 0)
    if type(count) is not int or not 0 <= count <= 16*1024*1024:
        raise ValueError("tile layer exceeds 16 million cells")
    value = layer["data"]
    if isinstance(value, list):
        if any(type(gid) is not int or not 0 <= gid <= 0xffffffff for gid in value):
            raise ValueError("tile data requires unsigned integer GIDs")
        return value
    if layer.get("encoding") != "base64" or not isinstance(value, str):
        raise ValueError("unsupported tile data encoding")
    data = base64.b64decode(value, validate=True)
    compression = layer.get("compression", "")
    if compression in {"zlib", "gzip"}:
        decoder = zlib.decompressobj(15 if compression == "zlib" else 31)
        data = decoder.decompress(data, count*4+1)
        if not decoder.eof or decoder.unused_data or len(data) > count*4:
            raise ValueError("compressed tile data exceeds declared size or contains trailing data")
    elif compression:
        raise ValueError(f"unsupported tile compression: {compression}")
    if len(data) % 4:
        raise ValueError("misaligned tile data")
    return list(struct.unpack(f"<{len(data)//4}I", data))


def triangulate(points):
    """Ear clipping; reject degenerate/self-intersecting polygons explicitly."""
    vertices = [(float(p["x"]), float(p["y"])) for p in points]
    if not 3 <= len(vertices) <= 1024 or any(not math.isfinite(v) for p in vertices for v in p):
        raise ValueError("polygon requires 3..1024 finite points")
    area = sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(vertices, vertices[1:]+vertices[:1]))
    if abs(area) < 1e-8:
        raise ValueError("degenerate polygon")
    if area < 0:
        vertices.reverse()
    def cross(a,b,c):
        return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    edges=list(zip(vertices,vertices[1:]+vertices[:1]))
    for i,(a,b) in enumerate(edges):
        for j in range(i+1,len(edges)):
            if j==i+1 or (i==0 and j==len(edges)-1): continue
            c,d=edges[j]
            if (max(min(a[0],b[0]),min(c[0],d[0]))<=min(max(a[0],b[0]),max(c[0],d[0])) and
                max(min(a[1],b[1]),min(c[1],d[1]))<=min(max(a[1],b[1]),max(c[1],d[1])) and
                cross(a,b,c)*cross(a,b,d)<=0 and cross(c,d,a)*cross(c,d,b)<=0):
                raise ValueError("self-intersecting polygon")
    triangles = []
    while len(vertices) > 3:
        for i,b in enumerate(vertices):
            a,c = vertices[i-1],vertices[(i+1)%len(vertices)]
            if cross(a,b,c) <= 1e-8:
                continue
            others = [p for j,p in enumerate(vertices) if j not in {(i-1)%len(vertices),i,(i+1)%len(vertices)}]
            if any(cross(a,b,p)>=0 and cross(b,c,p)>=0 and cross(c,a,p)>=0 for p in others):
                continue
            triangles.append([a,b,c]); vertices.pop(i); break
        else:
            raise ValueError("cannot decompose polygon; check intersections and duplicate vertices")
    if cross(*vertices)<=1e-8: raise ValueError("degenerate polygon remainder")
    triangles.append(vertices)
    return triangles


def tile_collision(group):
    """Bake object rotation and concave decomposition into tile-local triangles."""
    shapes=[]
    for obj in group.get("objects",[]):
        try:
            if any(key in obj for key in ("polyline","point","gid","text","template")):
                raise ValueError("tile collision requires a rectangle, ellipse or polygon")
            x=obj.get("x",0)+group.get("offsetx",0)
            y=obj.get("y",0)+group.get("offsety",0)
            angle=math.radians(obj.get("rotation",0))
            if not all(math.isfinite(v) and abs(v)<=1e6 for v in (x,y,angle)):
                raise ValueError("invalid collision transform")
            points=obj.get("polygon")
            if points is None:
                w,h=obj.get("width",0),obj.get("height",0)
                if not all(math.isfinite(v) and .16<=v<=4096 for v in (w,h)):
                    raise ValueError("collision dimensions must be 0.16..4096")
                if obj.get("ellipse"):
                    points=[{"x":w*(1+math.cos(i*math.tau/16))/2,"y":h*(1+math.sin(i*math.tau/16))/2} for i in range(16)]
                else:
                    points=[{"x":0,"y":0},{"x":w,"y":0},{"x":w,"y":h},{"x":0,"y":h}]
            c,s=math.cos(angle),math.sin(angle)
            for triangle in triangulate(points):
                vertices=[]
                for px,py in triangle:
                    tx,ty=x+c*px-s*py,y+s*px+c*py
                    if abs(tx)>1e6 or abs(ty)>1e6: raise ValueError("collision vertex outside range")
                    vertices.extend((tx,ty))
                shapes.append(vertices)
            if len(shapes)>16384: raise ValueError("tile collision exceeds 16384 triangles")
        except (ValueError,TypeError,KeyError) as error:
            raise ValueError(f"object {obj.get('id','?')} at ({obj.get('x',0)},{obj.get('y',0)}): {error}") from error
    return shapes


def object_collision(obj):
    """Bake static object-layer collision into native terrain shapes."""
    collision=next((p["value"] for p in obj.get("properties",[]) if p["name"]=="collision"),"empty")
    if collision=="empty": return []
    if collision not in ("solid","one_way"):
        raise ValueError("object collision must be solid, one_way or empty")
    if any(key in obj for key in ("gid","polyline","text")) or any(obj.get(key) for key in ("ellipse","point")):
        raise ValueError("static object collision requires a rectangle or polygon")
    x,y,angle=obj["x"],obj["y"],obj.get("rotation",0)
    if not all(type(v) in (int,float) and math.isfinite(v) and abs(v)<=1e6 for v in (x,y,angle)):
        raise ValueError("object collision transform outside range")
    if "polygon" in obj:
        if collision=="one_way": raise ValueError("one_way object requires an unrotated rectangle")
        polygons=obj["triangles"]
    else:
        w,h=obj.get("width",0),obj.get("height",0)
        if not all(type(v) in (int,float) and math.isfinite(v) and .16<=v<=4096 for v in (w,h)):
            raise ValueError("object collision dimensions must be 0.16..4096")
        if collision=="one_way" and angle%360: raise ValueError("one_way object requires an unrotated rectangle")
        if angle%360==0:
            return [{"x":x,"y":y,"w":w,"h":h,"one_way":collision=="one_way"}]
        polygons=[[(0,0),(w,0),(w,h),(0,h)]]
    radians=math.radians(angle)
    cosine,sine=math.cos(radians),math.sin(radians)
    if abs(cosine-round(cosine))<1e-12: cosine=round(cosine)
    if abs(sine-round(sine))<1e-12: sine=round(sine)
    shapes=[]
    for points in polygons:
        world=[(x+cosine*px-sine*py,y+sine*px+cosine*py) for px,py in points]
        if any(not math.isfinite(v) or abs(v)>1e6 for pair in world for v in pair):
            raise ValueError("object collision vertex outside range")
        left=min(px for px,_ in world); top=min(py for _,py in world)
        width=max(px for px,_ in world)-left; height=max(py for _,py in world)-top
        if not .16<=width<=4096 or not .16<=height<=4096:
            raise ValueError("object collision bounds must be 0.16..4096")
        shapes.append({"x":left,"y":top,"w":width,"h":height,
                       "vertices":[v for px,py in world for v in (px-left,py-top)]})
    return shapes


def tiled(root, source, read):
    path = project_path(root, source)
    data = json.loads(read(path))
    if data.get("orientation") != "orthogonal" or data.get("renderorder", "right-down") != "right-down":
        raise ValueError(f"{source}: only right-down orthogonal maps are supported")
    tw,th = data["tilewidth"],data["tileheight"]
    if type(tw) is not int or type(th) is not int or not 1 <= tw <= 256 or not 1 <= th <= 256:
        raise ValueError(f"{source}: invalid tile size")
    sets=[]; external_sets={}
    def load_xml(kind,content,owner):
        import sys
        sys.path.insert(0,str(Path(__file__).resolve().parent))
        try: import tiled_xml
        finally: sys.path.pop(0)
        return (tiled_xml.load_tileset if kind=="tsx" else tiled_xml.load_template)(content,owner.relative_to(root))
    def relative(owner,reference):
        if not isinstance(reference,str) or not reference:
            raise ValueError(f"{owner.relative_to(root)}: nonempty resource path required")
        return project_path(root,str(owner.parent/reference))
    def load_set(item,owner):
        item=dict(item); directory=owner.parent; external=None
        if "source" in item:
            try: external=relative(owner,item["source"])
            except ValueError as error:
                raise ValueError(f"{owner.relative_to(root)}: tileset.source: {error}") from error
            content=read(external)
            if external.suffix.lower()==".tsx":
                definition=load_xml("tsx",content,external)
            else:
                definition=json.loads(content)
            item={**definition,"firstgid":item["firstgid"]}; directory=external.parent
        context=(external or owner).relative_to(root).as_posix()
        def resource(reference,field):
            if not isinstance(reference,str): raise ValueError(f"{context}:{field}: path must be text")
            try: return project_path(root,str(directory/reference))
            except ValueError as error: raise ValueError(f"{context}:{field}: {error}") from error
        def files(properties):
            for prop in properties:
                if prop.get("type")=="file" and prop.get("value"):
                    dependency=resource(prop["value"],f"property {prop.get('name','?')}")
                    read(dependency); prop["value"]=dependency.relative_to(root).as_posix()
        files(item.get("properties",[]))
        if "image" in item:
            image=resource(item["image"],"image")
            width,height=png_size(read(image),f"{context}: tileset {item.get('name','?')}")
            if ("imagewidth" in item and item["imagewidth"]!=width) or ("imageheight" in item and item["imageheight"]!=height):
                raise ValueError(f"{context}: tileset {item.get('name','?')}: image size disagrees with PNG")
            item.update(image=image.relative_to(root).as_posix(),imagewidth=width,imageheight=height)
        for tile in item.get("tiles",[]):
            files(tile.get("properties",[]))
            if "objectgroup" in tile:
                try: tile["collision_shapes"]=tile_collision(tile["objectgroup"])
                except ValueError as error:
                    raise ValueError(f"{context}: tileset {item.get('name','?')} tile {tile.get('id','?')}: {error}") from error
            if "image" in tile:
                image=resource(tile["image"],f"tile {tile.get('id','?')}.image")
                width,height=png_size(read(image),f"{context}: tileset {item.get('name','?')} tile {tile.get('id','?')}",4096)
                if ("imagewidth" in tile and tile["imagewidth"]!=width) or ("imageheight" in tile and tile["imageheight"]!=height):
                    raise ValueError(f"{context}: tile {tile.get('id','?')}: image size disagrees with PNG")
                tile.update(image=image.relative_to(root).as_posix(),imagewidth=width,imageheight=height)
        sets.append(item)
        if external:
            if external in external_sets: raise ValueError(f"{source}: duplicate external tileset {external.relative_to(root)}")
            external_sets[external]=item
        return item
    for item in data.get("tilesets",[]): load_set(item,path)
    def extent(item):
        count=item.get("tilecount",0)
        ids=[tile.get("id",-1) for tile in item.get("tiles",[])]
        if type(count) is not int or count<0 or any(type(i) is not int or i<0 for i in ids):
            raise ValueError("invalid template tileset tile IDs/count")
        return max([count,*(i+1 for i in ids)])
    def template_gid(gid,definition,owner):
        if type(gid) is not int or not 1<=gid<=0xffffffff or gid&0x10000000:
            raise ValueError("invalid orthogonal template object gid")
        if not isinstance(definition,dict) or "source" not in definition:
            raise ValueError("tile template requires an external tileset")
        first=definition.get("firstgid")
        if type(first) is not int or not 1<=first<=0x0fffffff or (gid&0x0fffffff)<first:
            raise ValueError("invalid template tileset firstgid")
        external=relative(owner,definition["source"])
        item=external_sets.get(external)
        if item is None:
            next_gid=max([1,*(entry["firstgid"]+extent(entry) for entry in sets)])
            item=load_set({"source":str(external),"firstgid":next_gid},owner)
        local=(gid&0x0fffffff)-first
        if ("image" in item and local>=item.get("tilecount",0)) or (
                "image" not in item and not any(tile.get("id")==local for tile in item.get("tiles",[]))):
            raise ValueError("template object gid does not name a tileset tile")
        mapped=item["firstgid"]+local
        if not 1<=mapped<=0x0fffffff or item["firstgid"]+extent(item)-1>0x0fffffff:
            raise ValueError("template tileset exceeds supported GID range")
        return (gid&0xe0000000)|mapped
    def properties(values,owner):
        if not isinstance(values,list): raise ValueError("properties must be an array")
        result={}
        for value in values:
            if not isinstance(value,dict) or not isinstance(value.get("name"),str) or not value["name"]:
                raise ValueError("property requires a nonempty name")
            prop=dict(value);name=prop["name"]
            if name in result: raise ValueError(f"duplicate property {name}")
            result[name]=(prop,owner)
        return result
    def resolved_properties(values):
        resolved=[]
        for name,(prop,owner) in sorted(values.items()):
            if prop.get("type")=="file":
                reference=prop.get("value")
                if not isinstance(reference,str): raise ValueError(f"property {name}: file value must be a string")
                if reference:
                    dependency=relative(owner,reference);read(dependency)
                    prop["value"]=dependency.relative_to(root).as_posix()
            resolved.append(prop)
        return resolved
    def object_data(instance):
        base={};defaults={}
        if "template" in instance:
            owner=relative(path,instance["template"])
            content=read(owner)
            template=load_xml("tx",content,owner) if owner.suffix.lower()==".tx" else json.loads(content)
            if not isinstance(template,dict) or template.get("type")!="template" or not isinstance(template.get("object"),dict):
                raise ValueError(f"{owner.relative_to(root)}: expected an object template")
            base=dict(template["object"])
            if "template" in base: raise ValueError(f"{owner.relative_to(root)}: nested templates are unsupported")
            defaults=properties(base.get("properties",[]),owner)
            if "gid" in base and "gid" not in instance:
                base["gid"]=template_gid(base["gid"],template.get("tileset"),owner)
        defaults.update(properties(instance.get("properties",[]),path))
        obj={**base,**instance,"id":instance.get("id")}
        obj.pop("template",None)
        resolved=resolved_properties(defaults)
        if resolved or "properties" in obj: obj["properties"]=resolved
        return obj
    alignments={"unspecified":(0,1),"topleft":(0,0),"top":(.5,0),"topright":(1,0),
                "left":(0,.5),"center":(.5,.5),"right":(1,.5),
                "bottomleft":(0,1),"bottom":(.5,1),"bottomright":(1,1)}
    for item in sets:
        if item.get("objectalignment","unspecified") not in alignments:
            raise ValueError(f"{source}: tileset {item.get('name','?')}: unsupported objectalignment")
    def tile_set(gid,context):
        if type(gid) is not int or not 1<=gid<=0xffffffff or gid&0x10000000:
            raise ValueError(f"{context}: invalid orthogonal object GID")
        raw=gid&0x0fffffff
        for item in reversed(sorted(sets,key=lambda value:value["firstgid"])):
            if raw>=item["firstgid"]:
                local=raw-item["firstgid"]
                if local>=extent(item) or ("image" not in item and not any(tile.get("id")==local for tile in item.get("tiles",[]))):
                    break
                return item,local
        raise ValueError(f"{context}: GID has no tileset image")
    blocks={}; layers=[]; groups=[]; identities=set(); coverage={}
    def block_at(x,y):
        return blocks.setdefault((x,y),{"layers":{},"objects":[]})
    def cover_object(obj,anchor,context):
        width,height=obj.get("width",0),obj.get("height",0)
        angle=obj.get("rotation",0)
        if (any(type(n) not in (int,float) or not math.isfinite(n) for n in (width,height,angle))
                or width<0 or height<0 or abs(angle)>1e6):
            raise ValueError(f"{context}: invalid object dimensions/rotation")
        if "polygon" in obj or "polyline" in obj:
            points=obj.get("polygon",obj.get("polyline"))
            if not isinstance(points,list) or not 1<=len(points)<=1024:
                raise ValueError(f"{context}: object points require 1..1024 entries")
            try: local=[(p["x"],p["y"]) for p in points]
            except (TypeError,KeyError) as error: raise ValueError(f"{context}: object points require x,y") from error
            if any(type(n) not in (int,float) or not math.isfinite(n) for pair in local for n in pair):
                raise ValueError(f"{context}: object points must be finite")
        else:
            if "gid" in obj:
                item,_=tile_set(obj["gid"],context)
                ax,ay=alignments[item.get("objectalignment","unspecified")]
                offset=item.get("tileoffset",{})
                left=offset.get("x",0)-ax*width; top=offset.get("y",0)-ay*height
            else: left=top=0
            local=[(left,top),(left+width,top),(left,top+height),(left+width,top+height)]
        cosine,sine=math.cos(math.radians(angle)),math.sin(math.radians(angle))
        xs=[obj["x"]+x*cosine-y*sine for x,y in local]
        ys=[obj["y"]+x*sine+y*cosine for x,y in local]
        if any(not math.isfinite(n) for n in (*xs,*ys)):
            raise ValueError(f"{context}: transformed object footprint is not finite")
        left,right=math.floor(min(xs)/(tw*CHUNK)),math.floor(max(xs)/(tw*CHUNK))
        top,bottom=math.floor(min(ys)/(th*CHUNK)),math.floor(max(ys)/(th*CHUNK))
        if any(not -31250<=n<=31250 for n in (left,right,top,bottom)) or (right-left+1)*(bottom-top+1)>1024:
            raise ValueError(f"{context}: object footprint exceeds streamed chunk limits")
        for x in range(left,right+1):
            for y in range(top,bottom+1):
                if (x,y)!=anchor: coverage.setdefault((x,y),set()).add(anchor)
    def walk(items,parent):
        for original in items:
            layer=dict(original)
            name=parent.get("name", "")+layer.get("name",str(layer.get("id",0)))
            context=f"{source}:{name}"
            def numeric(field,default,low,high):
                value=layer.get(field,default)
                if type(value) not in (int,float) or not math.isfinite(value) or not low<=value<=high:
                    raise ValueError(f"{context}: invalid {field}")
                return value
            if type(layer.get("visible",True)) is not bool: raise ValueError(f"{context}: visible must be boolean")
            if layer.get("mode","normal")!="normal": raise ValueError(f"{context}: unsupported layer blend mode")
            if "transparentcolor" in layer: raise ValueError(f"{context}: transparentcolor requires a PNG alpha channel instead")
            color=layer.get("tintcolor","#FFFFFFFF")
            if not isinstance(color,str) or not re.fullmatch(r"#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})",color):
                raise ValueError(f"{context}: tintcolor requires #RRGGBB or #AARRGGBB")
            if len(color)==7: color="#FF"+color[1:]
            tint=tuple(a*int(color[1+i*2:3+i*2],16)/255 for i,a in enumerate(parent.get("_tint",(1,1,1,1))))
            inherited={"name":name,"opacity":parent.get("opacity",1)*numeric("opacity",1,0,1),
                       "visible":parent.get("visible",True) and layer.get("visible",True),
                       "offsetx":parent.get("offsetx",0)+numeric("offsetx",0,-1e6,1e6),
                       "offsety":parent.get("offsety",0)+numeric("offsety",0,-1e6,1e6),
                       "parallaxx":parent.get("parallaxx",1)*numeric("parallaxx",1,-100,100),
                       "parallaxy":parent.get("parallaxy",1)*numeric("parallaxy",1,-100,100)}
            for field,limit in (("offsetx",1e6),("offsety",1e6),("parallaxx",100),("parallaxy",100)):
                if abs(inherited[field])>limit: raise ValueError(f"{context}: inherited {field} outside supported range")
            kind=layer["type"]
            if kind=="group":
                try: group_properties=resolved_properties(properties(layer.get("properties",[]),path))
                except (ValueError,KeyError,TypeError) as error: raise ValueError(f"{context}: {error}") from error
                group={"name":name,"properties":group_properties}
                if "id" in layer: group["id"]=layer["id"]
                if parent.get("_group"): group["parent_group"]=parent["_group"]
                groups.append(group)
                walk(layer.get("layers",[]),{**inherited,"name":name+"/","_tint":tint,"_group":len(groups)})
                continue
            index=len(layers)
            layers.append({**inherited,"type":kind,"order":index,
                           "tintcolor":"#"+"".join(f"{math.floor(channel*255+.5):02X}" for channel in tint)})
            if kind=="objectgroup":
                draworder=layer.get("draworder","topdown")
                if draworder not in ("topdown","index"): raise ValueError(f"{context}: unsupported draworder")
                layers[-1]["draworder"]=draworder
            if parent.get("_group"): layers[-1]["group"]=parent["_group"]
            try: layers[-1]["properties"]=resolved_properties(properties(layer.get("properties",[]),path))
            except (ValueError,KeyError,TypeError) as error: raise ValueError(f"{context}: {error}") from error
            if kind=="tilelayer":
                pieces=layer.get("chunks") or [{"x":0,"y":0,"width":layer["width"],"height":layer["height"],"data":layer["data"]}]
                for piece in pieces:
                    if any(type(piece[key]) is not int for key in ("x","y","width","height")) or piece["width"]<=0 or piece["height"]<=0:
                        raise ValueError(f"{source}:{name}: invalid chunk dimensions")
                    if any(abs(piece[key])>1000000 for key in ("x","y")) or abs(piece["x"]+piece["width"])>1000000 or abs(piece["y"]+piece["height"])>1000000:
                        raise ValueError(f"{source}:{name}: chunk outside supported coordinates")
                    values=decode_tiles({**layer,**piece})
                    if len(values)!=piece["width"]*piece["height"]:
                        raise ValueError(f"{source}:{name}: tile count mismatch")
                    for at,gid in enumerate(values):
                        if not gid: continue
                        x=piece["x"]+at%piece["width"]; y=piece["y"]+at//piece["width"]
                        bx,by=x//CHUNK,y//CHUNK
                        cells=block_at(bx,by)["layers"].setdefault(str(index),[0]*(CHUNK*CHUNK))
                        cells[(y%CHUNK)*CHUNK+x%CHUNK]=gid
            elif kind=="objectgroup":
                for draw_order,original_object in enumerate(layer.get("objects",[])):
                    context=f"{source}:{name}: object {original_object.get('id', '?')}"
                    try: obj=object_data(original_object)
                    except (ValueError,KeyError,TypeError,OSError) as error:
                        raise ValueError(f"{context}: {error}") from error
                    if type(obj.get("id")) is not int or not 1<=obj["id"]<=0xffffffff:
                        raise ValueError(f"{context}: id must be a positive uint32")
                    for axis,size in (("x",tw),("y",th)):
                        position,offset=obj.get(axis,0),inherited["offset"+axis]
                        if any(type(v) not in (int,float) or not math.isfinite(v) for v in (position,offset)):
                            raise ValueError(f"{context}: {axis} must be finite")
                        obj[axis]=position+offset
                        if not math.isfinite(obj[axis]) or abs(obj[axis]/size)>1000000:
                            raise ValueError(f"{context}: {axis} outside supported coordinates")
                    identity=f"{name}:{obj['id']}"
                    if (not re.fullmatch(r"[A-Za-z0-9_.:/-]{1,127}",identity)
                            or any(part in ("", ".", "..") for part in identity.split("/"))):
                        raise ValueError(f"{context}: persistent_id requires a valid ASCII layer path")
                    if identity in identities: raise ValueError(f"{context}: duplicate persistent_id {identity}")
                    identities.add(identity)
                    obj["persistent_id"]=identity; obj["layer"]=index; obj["draw_order"]=draw_order
                    if "gid" in obj:
                        item,local=tile_set(obj["gid"],context)
                        tile=next((entry for entry in item.get("tiles",[]) if entry.get("id")==local),{})
                        for field,fallback in (("width",tile.get("imagewidth",item["tilewidth"])),
                                               ("height",tile.get("imageheight",item["tileheight"]))):
                            if not obj.get(field): obj[field]=fallback
                    if "collision_shapes" in obj: raise ValueError(f"{context}: collision_shapes is generated")
                    if "polygon" in obj:
                        try: obj["triangles"]=triangulate(obj["polygon"])
                        except ValueError as error: raise ValueError(f"{source}:{name}: object {obj['id']} at ({obj['x']},{obj['y']}): {error}") from error
                    try: collision_shapes=object_collision(obj)
                    except (ValueError,TypeError,KeyError) as error:
                        raise ValueError(f"{context} at ({obj['x']},{obj['y']}): {error}") from error
                    if collision_shapes: obj["collision_shapes"]=collision_shapes
                    anchor=(math.floor(obj["x"]/(tw*CHUNK)),math.floor(obj["y"]/(th*CHUNK)))
                    cover_object(obj,anchor,context)
                    block=block_at(*anchor)
                    if len(block["objects"])>=4096: raise ValueError(f"{context}: chunk exceeds 4096 objects")
                    block["objects"].append(obj)
            elif kind=="imagelayer":
                image=project_path(root,str((path.parent/layer["image"]).relative_to(root)))
                content=read(image)
                width,height=png_size(content,f"{source}:{name}: image layer")
                layers[-1].update(image=image.relative_to(root).as_posix(),imagewidth=width,imageheight=height)
                for field in ("repeatx","repeaty"):
                    value=layer.get(field,False)
                    if type(value) is not bool: raise ValueError(f"{source}:{name}: {field} must be boolean")
                    layers[-1][field]=value
            else:
                raise ValueError(f"{source}:{name}: unsupported layer type {kind}")
    walk(data.get("layers",[]),{})
    if len(layers)>1000 or len(blocks)>65536 or len(coverage)>65536:
        raise ValueError(f"{source}: stream layer or chunk capacity exceeded")
    try: map_properties=resolved_properties(properties(data.get("properties",[]),path))
    except (ValueError,KeyError,TypeError) as error: raise ValueError(f"{source}: map properties: {error}") from error
    metadata={"format":3,"chunk_size":CHUNK,"tilewidth":tw,"tileheight":th,"tilesets":sets,
              "layers":layers,"groups":groups,"properties":map_properties,"chunks":[],
              "object_coverage":[{"x":x,"y":y,"anchors":[{"x":ax,"y":ay} for ax,ay in sorted(anchors)]}
                                 for (x,y),anchors in sorted(coverage.items())]}
    for field in ("parallaxoriginx","parallaxoriginy"):
        value=data.get(field,0)
        if type(value) not in (int,float) or not math.isfinite(value) or abs(value)>1000000:
            raise ValueError(f"{source}: invalid {field}")
        metadata[field]=value
    return metadata,blocks


def build(manifest: Path, output: Path):
    root=manifest.resolve().parent
    dependencies={}
    def read(path):
        content=path.read_bytes(); dependencies[path.relative_to(root).as_posix()]=hashlib.sha256(content).hexdigest(); return content
    spec=json.loads(read(manifest.resolve()))
    if not isinstance(spec,dict) or set(spec)-{"atlases","maps","animations"}:
        raise ValueError("unknown asset manifest fields")
    for group in ("atlases","maps","animations"):
        if not isinstance(spec.get(group,{}),dict): raise ValueError(f"{manifest.name}:{group}: requires an object")
        for name in spec.get(group,{}):
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,128}",name): raise ValueError("asset names use letters, digits, underscore or hyphen")
    maps={name:tiled(root,path,read) for name,path in spec.get("maps",{}).items()}
    animations={};animation_images={}
    if spec.get("animations"):
        # Resolve sibling tools even when assets.py is loaded through importlib by an Agent.
        import sys
        sys.path.insert(0,str(Path(__file__).resolve().parent))
        try: import aseprite
        finally: sys.path.pop(0)
    for name,path in spec.get("animations",{}).items():
        if not isinstance(path,str): raise ValueError(f"{manifest.name}:animations.{name}: requires a JSON path")
        animation_path=project_path(root,path)
        def unique_object(pairs):
            obj={}
            for key,value in pairs:
                if key in obj: raise ValueError(f"{path}: duplicate JSON key {key!r}")
                obj[key]=value
            return obj
        try: source=json.loads(read(animation_path),object_pairs_hook=unique_object)
        except json.JSONDecodeError as error: raise ValueError(f"{path}:{error.lineno}:{error.colno}: {error.msg}") from error
        if not isinstance(source,dict) or not isinstance(source.get("meta"),dict):
            raise ValueError(f"{path}:meta: requires an object")
        image=source["meta"].get("image")
        if not isinstance(image,str) or not image or "\\" in image:
            raise ValueError(f"{path}:meta.image: requires a relative PNG path")
        try: image_path=project_path(root,str((animation_path.parent/image).relative_to(root)))
        except (ValueError,OSError) as error: raise ValueError(f"{path}:meta.image: {error}") from error
        png=read(image_path);png_size(png,f"{path}:meta.image")
        animations[name],animation_images[name]=aseprite.load(source,png,path)
        animations[name]["image"]=f"animation-{name}.png"
        animations[name]["module"]="animations.lua"
    for atlas in spec.get("atlases",{}).values():
        for path in atlas["images"].values(): read(project_path(root,path))
    digest=hashlib.sha256(canonical({"version":VERSION,"spec":spec,"inputs":dependencies}).encode()).hexdigest()
    destination=output.resolve()/digest
    if destination.exists():
        return destination
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".assets-",dir=output) as temp:
        stage=Path(temp); index={"format":VERSION,"digest":digest,"inputs":dependencies,"atlases":{},"maps":{},"animations":animations}
        for name,image in sorted(animation_images.items()):
            image.save(stage/animations[name]["image"])
        if animations:
            (stage/"animations.lua").write_text(aseprite.lua_catalog(animations),encoding="utf-8")
        for name,atlas in sorted(spec.get("atlases",{}).items()):
            from PIL import Image
            width=atlas.get("width",2048)
            if type(width) is not int or not 16<=width<=8192: raise ValueError("atlas width must be 16..8192")
            images=[]
            for key,path in atlas["images"].items():
                with Image.open(project_path(root,path)) as image: images.append((key,image.convert("RGBA")))
            images.sort(key=lambda p:(-p[1].height,p[0]))
            x=y=row=0; placements={}
            for key,image in images:
                if image.width+2>width: raise ValueError(f"atlas image too wide: {key}")
                if x+image.width+2>width: x=0; y+=row; row=0
                placements[key]={"x":x+1,"y":y+1,"w":image.width,"h":image.height}
                x+=image.width+2; row=max(row,image.height+2)
            height=max(1,y+row)
            if height>8192: raise ValueError("atlas exceeds 8192 pixels high")
            canvas=Image.new("RGBA",(width,height))
            for key,image in images:
                rect=placements[key]; canvas.paste(image,(rect["x"],rect["y"]))
            filename=f"atlas-{name}.png"; canvas.save(stage/filename)
            index["atlases"][name]={"image":filename,"regions":placements}
        for name,(metadata,blocks) in sorted(maps.items()):
            folder=stage/f"map-{name}"; folder.mkdir()
            for (x,y),block in sorted(blocks.items()):
                if len(block["layers"])>64: raise ValueError(f"map {name}: chunk ({x},{y}) exceeds 64 tile layers")
                filename=f"{x}_{y}.json"; content=canonical({"x":x,"y":y,**block})
                if len(content.encode())>2*1024*1024: raise ValueError(f"map {name}: chunk ({x},{y}) exceeds 2 MiB")
                (folder/filename).write_text(content,encoding="utf-8")
                metadata["chunks"].append({"x":x,"y":y,"path":filename,"bytes":len(content.encode())})
            index_content=canonical(metadata)
            if len(index_content.encode())>16*1024*1024: raise ValueError(f"map {name}: stream index exceeds 16 MiB")
            (folder/"index.json").write_text(index_content,encoding="utf-8")
            index["maps"][name]=f"map-{name}/index.json"
        (stage/"index.json").write_text(canonical(index),encoding="utf-8")
        # Atomic publication; no partially built cache entry is visible.
        os.rename(stage,destination)
    return destination


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest",type=Path); parser.add_argument("output",type=Path)
    args=parser.parse_args()
    try:
        directory=build(args.manifest,args.output)
    except (ValueError,OSError) as error:
        parser.exit(1,json.dumps({"ok":False,"code":"asset_build","error":str(error)})+"\n")
    print(json.dumps({"ok":True,"directory":str(directory)}))


if __name__=="__main__": main()
