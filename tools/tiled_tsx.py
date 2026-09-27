"""Read the supported external Tiled TSX subset into JSON tileset fields."""

import math
import xml.etree.ElementTree as ET


def load(content, source):
    def fail(field, message):
        raise ValueError(f"{source}:{field}: {message}")

    if len(content) > 4 * 1024 * 1024 or b"<!DOCTYPE" in content.upper() or b"<!ENTITY" in content.upper():
        fail("xml", "TSX exceeds 4 MiB or contains a DTD/entity declaration")
    try:
        root = ET.fromstring(content)
    except ET.ParseError as error:
        fail("xml", str(error))
    if root.tag != "tileset":
        fail("tileset", "expected a TSX tileset root")

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

    def properties(node, field):
        group = node.find("properties")
        if group is None:
            return []
        result = []
        names = set()
        for prop in group:
            if prop.tag != "property" or not prop.get("name") or prop.find("properties") is not None:
                fail(field + ".properties", "expected simple named properties")
            if prop.get("name") in names:
                fail(field + ".properties." + prop.get("name"), "duplicate name")
            names.add(prop.get("name"))
            kind = prop.get("type", "string")
            raw = prop.get("value", prop.text or "")
            if kind in ("string", "color", "file"):
                converted = raw
            elif kind == "bool":
                if raw not in ("true", "false"):
                    fail(field + ".properties." + prop.get("name"), "invalid boolean")
                converted = raw == "true"
            elif kind == "int":
                converted = value(prop, "value", integer=True, low=-2147483648, high=2147483647)
            elif kind == "object":
                converted = value(prop, "value", integer=True, low=0, high=0xffffffff)
            elif kind == "float":
                converted = value(prop, "value")
            else:
                fail(field + ".properties." + prop.get("name"), "unsupported property type")
            result.append({"name": prop.get("name"), "type": kind, "value": converted})
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
            item["properties"] = properties(obj, field + f".object[{item['id']}]")
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
              "tiles": [], "properties": properties(root, "tileset")}
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
        entry = {"id": number, "properties": properties(tile, field)}
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
