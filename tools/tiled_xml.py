"""Normalize supported Tiled TSX tilesets and TX object templates for offline maps."""

import math
import xml.etree.ElementTree as ET


def document(content, source, kind):
    if len(content) > 4 * 1024 * 1024 or b"<!DOCTYPE" in content.upper() or b"<!ENTITY" in content.upper():
        raise ValueError(f"{source}:xml: XML exceeds 4 MiB or contains a DTD/entity declaration")
    try:
        root = ET.fromstring(content)
    except ET.ParseError as error:
        raise ValueError(f"{source}:xml: {error}") from error
    if root.tag != kind:
        raise ValueError(f"{source}:{kind}: expected a {kind} root")
    return root


def properties(node, source, field):
    groups = node.findall("properties")
    if len(groups) > 1:
        raise ValueError(f"{source}:{field}.properties: duplicate property group")
    if not groups:
        return []
    group = groups[0]
    result = []
    names = set()
    for prop in group:
        name = prop.get("name")
        place = f"{source}:{field}.properties.{name or '?'}"
        if prop.tag != "property" or not name or prop.find("properties") is not None:
            raise ValueError(f"{place}: expected a simple named property")
        if name in names:
            raise ValueError(f"{place}: duplicate name")
        names.add(name)
        kind = prop.get("type", "string")
        raw = prop.get("value", prop.text or "")
        if kind in ("string", "color", "file"):
            converted = raw
        elif kind == "bool":
            if raw not in ("true", "false"):
                raise ValueError(f"{place}: invalid boolean")
            converted = raw == "true"
        elif kind in ("int", "object", "float"):
            try:
                converted = float(raw) if kind == "float" else int(raw, 10)
            except ValueError as error:
                raise ValueError(f"{place}: invalid number") from error
            low, high = (0, 0xffffffff) if kind == "object" else (-2147483648, 2147483647) if kind == "int" else (-1e6, 1e6)
            if not math.isfinite(converted) or not low <= converted <= high:
                raise ValueError(f"{place}: outside {low}..{high}")
        else:
            raise ValueError(f"{place}: unsupported property type {kind}")
        result.append({"name": name, "type": kind, "value": converted})
    return result


def load_tileset(content, source):
    def fail(field, message):
        raise ValueError(f"{source}:{field}: {message}")

    root = document(content, source, "tileset")

    def value(node, field, default=None, integer=False, low=-1e6, high=1e6):
        raw = node.get(field)
        if raw is None:
            if default is None:
                fail(f"{node.tag}.{field}", "required attribute is missing")
            return default
        try:
            parsed = int(raw, 10) if integer else float(raw)
        except (TypeError, ValueError):
            fail(f"{node.tag}.{field}", "invalid number")
        if not math.isfinite(parsed) or not low <= parsed <= high:
            fail(f"{node.tag}.{field}", f"outside {low}..{high}")
        return parsed

    def image(node, field):
        if node.get("trans") is not None or node.find("data") is not None:
            fail(field, "embedded images and color keys are unsupported; use PNG alpha")
        path = node.get("source")
        if not path:
            fail(field + ".source", "external image path is required")
        result = {"image": path}
        for key, target in (("width", "imagewidth"), ("height", "imageheight")):
            if key in node.attrib:
                result[target] = value(node, key, integer=True, low=1, high=8192)
        return result

    def objects(node, field):
        result = {"objects": [], "offsetx": value(node, "offsetx", 0), "offsety": value(node, "offsety", 0)}
        for obj in node:
            if obj.tag != "object":
                continue
            item = {"id": value(obj, "id", integer=True, low=1, high=0xffffffff)}
            for key in ("x", "y", "width", "height", "rotation"):
                item[key] = value(obj, key, 0)
            for shape in ("ellipse", "point", "polyline", "text"):
                if obj.find(shape) is not None:
                    item[shape] = True
            polygon = obj.find("polygon")
            if polygon is not None:
                points = []
                for pair in polygon.get("points", "").split():
                    try:
                        x, y = map(float, pair.split(","))
                    except ValueError:
                        fail(field + f".object[{item['id']}].polygon", "invalid point")
                    if not math.isfinite(x) or not math.isfinite(y):
                        fail(field + f".object[{item['id']}].polygon", "nonfinite point")
                    points.append({"x": x, "y": y})
                item["polygon"] = points
            item["properties"] = properties(obj, source, field + f".object[{item['id']}]")
            result["objects"].append(item)
        return result

    if root.get("tilerendersize", "tile") != "tile" or root.get("fillmode", "stretch") != "stretch":
        fail("tileset", "grid-size rendering or aspect-fit is unsupported")
    grid = root.find("grid")
    if grid is not None and grid.get("orientation", "orthogonal") != "orthogonal":
        fail("grid.orientation", "only orthogonal tile grids are supported")
    result = {"name": root.get("name", ""), "tilewidth": value(root, "tilewidth", integer=True, low=1, high=4096),
              "tileheight": value(root, "tileheight", integer=True, low=1, high=4096),
              "tilecount": value(root, "tilecount", integer=True, low=1, high=1048576),
              "columns": value(root, "columns", 0, integer=True, low=0, high=8192),
              "margin": value(root, "margin", 0, integer=True, low=0, high=8192),
              "spacing": value(root, "spacing", 0, integer=True, low=0, high=8192),
              "tiles": [], "properties": properties(root, source, "tileset")}
    for key in ("class", "objectalignment"):
        if key in root.attrib:
            result[key] = root.get(key)
    offset = root.find("tileoffset")
    if offset is not None:
        result["tileoffset"] = {key: value(offset, key, 0) for key in ("x", "y")}
    sheet = root.find("image")
    if sheet is not None:
        result.update(image(sheet, "tileset.image"))
    elif result["columns"] != 0:
        fail("tileset.columns", "image collection requires zero columns")
    ids = set()
    for tile in root.findall("tile"):
        number = value(tile, "id", integer=True, low=0, high=1048575)
        field = f"tile[{number}]"
        if number in ids:
            fail(field, "duplicate tile ID")
        ids.add(number)
        if any(key in tile.attrib for key in ("x", "y", "width", "height")):
            fail(field, "custom tile image subrectangles are unsupported")
        entry = {"id": number, "properties": properties(tile, source, field)}
        for key in ("class", "type"):
            if key in tile.attrib:
                entry[key] = tile.get(key)
        tile_image = tile.find("image")
        if tile_image is not None:
            if sheet is not None:
                fail(field + ".image", "atlas tiles cannot have separate images")
            entry.update(image(tile_image, field + ".image"))
        group = tile.find("objectgroup")
        if group is not None:
            entry["objectgroup"] = objects(group, field + ".objectgroup")
        animation = tile.find("animation")
        if animation is not None:
            entry["animation"] = [{"tileid": value(frame, "tileid", integer=True, low=0, high=1048575),
                                   "duration": value(frame, "duration", integer=True, low=1, high=3600000)}
                                  for frame in animation]
        result["tiles"].append(entry)
    return result


def load_template(content, source):
    """Return the same object-template fields as Tiled's JSON representation."""
    root = document(content, source, "template")
    children = list(root)
    if any(node.tag not in ("tileset", "object") for node in children) or len(root.findall("object")) != 1 or len(root.findall("tileset")) > 1:
        raise ValueError(f"{source}:template: expected one object and at most one external tileset")
    if len(children) == 2 and children[0].tag != "tileset":
        raise ValueError(f"{source}:template: tileset must precede object")

    def number(node, key, low, high, integer=False):
        raw = node.get(key)
        try:
            result = int(raw, 10) if integer else float(raw)
        except (TypeError, ValueError) as error:
            raise ValueError(f"{source}:{node.tag}.{key}: invalid number") from error
        if not math.isfinite(result) or not low <= result <= high:
            raise ValueError(f"{source}:{node.tag}.{key}: outside {low}..{high}")
        return result

    obj = root.find("object")
    if obj.get("template") is not None:
        raise ValueError(f"{source}:object.template: nested templates are unsupported")
    output = {"type": "template", "object": {}}
    if root.find("tileset") is not None:
        set_node = root.find("tileset")
        if not set_node.get("source") or len(set_node) or set_node.get("firstgid") is None:
            raise ValueError(f"{source}:tileset: an external source and firstgid are required")
        output["tileset"] = {"source": set_node.get("source"),
                             "firstgid": number(set_node, "firstgid", 1, 0x0fffffff, True)}
    result = output["object"]
    for key in ("name",):
        if key in obj.attrib:
            result[key] = obj.get(key)
    if "class" in obj.attrib or "type" in obj.attrib:
        result["type"] = obj.get("class", obj.get("type"))
    for key in ("x", "y", "width", "height", "rotation"):
        if key in obj.attrib:
            result[key] = number(obj, key, -1e6, 1e6)
    if "opacity" in obj.attrib:
        result["opacity"] = number(obj, "opacity", 0, 1)
    if "gid" in obj.attrib:
        result["gid"] = number(obj, "gid", 1, 0xffffffff, True)
    if "visible" in obj.attrib:
        if obj.get("visible") not in ("0", "1"):
            raise ValueError(f"{source}:object.visible: expected 0 or 1")
        result["visible"] = obj.get("visible") == "1"
    shapes = [node for node in obj if node.tag in ("ellipse", "point", "polygon", "polyline", "text")]
    if len(shapes) > 1 or any(node.tag not in ("properties", "ellipse", "point", "polygon", "polyline", "text") for node in obj):
        raise ValueError(f"{source}:object: expected at most one supported shape")
    if shapes:
        shape = shapes[0]
        if shape.tag in ("ellipse", "point"):
            result[shape.tag] = True
        elif shape.tag in ("polygon", "polyline"):
            points = []
            for pair in shape.get("points", "").split():
                try:
                    x, y = map(float, pair.split(","))
                except ValueError as error:
                    raise ValueError(f"{source}:object.{shape.tag}: invalid point") from error
                if not all(math.isfinite(value) and abs(value) <= 1e6 for value in (x, y)):
                    raise ValueError(f"{source}:object.{shape.tag}: point outside range")
                points.append({"x": x, "y": y})
            if not points:
                raise ValueError(f"{source}:object.{shape.tag}: empty points")
            result[shape.tag] = points
        else:
            rendered = {"text": shape.text or ""}
            for key in ("fontfamily", "color", "halign", "valign"):
                if key in shape.attrib:
                    rendered[key] = shape.get(key)
            if "pixelsize" in shape.attrib:
                rendered["pixelsize"] = number(shape, "pixelsize", 1, 4096, True)
            for key in ("wrap", "bold", "italic", "underline", "strikeout", "kerning"):
                if key in shape.attrib:
                    if shape.get(key) not in ("0", "1"):
                        raise ValueError(f"{source}:object.text.{key}: expected 0 or 1")
                    rendered[key] = shape.get(key) == "1"
            result["text"] = rendered
    result["properties"] = properties(obj, source, "object")
    return output
