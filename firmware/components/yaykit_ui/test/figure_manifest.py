"""The figures' manifests (yaykit/firmware/assets/figures/<figure>.json), for test/run.sh. Only reads.

  fresh <figures dir> <repository root>   each manifest still names the rig and the drawings there are now, whatever
                                          machine this is: nothing here depends on how sharp resizes. Fails when not.
  info <manifest>                         figure, theme, file and the platform it was made on, on one line
  lines <manifest> <rig> <pack>           what test_figure.c reads on stdin; fails when the pack is not the manifest's
"""
import glob, hashlib, json, os, re, sys


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def read(path):
    with open(path) as f:
        return json.load(f)


def fresh(figures, root):
    wrong = []
    for path in sorted(glob.glob(os.path.join(figures, "*.json"))):
        manifest = read(path)
        figure = manifest["figure"]
        rig = os.path.join(root, "packages/yay-art/dist", "rig-%s.json" % figure)
        if not os.path.exists(rig) or sha256(rig) != manifest["rig"]:
            wrong.append("%s: its rig is not %s" % (figure, manifest["rig"][:12]))
        for name, want in manifest["drawings"].items():
            drawing = os.path.join(root, "design/yay/source", figure, name)
            if not os.path.exists(drawing) or sha256(drawing) != want:
                wrong.append("%s: %s is not %s" % (figure, name, want[:12]))
    if wrong:
        print("the figures' manifests are out of date (pnpm art:pack <figure> on the machine that made them):", file=sys.stderr)
        for line in wrong:
            print("  " + line, file=sys.stderr)
        sys.exit(1)
    print("yk_figure: every manifest names the rig and drawings there are now")


def info(path):
    manifest = read(path)
    print(manifest["figure"], manifest["theme"], manifest["file"], manifest["environment"]["platform"])


def lines(path, rig_path, pack):
    manifest = read(path)
    rig = read(rig_path)
    if sha256(pack) != manifest["sha256"]:
        sys.exit("%s is not the manifest's: pnpm art:pack %s" % (pack, manifest["figure"]))
    print("pack", manifest["bytes"], len(manifest["pictures"]))
    r = manifest["round"]
    print("round", r["blinkMs"], r["blinkGapMs"][0], r["blinkGapMs"][1], r["breathePx"], r["breatheMs"])
    for picture in manifest["pictures"]:
        pose, size = re.fullmatch(r"yay_(.+)_(\d+)", picture["name"]).groups()
        patches = picture.get("patches", [])
        print("picture", pose.replace("_", "-"), size, int(picture["eyesOpen"]), picture["offset"], picture["bytes"], len(patches))
        for q in patches:
            print("patch", q["kind"], q["slot"], q["x"], q["y"], q["w"], q["h"])
    print("mouths", len(rig["round"]["mouths"]))


if __name__ == "__main__":
    {"fresh": fresh, "info": info, "lines": lines}[sys.argv[1]](*sys.argv[2:])
