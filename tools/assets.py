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

VERSION = 1
CHUNK = 32


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


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
    triangles.append(vertices)
    return triangles


def tiled(root, source, read):
    path = project_path(root, source)
    data = json.loads(read(path))
    if data.get("orientation") != "orthogonal" or data.get("renderorder", "right-down") != "right-down":
        raise ValueError(f"{source}: only right-down orthogonal maps are supported")
    tw,th = data["tilewidth"],data["tileheight"]
    if type(tw) is not int or type(th) is not int or not 1 <= tw <= 256 or not 1 <= th <= 256:
        raise ValueError(f"{source}: invalid tile size")
    sets=[]
    for item in data.get("tilesets", []):
        item=dict(item)
        directory=path.parent
        if "source" in item:
            external=project_path(root,str((path.parent/item["source"]).relative_to(root)))
            item={**json.loads(read(external)), "firstgid":item["firstgid"]}; directory=external.parent
        if "image" in item:
            image=project_path(root,str((directory/item["image"]).relative_to(root)))
            read(image); item["image"]=image.relative_to(root).as_posix()
        sets.append(item)
    blocks={}; layers=[]; objects=[]
    def walk(items,parent):
        for original in items:
            layer=dict(original)
            name=parent.get("name", "")+layer.get("name",str(layer.get("id",0)))
            inherited={"name":name,"opacity":parent.get("opacity",1)*layer.get("opacity",1),
                       "visible":parent.get("visible",True) and layer.get("visible",True),
                       "offsetx":parent.get("offsetx",0)+layer.get("offsetx",0),
                       "offsety":parent.get("offsety",0)+layer.get("offsety",0),
                       "parallaxx":parent.get("parallaxx",1)*layer.get("parallaxx",1),
                       "parallaxy":parent.get("parallaxy",1)*layer.get("parallaxy",1)}
            kind=layer["type"]
            if kind=="group":
                walk(layer.get("layers",[]),{**inherited,"name":name+"/"}); continue
            index=len(layers)
            layers.append({**inherited,"type":kind,"order":index})
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
                        block=blocks.setdefault((bx,by),{})
                        cells=block.setdefault(str(index),[0]*(CHUNK*CHUNK))
                        cells[(y%CHUNK)*CHUNK+x%CHUNK]=gid
            elif kind=="objectgroup":
                for original_object in layer.get("objects",[]):
                    obj=dict(original_object)
                    if "template" in obj:
                        template=project_path(root,str((path.parent/obj["template"]).relative_to(root)))
                        obj={**json.loads(read(template))["object"],**obj}; obj.pop("template",None)
                    obj["x"]=obj.get("x",0)+inherited["offsetx"]; obj["y"]=obj.get("y",0)+inherited["offsety"]
                    obj["author_id"]=f"{name}:{obj['id']}"
                    if "polygon" in obj:
                        try: obj["triangles"]=triangulate(obj["polygon"])
                        except ValueError as error: raise ValueError(f"{source}:{name}: object {obj['id']} at ({obj['x']},{obj['y']}): {error}") from error
                    objects.append(obj)
            elif kind=="imagelayer":
                image=project_path(root,str((path.parent/layer["image"]).relative_to(root)))
                read(image); layers[-1]["image"]=image.relative_to(root).as_posix()
            else:
                raise ValueError(f"{source}:{name}: unsupported layer type {kind}")
    walk(data.get("layers",[]),{})
    metadata={"format":1,"chunk_size":CHUNK,"tilewidth":tw,"tileheight":th,"tilesets":sets,"layers":layers,"objects":objects,"chunks":[]}
    return metadata,blocks


def build(manifest: Path, output: Path):
    root=manifest.resolve().parent
    dependencies={}
    def read(path):
        content=path.read_bytes(); dependencies[path.relative_to(root).as_posix()]=hashlib.sha256(content).hexdigest(); return content
    spec=json.loads(read(manifest.resolve()))
    if set(spec)-{"atlases","maps","animations"}:
        raise ValueError("unknown asset manifest fields")
    for group in ("atlases","maps","animations"):
        for name in spec.get(group,{}):
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,128}",name): raise ValueError("asset names use letters, digits, underscore or hyphen")
    maps={name:tiled(root,path,read) for name,path in spec.get("maps",{}).items()}
    animations={}
    for name,path in spec.get("animations",{}).items():
        animation_path=project_path(root,path)
        source=json.loads(read(animation_path))
        image=source.get("meta",{}).get("image")
        if image: read(project_path(root,str((animation_path.parent/image).relative_to(root))))
        frames=source["frames"]
        if isinstance(frames,dict): frames=list(frames.values())
        animations[name]={"frames":[{"rect":f["frame"],"duration":f["duration"]/1000} for f in frames],"tags":source.get("meta",{}).get("frameTags",[])}
    for atlas in spec.get("atlases",{}).values():
        for path in atlas["images"].values(): read(project_path(root,path))
    digest=hashlib.sha256(canonical({"version":VERSION,"spec":spec,"inputs":dependencies}).encode()).hexdigest()
    destination=output.resolve()/digest
    if destination.exists():
        return destination
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".assets-",dir=output) as temp:
        stage=Path(temp); index={"format":VERSION,"digest":digest,"inputs":dependencies,"atlases":{},"maps":{},"animations":animations}
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
            for (x,y),layers in sorted(blocks.items()):
                filename=f"{x}_{y}.json"; content=canonical({"x":x,"y":y,"layers":layers})
                (folder/filename).write_text(content,encoding="utf-8")
                metadata["chunks"].append({"x":x,"y":y,"path":filename,"bytes":len(content.encode())})
            (folder/"index.json").write_text(canonical(metadata),encoding="utf-8")
            index["maps"][name]=f"map-{name}/index.json"
        (stage/"index.json").write_text(canonical(index),encoding="utf-8")
        # Atomic publication; no partially built cache entry is visible.
        os.rename(stage,destination)
    return destination


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest",type=Path); parser.add_argument("output",type=Path)
    args=parser.parse_args()
    print(json.dumps({"ok":True,"directory":str(build(args.manifest,args.output))}))


if __name__=="__main__": main()
