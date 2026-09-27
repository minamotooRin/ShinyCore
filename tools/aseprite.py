"""Validate Aseprite JSON and restore trimmed frames to a native sprite grid."""
import io
import math


def load(data, image_bytes, source):
    def fail(field, message): raise ValueError(f"{source}:{field}: {message}")
    def integer(value, field, low=0, high=8192):
        if type(value) is not int or not low<=value<=high: fail(field,f"requires integer {low}..{high}")
        return value
    def rect(value, field, size_only=False):
        if not isinstance(value,dict): fail(field,"requires an object")
        return {k:integer(value.get(k),field+"."+k,1 if k in ("w","h") else 0)
                for k in (("w","h") if size_only else ("x","y","w","h"))}
    def text(value,field):
        try: valid=isinstance(value,str) and value and "\0" not in value and len(value.encode("utf-8"))<=128
        except UnicodeError: valid=False
        if not valid: fail(field,"requires nonempty UTF-8 text up to 128 bytes")
        return value
    meta=data.get("meta",{})
    if not isinstance(meta,dict): fail("meta","requires an object")
    from PIL import Image
    try:
        with Image.open(io.BytesIO(image_bytes)) as image:
            if image.format!="PNG" or not all(1<=n<=8192 for n in image.size): fail("meta.image","requires PNG dimensions 1..8192")
            sheet=image.convert("RGBA")
    except OSError as error: fail("meta.image",str(error))
    if "size" in meta and tuple(rect(meta["size"],"meta.size",True).values())!=sheet.size:
        fail("meta.size","does not match PNG dimensions")
    raw=data.get("frames")
    if not isinstance(raw,(list,dict)) or not 1<=len(raw)<=65536: fail("frames","requires 1..65536 frames")
    entries=list(raw.items()) if isinstance(raw,dict) else [(None,v) for v in raw]
    frames=[];canvas=None;names=set()
    for i,(key,value) in enumerate(entries):
        field=f"frames[{i}]"
        if not isinstance(value,dict): fail(field,"requires an object")
        name=text(key if key is not None else value.get("filename",str(i)),field+".filename")
        if name in names: fail(field+".filename","duplicate name")
        names.add(name)
        for flag in ("rotated","trimmed"):
            if type(value.get(flag,False)) is not bool: fail(field+"."+flag,"requires boolean")
        # Aseprite exports rotated:false. Other packers are a different input contract.
        if value.get("rotated",False): fail(field+".rotated","rotated external-packer frames are not Aseprite output")
        area=rect(value.get("frame"),field+".frame")
        if area["x"]+area["w"]>sheet.width or area["y"]+area["h"]>sheet.height: fail(field+".frame","outside PNG")
        original=rect(value.get("sourceSize",{"w":area["w"],"h":area["h"]}),field+".sourceSize",True)
        trim=rect(value.get("spriteSourceSize",{"x":0,"y":0,"w":area["w"],"h":area["h"]}),field+".spriteSourceSize")
        if trim["w"]!=area["w"] or trim["h"]!=area["h"] or trim["x"]+trim["w"]>original["w"] or trim["y"]+trim["h"]>original["h"]:
            fail(field+".spriteSourceSize","size or offset does not fit the original canvas")
        if value.get("trimmed",False) and ("sourceSize" not in value or "spriteSourceSize" not in value):
            fail(field,"trimmed frames require sourceSize and spriteSourceSize")
        if not value.get("trimmed",False) and (trim["x"] or trim["y"] or trim["w"]!=original["w"] or trim["h"]!=original["h"]):
            fail(field+".trimmed","untrimmed frame must fill its canvas")
        if canvas is not None and original!=canvas: fail(field+".sourceSize","all frames must share a canvas")
        canvas=original
        duration=integer(value.get("duration"),field+".duration",1,65535)/1000
        frames.append({"name":name,"source":area,"trim":trim,"duration":duration})
    tags=meta.get("frameTags",[])
    if not isinstance(tags,list) or len(tags)>256: fail("meta.frameTags","requires at most 256 tags")
    clips={};normal_tags=[];steps=0
    for i,tag in enumerate(tags or [{"name":"all","from":0,"to":len(frames)-1}]):
        field=f"meta.frameTags[{i}]"
        if not isinstance(tag,dict): fail(field,"requires an object")
        name=text(tag.get("name"),field+".name")
        if name in clips: fail(field+".name","duplicate tag")
        start=integer(tag.get("from"),field+".from",0,len(frames)-1)
        end=integer(tag.get("to"),field+".to",start,len(frames)-1)
        direction=tag.get("direction","forward")
        if direction not in ("forward","reverse","pingpong","pingpong_reverse"): fail(field+".direction","unsupported direction")
        repeat=tag.get("repeat",0)
        if isinstance(repeat,str) and repeat.isascii() and repeat.isdecimal(): repeat=int(repeat)
        repeat=integer(repeat,field+".repeat",0,65535)
        order=list(range(start,end+1))
        if direction in ("reverse","pingpong_reverse"): order.reverse()
        if direction.startswith("pingpong"): order+=order[-2:0:-1]
        count=len(order)*max(1,repeat);steps+=count
        if steps>65536: fail(field,"expanded clips exceed 65536 total frame entries")
        clips[name]={"loop":repeat==0,"frames":[{"frame":f,"duration":frames[f]["duration"]} for f in order*max(1,repeat)]}
        normal_tags.append({"name":name,"from":start,"to":end,"direction":direction,"repeat":repeat})
    w,h=canvas["w"],canvas["h"];n=len(frames)
    columns=min(n,8192//w,max(math.ceil(math.sqrt(n*h/w)),math.ceil(n/(8192//h))))
    rows=math.ceil(n/columns)
    if rows*h>8192: fail("frames","restored sprite grid exceeds 8192 pixels")
    atlas=Image.new("RGBA",(columns*w,rows*h))
    for i,frame in enumerate(frames):
        a,t=frame["source"],frame["trim"]
        atlas.paste(sheet.crop((a["x"],a["y"],a["x"]+a["w"],a["y"]+a["h"])),(i%columns*w+t["x"],i//columns*h+t["y"]))
        frame["rect"]={"x":i%columns*w,"y":i//columns*h,"w":w,"h":h}
    return {"frame_w":w,"frame_h":h,"frames":frames,"tags":normal_tags,"clips":clips},atlas


def lua_catalog(animations):
    # JSON string quoting overlaps Lua except control escapes: use decimal escapes.
    def quote(text):
        return '"'+''.join('\\'+c if c in ('"','\\') else f"\\{ord(c):03d}" if ord(c)<32 else c for c in text)+'"'
    lines=["-- Generated offline by tools/assets.py; plain project-local data.","return {"]
    for name,a in sorted(animations.items()):
        lines.append(f"  [{quote(name)}]={{image={quote(a['image'])},frame_w={a['frame_w']},frame_h={a['frame_h']},clips={{")
        for tag,clip in sorted(a["clips"].items()):
            values=[f"{{frame={f['frame']},duration={f['duration']!r}}}" for f in clip["frames"]]
            values.append("loop="+str(clip["loop"]).lower())
            lines.append(f"    [{quote(tag)}]={{"+",".join(values)+"},")
        lines.append("  }},")
    return "\n".join([*lines,"}",""])
