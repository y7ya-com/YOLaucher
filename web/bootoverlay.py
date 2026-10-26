"""Build overlay.zip: the web filesystem's Wine prefix registry with yo2's settings baked in, for faster boots.

    python3 web/bootoverlay.py site/boxedwine.zip site/overlay.zip [--keep SERVICE ...]
    URL: ...&overlay=overlay.zip      (Boxedwine puts overlay zips ahead of the root zip, so these .reg files win)

What it changes in home/username/.wine/{system,user}.reg (the prefix in the zip is already initialised, so Wine never
rebuilds it; the boot cost is the services wineboot starts and start.bat's regedit):
- user.reg: DllOverrides "version"="native,builtin" (the yo2net hook; start.bat no longer needs `regedit /S yo2.reg`)
  and "winemenubuilder.exe"="" (disabled; wineboot runs it from RunServices to make desktop menus).
- system.reg: services the game never uses set to Start=4 (disabled): Eventlog (svchost), PlugPlay (plugplay.exe and
  the WinePlugPlay drivers winebus/winehid/winebth; winebth fails anyway). Kept: MountMgr (drives), NDIS/nsiproxy
  (networking). --keep PlugPlay keeps one.
"""
import re
import sys
import zipfile

PREFIX = "home/username/.wine/"
DISABLE = ["Eventlog", "PlugPlay"]
USER_OVERRIDES = {"version": "native,builtin", "winemenubuilder.exe": ""}


def set_values(reg, key, values):
    """Set string values inside an existing [key] section of a Wine .reg text."""
    head = re.search(r"^\[%s\][^\n]*\n" % re.escape(key), reg, re.M)
    assert head, key
    end = reg.find("\n\n", head.end())
    body = reg[head.end():end]
    for name, data in values.items():
        line = '"%s"=%s' % (name, data)
        pat = re.compile(r'^"%s"=.*$' % re.escape(name), re.M)
        body = pat.sub(line, body) if pat.search(body) else body + "\n" + line
    return reg[:head.end()] + body.lstrip("\n") + reg[end:]


def build(root_zip, out_zip, keep=()):
    z = zipfile.ZipFile(root_zip)
    system = z.read(PREFIX + "system.reg").decode("latin1")
    user = z.read(PREFIX + "user.reg").decode("latin1")
    for svc in DISABLE:
        if svc not in keep:
            system = set_values(system, r"System\\ControlSet001\\Services\\" + svc, {"Start": "dword:00000004"})
    user = set_values(user, r"Software\\Wine\\DllOverrides", {k: '"%s"' % v for k, v in USER_OVERRIDES.items()})
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as o:
        o.writestr(PREFIX + "system.reg", system.encode("latin1"))
        o.writestr(PREFIX + "user.reg", user.encode("latin1"))
    return system, user


if __name__ == "__main__":
    t = set_values('[A\\\\B] 1\n#time=1\n"Start"=dword:00000002\n\n[C] 2\n', r"A\\B", {"Start": "dword:00000004", "x": '""'})
    assert t == '[A\\\\B] 1\n#time=1\n"Start"=dword:00000004\n"x"=""\n\n[C] 2\n', t
    args = sys.argv[1:]
    keep = args[args.index("--keep") + 1:] if "--keep" in args else []
    build(args[0], args[1], keep)
