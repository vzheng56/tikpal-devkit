#!/usr/bin/env python3
"""kit check: what an app has to pass before it is submitted, here and in its repository's CI.

  check_app.py <kit> <app>
  check_app.py <kit> <app> --static <build dir>

--static is the review machine's: the app is a source.tar unpacked (no git, no devkit/), the build is the one the review
rebuilt in its container, and nothing of the app runs (no host tests). Everything else is checked the same way.

The app's manifest (the main rules of Tikpal's contract, and its `records` rule for rule; the review checks it against
the contract itself), its version and project name, the kit untouched at a version, everything committed, the release
build there and of this version, without development commands and at most 3.5 MB, the host tests, and nothing in the
repository that looks like a key, a token or a round screen's id. From the kit whose review/symbols.json has `require`
on (1.9), that the system pull-down cannot be left out: the components and the link table have it, app.json has its
voiceCode, the app's own code starts it and says nothing that takes it away. Standard library only: it runs before
ESP-IDF is installed too.
"""
import hashlib
import json
import math
import os
import re
import subprocess
import sys

SLOT_MAX = 3670016
RESERVED = {"yay", "launcher", "recovery", "template", "system"}
SCENES = {"party", "learning", "relax", "language", "focus", "tools"}
AUDIENCES = {"kids", "adults", "all"}
LANGS = {"zh", "en", "ja", "es-419"}
KEYS = {
    "slug", "kind", "version", "name", "icon", "iconLarge", "boards", "layout", "minYay", "apiLevel", "minFirmware",
    "capabilities", "launch", "aliases", "scenes", "audience", "records", "settings", "settingOptions", "settingLabels",
    "settingRules", "settingsScope", "ephemeral", "pricing", "voiceCode",
    "permissions", "privacy", "ageRating", "miniapp",
}
REQUIRED = {"slug", "kind", "version", "name", "boards", "layout", "capabilities", "aliases", "scenes", "audience", "pricing"}
FAKE_DEVICES = {"yk_00000000beef", "yk_0a1b2c3d4e5f", "yk_000000000000", "yk_0123456789ab"}
SECRETS = [
    (re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"), "私钥"),
    (re.compile(r"\b(gh[opusr])_[A-Za-z0-9]{20,}|\bgithub_pat_[A-Za-z0-9_]{20,}"), "GitHub 令牌"),
    (re.compile(r"\bsk-[A-Za-z0-9_-]{20,}"), "API 密钥"),
    (re.compile(r"\bAKIA[0-9A-Z]{16}\b"), "AWS 密钥"),
    (re.compile(r"\beyJ[A-Za-z0-9_-]{8,}\.eyJ[A-Za-z0-9_-]{8,}\."), "令牌（JWT）"),
    (re.compile(r"(?<![0-9A-Fa-f:])(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}(?![0-9A-Fa-f:])"), "圆屏的 USB 序列号"),
]

# The system pull-down (审核规则 F-05): the source of an app, read as text. A build's half is `require` in the kit's
# review/symbols.json; the review reads the rebuilt image for the rest (scripts/review/firmware.py in Tikpal's repository).
C_FILE = re.compile(r"\.(c|h|cc|cpp|cxx|hpp|hh|inc|inl|S|s)$")
CMAKE_FILE = re.compile(r"((^|/)CMakeLists\.txt|\.cmake)$")
STRING = r'"(?:[^"\\\n]|\\.)*"'
STRINGS = rf"(?:{STRING}\s*)+"
C_TOKEN = re.compile(rf"""{STRING}|'(?:[^'\\\n]|\\.)*'|/\*.*?\*/|//[^\n]*""", re.S)
HOME_WORD = re.compile(r"\b(?:is_home|YK_APP_HOME_SLUG)\b")
HOME_SLUG = "yay"  # YK_APP_HOME_SLUG: with .is_home it is the main firmware, which has no pull-down of this kind
SLUG_FIELD = re.compile(rf"(?:\.|->)\s*slug\s*=\s*({STRINGS}|[A-Za-z_]\w*)")
C_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+(\S.*?)[ \t]*$", re.M)
C_CONST = re.compile(rf"\b([A-Za-z_]\w*)\s*(?:\[\s*\])?\s*=\s*({STRINGS})")
# The linker letting the app's definition stand beside the kit's: --allow-multiple-definition (--allow-m on), -z muldefs.
MULTIPLE_DEFINITIONS = re.compile(r"(?<![\w-])--?allow-m[a-z-]*|muldefs\b")
# {"x", "1.0", NULL, true}: by position, is_home goes unnamed. {0} is the one to fill in afterwards.
CONFIG_BY_POSITION = re.compile(r"\byk_app_config_t\b(?:\s*\)|[^;{}()=]*=)\s*\{\s*(?!0\s*\})[^.\s}]")

# `records` (what 动态 keeps) as Tikpal's contract has it, rule for rule: the review packs an app only if the contract
# parses its app.json, so records that pass here pass there. The contract is packages/contracts/src/app-spec.ts in
# Tikpal's repository: `recordKind`, `name`, `recordSchema`, `collectRule`, `milestone`, `records`, and the records
# part of `appSpec`'s superRefine (capabilities.records and records name the same kinds, a collect rule names a
# collection, a milestone a kind, an ephemeral app keeps none); and device-api.ts `capabilityLimits.records`. When
# those change, this changes with them: Tikpal's tests (tests/devkit.test.ts) run check_records on the contract's own
# fixtures and hold it to the contract's answer.
RECORD_KIND = re.compile(r"[a-z][a-z0-9.]{2,40}")  # recordKind; yk_record_kind_ok on the round screen too
FIELD = re.compile(r"[a-z][A-Za-z0-9_]{0,40}")  # name: a record's fields, a collection, a milestone's name
# pages.template, name@version. Spelt [-a-z0-9], not the way the slug's rule is: the kit's own consistency check reads
# the slug's length from the first class written that way in this file.
TEMPLATE = re.compile(r"[a-z][-a-z0-9]{1,40}@[0-9]{1,3}")
SCHEMA_KEYS = [
    "type", "description", "properties", "required", "additionalProperties", "items", "minItems", "maxItems",
    "minLength", "maxLength", "minimum", "maximum", "enum", "anyOf", "default",
]
SCHEMA_TYPES = {"object", "array", "string", "number", "integer", "boolean", "null"}
RECORDS_KEYS = ["ask", "kinds", "collections", "milestones", "feed", "pages"]
RECORDS_DOC = "写法见 devkit/docs/app-json.md 的「记录 records」"
# `voiceCode` (应用码) as Tikpal's contract has it: app-spec.ts `voiceCode` and voice-code.ts VOICE_CODE.numbers.
VOICE_CODE_KEYS = ["number", "ask", "own"]
OWN_ACTION = re.compile(r"8|9|1[0-5]")
# The first version's parts of app.json (app-spec.ts permissions, privacy, ageRating, miniapp) and the 小程序's
# capabilities (miniapp-capabilities.ts): what a manifest may ask for (some level offers it), the level each needs,
# the kind 1 ones app.json says why for, and the ones only named 小程序 may ask for. Tikpal's tests hold these to the
# contract (tests/devkit.test.ts).
DEVICE_HARDWARE = ["microphone", "speaker", "motion", "tf_card", "bluetooth"]
PRIVACY_DATA = ["voice", "text", "photos", "location", "usage"]
AGE_RATINGS = ["4+", "9+", "12+", "17+"]
MINIAPP_LEVELS = {
    "recordings": 1, "clipboard": 1, "audio": 1,
    "camera": 2, "microphone": 2, "motion": 2, "location": 2, "scan": 2, "share": 2, "photos": 2,
    "records": 2, "settings": 2, "user": 2,
}
PHONE_ABILITIES = ["camera", "microphone", "motion", "location"]
MINIAPP_ONLY = {"recordings": ["recording-card"], "audio": ["recording-card"]}
MINIAPP_PATH = re.compile(r"(?!.*(?:^|/)\.\.?(?:/|$))(?!/)(?!.*//)(?!.*/$)[a-z0-9._/-]{1,120}")
SPEC_VERSION = re.compile(r"\d{1,4}\.\d{1,4}\.\d{1,4}(?:[-+][0-9A-Za-z.-]{1,30})?")
VOICE_CODE_DOC = "写法见 devkit/docs/app-json.md 的「应用码 voiceCode」"
TEST_NUMBERS = (65280, 65535)
# The kinds main/ writes, as a hint: yk_app_record("mytimer.count", ...) with the kind written out.
RECORD_CALL = re.compile(r'\byk_app_record\(\s*"([^"\\]*)"')
# The NVS namespaces main/ opens with the name written out: nvs_open("yk_mytimer", ...).
NVS_CALL = re.compile(r'\bnvs_open\(\s*"([^"\\]*)"')

problems, notes = [], []


def bad(text):
    problems.append(text)


def version_tuple(v):
    return tuple(int(x) for x in re.match(r"^(\d+)\.(\d+)\.(\d+)", v).groups())


def git(cwd, *args):
    return subprocess.run(["git", "-c", "safe.directory=*", "-C", cwd, *args], capture_output=True, text=True)


def text_of(spec, field, maxlen, where=None):
    check_text(where or field, spec.get(field), maxlen)


def check_text(where, value, maxlen):
    if not isinstance(value, dict) or set(value) - LANGS or not {"zh", "en"} <= set(value):
        bad(f"app.json 的 {where} 要有 zh 和 en（可以再加 ja、es-419），不能有别的语言")
        return
    for lang, s in value.items():
        if not isinstance(s, str) or not s.strip() or len(s) > maxlen:
            bad(f"app.json 的 {where}.{lang} 要是 1–{maxlen} 个字符")


def phrases_of(where, value, items, maxlen):
    if not isinstance(value, dict) or set(value) - LANGS or not {"zh", "en"} <= set(value):
        bad(f"app.json 的 {where} 要有 zh 和 en 两个列表")
        return
    for lang, lst in value.items():
        if not isinstance(lst, list) or len(lst) > items or any(not isinstance(s, str) or not s or len(s) > maxlen for s in lst):
            bad(f"app.json 的 {where}.{lang} 最多 {items} 句，每句 1–{maxlen} 个字符")


def is_number(v):
    # JSON numbers as zod's z.number() takes them: not true/false (bool is an int in Python), nothing infinite.
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def is_int(v, low=None, high=None):
    # z.number().int(): a safe integer (|v| < 2^53); 3.0 is one too, as JavaScript reads it.
    return (is_number(v) and v == int(v) and abs(v) <= 2**53 - 1
            and (low is None or v >= low) and (high is None or v <= high))


def is_field(v):
    return isinstance(v, str) and FIELD.fullmatch(v) is not None


def strict(where, value, known, required=()):
    """An object with these keys and no others (the contract's .strict()); False when it is not an object at all."""
    if not isinstance(value, dict):
        bad(f"app.json 的 {where} 要是对象")
        return False
    for key in sorted(set(value) - set(known)):
        bad(f"app.json 的 {where} 里有不认识的字段 {key}（只有 " + "、".join(known) + "）")
    missing = [key for key in required if key not in value]
    if missing:
        bad(f"app.json 的 {where} 少了 " + "、".join(missing))
    return True


def check_schema(where, schema):
    """recordSchema: the part of JSON Schema (2020-12) a record may use. Every object lists its fields and allows no
    others, every array says what its items are; any keyword but these is refused."""
    if not strict(where, schema, SCHEMA_KEYS):
        return
    if "type" in schema and (not isinstance(schema["type"], str) or schema["type"] not in SCHEMA_TYPES):
        bad(f"app.json 的 {where}.type 是 " + "、".join(sorted(SCHEMA_TYPES)) + " 之一")
    if "description" in schema and (not isinstance(schema["description"], str) or len(schema["description"]) > 200):
        bad(f"app.json 的 {where}.description 最多 200 个字符")
    properties = schema.get("properties")
    if "properties" in schema:
        if not isinstance(properties, dict):
            bad(f"app.json 的 {where}.properties 要是对象：字段名对它的 schema")
            properties = None
        else:
            for field, sub in properties.items():
                if not is_field(field):
                    bad(f"app.json 的 {where}.properties 的字段名 {field} 要小写字母开头，只用字母、数字、下划线，最长 41 个字符")
                check_schema(f"{where}.properties.{field}", sub)
    if "required" in schema:
        required = schema["required"]
        if not isinstance(required, list) or not all(is_field(f) for f in required):
            bad(f"app.json 的 {where}.required 要是字段名的列表")
        else:
            for field in required:
                if not isinstance(properties, dict) or field not in properties:
                    bad(f"app.json 的 {where}.required 里的 {field} 不在 properties 里")
            if len(set(required)) != len(required):
                bad(f"app.json 的 {where}.required 里每个字段只写一次")
    if "additionalProperties" in schema and schema["additionalProperties"] is not False:
        bad(f"app.json 的 {where}.additionalProperties 只能写 false")
    if "items" in schema:
        check_schema(f"{where}.items", schema["items"])
    for key in ("minItems", "maxItems", "minLength", "maxLength"):
        if key in schema and not is_int(schema[key], 0):
            bad(f"app.json 的 {where}.{key} 要是不小于 0 的整数")
    for key in ("minimum", "maximum"):
        if key in schema and not is_number(schema[key]):
            bad(f"app.json 的 {where}.{key} 要是数")
    if "enum" in schema:
        values = schema["enum"]
        if not isinstance(values, list) or not values or not all(v is None or isinstance(v, (str, bool)) or is_number(v) for v in values):
            bad(f"app.json 的 {where}.enum 至少一个值，每个是字符串、数、true/false 或 null")
        elif len({json.dumps(v, sort_keys=True) for v in values}) != len(values):
            bad(f"app.json 的 {where}.enum 里每个值只写一次")
    if "anyOf" in schema:
        options = schema["anyOf"]
        if not isinstance(options, list) or not options:
            bad(f"app.json 的 {where}.anyOf 至少一个 schema")
        else:
            for i, sub in enumerate(options):
                check_schema(f"{where}.anyOf[{i}]", sub)
    # recordSchema's superRefine. `default` takes anything.
    if schema.get("type") == "object" and ("properties" not in schema or schema.get("additionalProperties") is not False):
        bad(f'app.json 的 {where} 是对象：要列出 properties，并写 "additionalProperties": false')
    if schema.get("type") == "array" and "items" not in schema:
        bad(f"app.json 的 {where} 是数组：要写 items")


def check_collect(where, rule, collections):
    """collectRule: what a kept record adds to a collection (the record, or each item of its list `each`)."""
    if not strict(where, rule, ["collection", "each", "key", "keyAs", "fields", "with"], ["collection", "key"]):
        return
    for key in ("collection", "each", "key"):
        if key in rule and not is_field(rule[key]):
            bad(f"app.json 的 {where}.{key} 要是一个字段名（小写字母开头，只用字母、数字、下划线）")
    if "keyAs" in rule and rule["keyAs"] not in ("lower", "phrase"):
        bad(f"app.json 的 {where}.keyAs 是 lower 或 phrase")
    if "fields" in rule and (not isinstance(rule["fields"], list) or not rule["fields"] or not all(is_field(f) for f in rule["fields"])):
        bad(f"app.json 的 {where}.fields 至少一个字段名")
    if "with" in rule and (not isinstance(rule["with"], dict) or not all(is_field(k) and is_field(v) for k, v in rule["with"].items())):
        bad(f"app.json 的 {where}.with 要是字段名对字段名")
    # appSpec's superRefine: the collection is one of records.collections.
    if is_field(rule.get("collection")) and rule["collection"] not in collections:
        bad(f"app.json 的 {where}.collection 是 {rule['collection']}，records.collections 里没有这个集合")


def check_milestone(where, m, kinds):
    """milestone: days in a row (streak), a field summed over a kind (total), the first of each value or of `when`."""
    if not isinstance(m, dict) or m.get("type") not in ("streak", "total", "first"):
        bad(f"app.json 的 {where} 要有 type：streak、total 或 first")
        return
    if m["type"] == "streak":
        strict(where, m, ["type", "days"], ["days"])
        if "days" in m and not is_int(m["days"], 2, 3650):
            bad(f"app.json 的 {where}.days 是 2–3650 的整数")
        return
    if m["type"] == "total":
        strict(where, m, ["type", "kind", "field", "reach"], ["kind", "field", "reach"])
        if "field" in m and not is_field(m["field"]):
            bad(f"app.json 的 {where}.field 要是一个字段名")
        if "reach" in m and not is_int(m["reach"], 1):
            bad(f"app.json 的 {where}.reach 是正整数")
    else:
        strict(where, m, ["type", "kind", "by", "name", "when"], ["kind"])
        for key in ("by", "name"):
            if key in m and not is_field(m[key]):
                bad(f"app.json 的 {where}.{key} 要是一个字段名")
        when = m.get("when")
        if "when" in m and (not isinstance(when, dict) or not all(
                is_field(k) and (isinstance(v, (str, bool)) or is_number(v)) for k, v in when.items())):
            bad(f"app.json 的 {where}.when 要是字段名对值（字符串、数、true/false）")
        # The contract's refine: !!by !== !!name && !!name === !!when.
        if bool(m.get("by")) == bool(m.get("name")) or bool(m.get("name")) != ("when" in m and m["when"] is not None):
            bad(f"app.json 的 {where} 写 by（每个值第一次），或者写 name 加 when（第一次是这些值），只写一种")
    kind = m.get("kind")
    if "kind" in m and (not isinstance(kind, str) or not RECORD_KIND.fullmatch(kind)):
        bad(f"app.json 的 {where}.kind 只用小写字母、数字和点，字母开头，3–41 个字符")
    elif isinstance(kind, str) and kind not in kinds:
        bad(f"app.json 的 {where}.kind 是 {kind}，records.kinds 里没有这种记录")


def kind_names(value):
    return [k for k in value if isinstance(k, str)] if isinstance(value, list) else []


def check_records(spec):
    """`records` and `capabilities.records`, as the contract has them (the comment above RECORD_KIND says where)."""
    caps = spec.get("capabilities") if isinstance(spec.get("capabilities"), dict) else {}
    allowed = []
    if "records" in caps:
        cap = caps["records"]
        if strict("capabilities.records", cap, ["kinds"], ["kinds"]):
            allowed = kind_names(cap.get("kinds"))
            if "kinds" in cap and (not isinstance(cap["kinds"], list) or not cap["kinds"] or len(allowed) != len(cap["kinds"])
                                   or not all(RECORD_KIND.fullmatch(k) for k in allowed)):
                bad("app.json 的 capabilities.records.kinds 至少一种，每种只用小写字母、数字和点，字母开头，3–41 个字符")
    if spec.get("ephemeral") is True and "records" in caps:
        bad("app.json 写了 ephemeral（什么都不留），就不能有 capabilities.records")

    kinds = []
    if "records" in spec:
        records = spec["records"]
        if not isinstance(records, dict):
            bad(f"app.json 的 records 要是对象，{RECORDS_DOC}")
            records = {}
        else:
            unknown = sorted(set(records) - set(RECORDS_KEYS))
            for key in unknown:
                bad(f"app.json 的 records 里有不认识的字段 {key}（只有 " + "、".join(RECORDS_KEYS) + f"）：{RECORDS_DOC}")
            missing = [key for key in ("ask", "kinds", "feed", "pages") if key not in records]
            if missing:
                bad("app.json 的 records 少了 " + "、".join(missing) + ("" if unknown else f"：{RECORDS_DOC}"))
        if "ask" in records:
            text_of(records, "ask", 400, "records.ask")

        collections = set()
        if "collections" in records:
            listed = records["collections"]
            if not isinstance(listed, list) or len(listed) > 8:
                bad("app.json 的 records.collections 最多 8 个集合")
                listed = []
            for i, c in enumerate(listed):
                where = f"records.collections[{i}]"
                if not strict(where, c, ["name", "title", "unit", "every"], ["name", "title", "unit"]):
                    continue
                if is_field(c.get("name")):
                    collections.add(c["name"])
                elif "name" in c:
                    bad(f"app.json 的 {where}.name 要小写字母开头，只用字母、数字、下划线")
                for key in ("title", "unit"):
                    if key in c:
                        text_of(c, key, 20, f"{where}.{key}")
                if "every" in c and not is_int(c["every"], 1):
                    bad(f"app.json 的 {where}.every 是正整数")

        entries = records.get("kinds")
        if "kinds" in records and (not isinstance(entries, list) or not 1 <= len(entries) <= 16):
            bad("app.json 的 records.kinds 是 1–16 种记录的列表")
        for i, k in enumerate(entries if isinstance(entries, list) else []):
            where = f"records.kinds[{i}]"
            if not strict(where, k, ["kind", "schema", "noMemory", "collect"], ["kind", "schema"]):
                continue
            if isinstance(k.get("kind"), str):
                kinds.append(k["kind"])
            if "kind" in k and (not isinstance(k["kind"], str) or not RECORD_KIND.fullmatch(k["kind"])):
                bad(f"app.json 的 {where}.kind 只用小写字母、数字和点，字母开头，3–41 个字符，比如 mytimer.count")
            if "schema" in k:
                check_schema(f"{where}.schema", k["schema"])
                if not isinstance(k["schema"], dict) or k["schema"].get("type") != "object":
                    bad(f'app.json 的 {where}.schema 要写 "type": "object"：一条记录是一个对象')
            if "noMemory" in k and k["noMemory"] is not True:
                bad(f"app.json 的 {where}.noMemory 只能写 true（不想让伙伴的记忆读这种记录时写），不要就别写")
            if "collect" in k:
                rules = k["collect"]
                if not isinstance(rules, list) or not 1 <= len(rules) <= 8:
                    bad(f"app.json 的 {where}.collect 是 1–8 条")
                else:
                    for j, rule in enumerate(rules):
                        check_collect(f"{where}.collect[{j}]", rule, collections)

        if "milestones" in records:
            listed = records["milestones"]
            if not isinstance(listed, list) or len(listed) > 16:
                bad("app.json 的 records.milestones 最多 16 个")
                listed = []
            for i, m in enumerate(listed):
                check_milestone(f"records.milestones[{i}]", m, kinds)

        if "feed" in records and strict("records.feed", records["feed"], ["cadence", "feedDays"], ["cadence", "feedDays"]):
            feed = records["feed"]
            if "cadence" in feed and feed["cadence"] not in ("daily", "weekly", "session"):
                bad("app.json 的 records.feed.cadence 是 daily（每天早上）、weekly（每周一）或 session（用完一次）")
            if "feedDays" in feed and not is_int(feed["feedDays"], 1, 366):
                bad("app.json 的 records.feed.feedDays 是 1–366 的整数")
        if "pages" in records and strict("records.pages", records["pages"], ["template"], ["template"]):
            template = records["pages"].get("template")
            if "template" in records["pages"] and (not isinstance(template, str) or not TEMPLATE.fullmatch(template)):
                bad("app.json 的 records.pages.template 写成 名字@版本（比如 mytimer@1），模板名跟我们一起定")

    # appSpec's superRefine: the kinds the round screen may write (capabilities.records) are the kinds 动态 knows
    # (records.kinds), compared as the contract does: as many, and each allowed one among them.
    if len(allowed) != len(kinds) or any(k not in kinds for k in allowed):
        bad("app.json 的 capabilities.records.kinds（" + ("、".join(allowed) or "没写") + "）和 records.kinds（"
            + ("、".join(kinds) or "没写") + "）要是同样几种：圆屏只能写前者开了的，「动态」只收后者有 schema 的")


def record_hints(spec, app):
    """A hint, never a problem: kinds main/ writes with yk_app_record("<kind>", ...) that records.kinds does not
    declare. The server takes no record of a kind it was not given (the kit's template writes one, <prefix>.count)."""
    records = spec.get("records") if isinstance(spec.get("records"), dict) else {}
    declared = {k.get("kind") for k in records.get("kinds") or [] if isinstance(k, dict) and isinstance(k.get("kind"), str)}
    seen = set()
    for root, dirs, files in os.walk(os.path.join(app, "main")):
        dirs.sort()
        for name in sorted(f for f in files if f.endswith(".c")):
            path = os.path.join(root, name)
            for n, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
                for kind in RECORD_CALL.findall(line):
                    if kind not in declared and kind not in seen:
                        seen.add(kind)
                        notes.append(f"提示：{os.path.relpath(path, app)}:{n} 用 yk_app_record 写 {kind}，app.json 的 records 里没有这种，服务端不收。"
                                     "要留进「动态」，照 devkit/docs/app-json.md 的「记录 records」补上 records 和 capabilities.records，"
                                     "先在开发者平台的「能力和权限」页申请「记录」能力；不要就删掉这句")


def own_namespace(slug):
    """The app's own NVS namespace, yk_<简称>: 简称 is the slug without its hyphens (as in its record kinds,
    <简称>.count), and NVS takes at most 15 characters. The review holds every nvs_open in the image to it."""
    return ("yk_" + slug.replace("-", ""))[:15]


def check_namespaces(slug, app):
    """A namespace main/ opens by name that is not the app's own: the review returns it (symbols.namespace)."""
    own = own_namespace(slug)
    for root, dirs, files in os.walk(os.path.join(app, "main")):
        dirs.sort()
        for name in sorted(f for f in files if f.endswith(".c")):
            path = os.path.join(root, name)
            for n, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
                for ns in NVS_CALL.findall(line):
                    if ns != own:
                        bad(f"{os.path.relpath(path, app)}:{n} nvs_open 打开的是 {ns}：设置只能放自己的命名空间 {own}"
                            "（yk_ 加 slug 去掉连字符，最长 15 个字符；devkit/docs/app-guide.md 的 NVS）")


def check_voice_code(spec):
    """`voiceCode`, as the contract has it, `own` (actions 8–15) included: open since the first version (11-02)."""
    if "voiceCode" not in spec:
        return
    code = spec["voiceCode"]
    if not strict("voiceCode", code, VOICE_CODE_KEYS, ["number"]):
        return
    number = code.get("number")
    if "number" in code and not is_int(number, 1, 65535):
        bad(f"app.json 的 voiceCode.number 要是建应用时给的应用编号，1–65535 的整数：{VOICE_CODE_DOC}")
    elif is_int(number, *TEST_NUMBERS):
        notes.append(f"提示：voiceCode.number 是本机测试号（{TEST_NUMBERS[0]}–{TEST_NUMBERS[1]}），只能在自己的圆屏上试；"
                     "提交前换成建应用时给的编号，审核不收测试号")
    if "ask" in code:
        ask = code["ask"]
        phrases_of("voiceCode.ask", ask, 16, 80)
        if isinstance(ask, dict) and isinstance(ask.get("zh"), list):
            if not ask["zh"]:
                bad(f"app.json 的 voiceCode.ask.zh 至少一句：{VOICE_CODE_DOC}")
            for lang, sentences in sorted(ask.items()):
                if isinstance(sentences, list) and len(sentences) != len(ask["zh"]):
                    bad(f"app.json 的 voiceCode.ask.{lang} 要和 zh 一样多句、一一对应（第几句就是码里的序号）")
    if "own" in code:
        own = code["own"]
        if not isinstance(own, dict):
            bad(f"app.json 的 voiceCode.own 要是对象：动作号（8–15）对应它的意思：{VOICE_CODE_DOC}")
            return
        for action, meaning in sorted(own.items()):
            if not OWN_ACTION.fullmatch(action):
                bad(f"app.json 的 voiceCode.own 只能用动作 8–15，没有 {action}：{VOICE_CODE_DOC}")
            else:
                check_text(f"voiceCode.own.{action}", meaning, 200)


def check_first_version(spec):
    """permissions, privacy, ageRating and miniapp, as the contract has them (app-spec.ts, 11-02)."""
    asked = []
    if "miniapp" in spec:
        mini = spec["miniapp"]
        if strict("miniapp", mini, ["entry", "capabilities", "capabilityLevel", "minFirmware"], ["entry", "capabilities", "capabilityLevel"]):
            if "entry" in mini and not (isinstance(mini["entry"], str) and MINIAPP_PATH.fullmatch(mini["entry"])):
                bad("app.json 的 miniapp.entry 是包里的一个路径：小写字母、数字和 . _ - /，不能有 ..")
            caps = mini.get("capabilities")
            if not isinstance(caps, list) or any(not isinstance(c, str) for c in caps):
                bad("app.json 的 miniapp.capabilities 要是能力名的列表")
                caps = []
            for c in caps:
                if c not in MINIAPP_LEVELS:
                    bad(f"app.json 的 miniapp.capabilities 里的 {c} 还没开放（能开放的：" + "、".join(MINIAPP_LEVELS) + "）")
                elif c in MINIAPP_ONLY and spec.get("slug") not in MINIAPP_ONLY[c]:
                    bad(f"app.json 的 miniapp.capabilities 里的 {c} 只给 " + "、".join(MINIAPP_ONLY[c]))
            if len(set(caps)) != len(caps):
                bad("app.json 的 miniapp.capabilities 有能力写了两次")
            level = mini.get("capabilityLevel")
            need = max([MINIAPP_LEVELS.get(c, 1) for c in caps] + [1])
            if not is_int(level, 1):
                bad("app.json 的 miniapp.capabilityLevel 是至少 1 的整数")
            elif level < need:
                bad(f"app.json 的 miniapp.capabilityLevel 至少要 {need}：要的能力从第 {need} 级才有")
            if "minFirmware" in mini and not (isinstance(mini["minFirmware"], str) and SPEC_VERSION.fullmatch(mini["minFirmware"])):
                bad("app.json 的 miniapp.minFirmware 是三段式版本号，比如 0.2.0")
            asked = [c for c in caps if c in PHONE_ABILITIES]
            if "records" in caps and "records" not in spec:
                bad("app.json 的 miniapp 要 records，app.json 就要有 records")
            if "settings" in caps and not spec.get("settings"):
                bad("app.json 的 miniapp 要 settings，app.json 就要有 settings")
            if spec.get("ephemeral") is True and ("records" in caps or "settings" in caps):
                bad("app.json 写了 ephemeral（什么都不留），小程序就不能要 records、settings")
    why = []
    if "permissions" in spec:
        perm = spec["permissions"]
        if strict("permissions", perm, ["device", "phone"]):
            device = perm.get("device", [])
            if not isinstance(device, list) or any(d not in DEVICE_HARDWARE for d in device) or len(set(device)) != len(device):
                bad("app.json 的 permissions.device 是用到的 Tikpal Voice 硬件，每样写一次：" + "、".join(DEVICE_HARDWARE))
            phone = perm.get("phone", {})
            if not isinstance(phone, dict):
                bad("app.json 的 permissions.phone 要是对象：手机能力对应为什么要用")
                phone = {}
            for ability, text in sorted(phone.items()):
                if ability not in PHONE_ABILITIES:
                    bad(f"app.json 的 permissions.phone 只写 " + "、".join(PHONE_ABILITIES) + f"，没有 {ability}")
                else:
                    check_text(f"permissions.phone.{ability}", text, 120)
                    why.append(ability)
    for ability in asked:
        if ability not in why:
            bad(f"app.json 的小程序要 {ability}，permissions.phone 里要写为什么用")
    for ability in why:
        if ability not in asked:
            bad(f"app.json 的 permissions.phone 写了 {ability}，小程序却没要它")
    if "privacy" in spec:
        privacy = spec["privacy"]
        if strict("privacy", privacy, ["collects"], ["collects"]):
            collects = privacy.get("collects")
            if not isinstance(collects, list):
                bad("app.json 的 privacy.collects 是列表：收什么、为什么；什么都不收写空列表")
            else:
                seen = []
                for i, item in enumerate(collects):
                    if strict(f"privacy.collects[{i}]", item, ["data", "why"], ["data", "why"]):
                        if item.get("data") not in PRIVACY_DATA:
                            bad(f"app.json 的 privacy.collects[{i}].data 是 " + "、".join(PRIVACY_DATA) + " 之一")
                        elif item["data"] in seen:
                            bad(f"app.json 的 privacy.collects 里 {item['data']} 写了两次")
                        else:
                            seen.append(item["data"])
                        if "why" in item:
                            check_text(f"privacy.collects[{i}].why", item["why"], 200)
    if "ageRating" in spec and spec["ageRating"] not in AGE_RATINGS:
        bad("app.json 的 ageRating 是 " + "、".join(AGE_RATINGS) + " 之一")


def kit_require(kit_dir):
    """`require` in the kit's review/symbols.json, None for a kit without it (before 1.9): the review runs this file on
    every kit, so an app on an older one is held to what that kit had. SUBMISSION_MIN_KIT on the platform moves them on."""
    try:
        return json.load(open(os.path.join(kit_dir, "review", "symbols.json"), encoding="utf-8")).get("require")
    except (OSError, ValueError):
        return None


def linked_symbols(text):
    """The names a map file places: its memory map only, not the sections thrown away before it nor the cross
    reference table after it, which name what never made it into the image."""
    start = text.find("Linker script and memory map")
    end = text.find("Cross Reference Table", max(start, 0))
    placed = text[start:end if end >= 0 else None] if start >= 0 else ""
    return set(re.findall(r"^\s+0x[0-9a-f]+\s+([A-Za-z_][\w$.]*)\s*$", placed, re.M))


def check_pulldown_build(require, desc, build):
    """The system pull-down is in the build: its components, and in the link table what makes it (what is not reached
    is gone from there: a component in the build proves nothing by itself)."""
    for component in require["components"]:
        if component not in desc.get("build_components", []):
            bad(f"构建里没有 {component}：系统下拉靠它。组件别删，CMakeLists.txt 照模板写，重新 ./kit build --release")
    try:
        linked = linked_symbols(open(os.path.join(build, desc.get("project_name", "") + ".map"), encoding="utf-8", errors="replace").read())
    except OSError:
        bad("构建里没有链接表（.map）：重新 ./kit build --release")
        return
    for symbol in require["symbols"]:
        if symbol not in linked:
            bad(f"链接表里没有 {symbol}：系统下拉没接上。yk_app_start()、yk_app_ready() 照模板调，别换掉开发包的组件")


def c_code(text):
    """C with its comments blanked: the strings stay, and every line where it was."""
    return C_TOKEN.sub(lambda m: m.group(0) if m.group(0)[0] in "\"'" else re.sub(r"[^\n]", " ", m.group(0)), text)


def app_sources(app, tracked):
    """The app's own C and CMake, comments out: path → text. Everything of it, not main/ only: a component in the
    project is its code too."""
    out = {}
    for path in filter(None, tracked):
        full = os.path.join(app, path)
        if path == "devkit" or path.startswith("devkit/") or not os.path.isfile(full):
            continue
        if C_FILE.search(path):
            out[path] = c_code(open(full, encoding="utf-8", errors="replace").read())
        elif CMAKE_FILE.search(path):
            out[path] = re.sub(r"#[^\n]*", " ", open(full, encoding="utf-8", errors="replace").read())
    return out


def line_of(text, match):
    return text.count("\n", 0, match.start()) + 1


def string_value(value, constants):
    """What a slug's value says when it can be told: a string (or several in a row), or a name for one."""
    for _ in range(8):
        if re.fullmatch(STRINGS, value):
            return "".join(s[1:-1] for s in re.findall(STRING, value))
        if value not in constants:
            return None
        value = constants[value]
    return None


def check_pulldown_source(require, sources):
    """The app's own code starts the pull-down and does not say it is the main firmware (.is_home, with the slug yay,
    is the one way yk_app_is_home() lets through)."""
    constants = {}
    for path, text in sources.items():
        if C_FILE.search(path):
            constants.update(C_DEFINE.findall(text))
            constants.update((name, value.strip()) for name, value in C_CONST.findall(text))
    for path, text in sorted(sources.items()):
        for m in HOME_WORD.finditer(text):
            bad(f"{path}:{line_of(text, m)} 写了 {m.group(0)}：那是主固件的，第三方应用不能写。删掉它，系统下拉和应用环才在（审核规则 F-05）")
        if not C_FILE.search(path):
            for m in MULTIPLE_DEFINITIONS.finditer(text):
                bad(f"{path}:{line_of(text, m)} 有链接参数 {m.group(0)}：它让应用的函数盖住开发包的。删掉它（审核规则 F-05）")
            continue
        for m in SLUG_FIELD.finditer(text):
            if string_value(m.group(1).strip(), constants) == HOME_SLUG:
                bad(f"{path}:{line_of(text, m)} 的 .slug 是 {HOME_SLUG}：那是主固件的名字，第三方应用不能用。写 app.json 的 slug（审核规则 F-05）")
        for m in CONFIG_BY_POSITION.finditer(text):
            bad(f"{path}:{line_of(text, m)} 的 yk_app_config_t 按位置写的：照模板按字段写（.slug = …, .version = …）")
    for call in require["calls"]:
        if not any(re.search(rf"\b{call}\s*\(", text) for path, text in sources.items() if C_FILE.search(path)):
            bad(f"应用代码里没调 {call}()：系统下拉接不上。启动顺序见 devkit/docs/app-guide.md 第 4 节")


def check_manifest(spec, kit):
    for key in sorted(set(spec) - KEYS):
        bad(f"app.json 里有不认识的字段 {key}")
    for key in sorted(REQUIRED - set(spec)):
        bad(f"app.json 少了 {key}")
    slug = spec.get("slug", "")
    # 24 at most: the image's project name yaykit_<slug> is cut at 31 characters (the kit's `new`, docs/app-json.md).
    if not re.fullmatch(r"[a-z][a-z0-9-]{1,23}", str(slug)) or slug in RESERVED:
        bad("app.json 的 slug 只用小写字母、数字、连字符，字母开头，2–24 个字符，不能是 " + "、".join(sorted(RESERVED)))
    if spec.get("kind") != "extension":
        bad('app.json 的 kind 要写 "extension"')
    if not re.match(r"^\d{1,4}\.\d{1,4}\.\d{1,4}$", str(spec.get("version", ""))):
        bad("app.json 的 version 要是 主.次.修，比如 0.1.0")
    text_of(spec, "name", 24)
    if "tikpal-r1" not in (spec.get("boards") or []):
        bad('app.json 的 boards 要有 "tikpal-r1"')
    if spec.get("layout") != "yaykit-v1":
        bad('app.json 的 layout 要写 "yaykit-v1"')
    min_yay = spec.get("minYay")
    if not min_yay or not re.match(r"^\d+\.\d+\.\d+$", str(min_yay)):
        bad(f'app.json 要写 minYay（至少 "{kit["yay"]}"）')
    elif version_tuple(min_yay) < version_tuple(kit["yay"]):
        bad(f'app.json 的 minYay 是 {min_yay}，比开发包对应的 Tikpal 主固件 {kit["yay"]} 还旧')
    if spec.get("apiLevel") != kit["apiLevel"]:
        bad(f'app.json 的 apiLevel 要写开发包的 {kit["apiLevel"]}')
    if not isinstance(spec.get("capabilities"), dict):
        bad("app.json 的 capabilities 要是对象；不要云能力就写 {}")
    launch = spec.get("launch")
    if launch is not None:
        if not isinstance(launch, dict) or set(launch) - {"summary", "examples", "params"}:
            bad("app.json 的 launch 只有 summary、examples、params")
        else:
            text_of(launch, "summary", 300, "launch.summary")
            phrases_of("launch.examples", launch.get("examples"), 12, 80)
            for name, p in (launch.get("params") or {}).items():
                if not isinstance(p, dict) or p.get("type") not in {"string", "number", "boolean"} or not p.get("description"):
                    bad(f"app.json 的 launch.params.{name} 要有 type（string、number、boolean）和 description")
    phrases_of("aliases", spec.get("aliases"), 8, 24)
    scenes = spec.get("scenes")
    if not isinstance(scenes, list) or not 1 <= len(scenes) <= 3 or set(scenes) - SCENES:
        bad("app.json 的 scenes 是 1–3 个：" + "、".join(sorted(SCENES)))
    if spec.get("audience") not in AUDIENCES:
        bad("app.json 的 audience 是 kids、adults 或 all")
    if spec.get("pricing") != {"kind": "free"}:
        bad('app.json 的 pricing 现在只能是 {"kind": "free"}')
    check_records(spec)
    check_voice_code(spec)
    check_first_version(spec)


def main():
    kit_dir, app = sys.argv[1], sys.argv[2]
    static = sys.argv[4] if len(sys.argv) > 4 and sys.argv[3] == "--static" else None
    kit = json.load(open(os.path.join(kit_dir, "devkit.json")))
    try:
        spec = json.load(open(os.path.join(app, "app.json"), encoding="utf-8"))
    except (OSError, ValueError) as e:
        print(f"✗ app.json 读不了：{e}")
        return 1
    check_manifest(spec, kit)
    require = kit_require(kit_dir)
    if require and "voiceCode" not in spec:
        bad(f"app.json 少了 voiceCode：建应用时给的应用编号写进去，下拉里的「在手机上打开」靠它。{VOICE_CODE_DOC}")
    record_hints(spec, app)
    slug, version = spec.get("slug", ""), spec.get("version", "")
    if isinstance(slug, str) and slug:
        check_namespaces(slug, app)

    cmake = open(os.path.join(app, "CMakeLists.txt"), encoding="utf-8").read()
    project = re.search(r"project\((\w+)\)", cmake)
    if not project or project.group(1) != "yaykit_" + slug.replace("-", "_"):
        bad(f"CMakeLists.txt 的 project() 要是 yaykit_{slug.replace('-', '_')}")
    ver = re.search(r'set\(PROJECT_VER "([^"]+)"\)', cmake)
    if not ver or ver.group(1) != version:
        bad(f"CMakeLists.txt 的 PROJECT_VER 要和 app.json 的 version（{version}）一样")

    devkit = os.path.join(app, "devkit")
    if not static and os.path.isdir(os.path.join(devkit, "firmware")):
        if git(devkit, "status", "--porcelain").stdout.strip():
            bad("devkit/ 里有改动：开发包只读，git -C devkit checkout . 还原")
        tag = git(devkit, "describe", "--tags", "--exact-match").stdout.strip()
        if tag != "v" + kit["version"]:
            bad(f"devkit/ 不是开发包的正式版本（被切到了别的提交）：./kit update {kit['version']} 换回 {kit['version']} 版")
    dirty = "" if static else git(app, "status", "--porcelain").stdout.strip()
    if dirty:
        bad("有没提交的改动（审核按提交号重编，没提交的不算）：\n    " + dirty.replace("\n", "\n    "))
    head = "" if static else git(app, "rev-parse", "HEAD").stdout.strip()

    build = static or os.path.join(app, "build-release")
    desc_path = os.path.join(build, "project_description.json")
    if not os.path.exists(desc_path):
        bad("还没编提交用的那份：./kit build --release")
    else:
        desc = json.load(open(desc_path))
        if desc.get("project_version") != version:
            bad(f"build-release 是 {desc.get('project_version')} 编的，不是 {version}：重新 ./kit build --release")
        config = open(os.path.join(build, "sdkconfig"), encoding="utf-8").read()
        if "CONFIG_YAY_DEV=y" in config:
            bad("提交用的那份带着开发命令（CONFIG_YAY_DEV=y）：用 ./kit build --release 编")
        if "CONFIG_APP_REPRODUCIBLE_BUILD=y" not in config:
            bad("没开 CONFIG_APP_REPRODUCIBLE_BUILD：别从 sdkconfig.defaults 里删它")
        image = os.path.join(build, desc["app_bin"])
        data = open(image, "rb").read()
        if len(data) > SLOT_MAX:
            bad(f"镜像 {len(data)} 字节，超过 {SLOT_MAX}（3.5 MB）")
        notes.append(f"镜像 {desc['app_bin']}：{len(data)} 字节，SHA-256 {hashlib.sha256(data).hexdigest()}")
        if require:
            check_pulldown_build(require, desc, build)

    tests = os.path.join(app, "test", "run.sh")
    if static:
        notes.append("主机测试在审核的容器里跑过，这里不跑")
    elif os.path.exists(tests):
        run = subprocess.run(["bash", tests], capture_output=True, text=True)
        if run.returncode:
            bad("主机测试没过：\n    " + (run.stdout + run.stderr).strip().replace("\n", "\n    "))
        else:
            notes.append("主机测试：" + (run.stdout.strip().splitlines() or ["通过"])[-1])
    else:
        notes.append("没有 test/run.sh（还没有主机测试）")

    if static:
        tracked = [os.path.relpath(os.path.join(root, f), app) for root, _d, fs in os.walk(app) for f in fs]
    else:
        tracked = git(app, "ls-files", "-z").stdout.split("\0")
    if require:
        check_pulldown_source(require, app_sources(app, tracked))
    for path in filter(None, tracked):
        full = os.path.join(app, path)
        if path.startswith("devkit") or not os.path.isfile(full):
            continue
        raw = open(full, "rb").read()
        if b"\0" in raw[:8192]:
            continue
        for n, line in enumerate(raw.decode("utf-8", "replace").splitlines(), 1):
            for pattern, what in SECRETS:
                if pattern.search(line):
                    bad(f"{path}:{n} 像是{what}：不要放进仓库")
            for m in re.finditer(r"\byk_[0-9a-f]{12}\b", line):
                if m.group(0) not in FAKE_DEVICES:
                    bad(f"{path}:{n} 有圆屏的设备编号 {m.group(0)}：抹掉（换成 yk_0a1b2c3d4e5f 这样的假号）")

    for n in notes:
        print("· " + n)
    if head:
        print(f"· 提交号 {head}，开发包 {kit['version']}")
    if problems:
        for p in problems:
            print("✗ " + p)
        print(f"有 {len(problems)} 处要改，改完再 ./kit check。")
        return 1
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("✓ 都过了：./kit submit --ci-sha 带上面的 SHA-256（这是应用仓库 CI 编的，跟审核重编的环境一样）。")
    else:
        print("✓ 都过了。./kit submit --ci-sha 带应用仓库 CI（GitHub 上）里 ./kit check 打的那一串 SHA-256：Mac 上编的和")
        print("  Linux 上编的差 60 几个字节（ELF 哈希和两处校验，乐鑫两个平台的工具链不一样），代码一样。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
