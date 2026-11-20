#!/usr/bin/env python3
"""Installs an app image into a launcher slot over the USB console.

install_usb.py <port> <slot> <image.bin> [slug] [--open] [--drop-ack=N]

--drop-ack=N (bench): chunk N's YK_ACK is read and thrown away, as if lost; a device that says YK_ACK_AGAIN goes on.

Protocol (firmware/components/yaykit_install, the recovery's and Yay's `install`): `install <slot> <size> <sha256> [slug]`, wait for
YK_INSTALL_READY, then send 4 KB chunks, each answered with YK_ACK <bytes>; ends with
YK_INSTALL OK|<code>. The launcher keeps running; nothing else on the device is touched.

Devices from Yay 0.1.22 and the next recovery say YK_ACK_AGAIN <bytes> while they wait for the next chunk; only the
number equal to what was sent counts, an earlier one is passed over. A chunk is never sent twice (the device takes bytes
in order). Nothing the device said is left waiting in the port while it sends: before each chunk what has come is
taken (the device's USB console drops what the host does not take within 50 ms, and a lost YK_ACK left both sides
waiting: USB_TIMEOUT, 位置 1 erased; 新四号机 11-18). Just after the recovery starts its console is busiest (Wi-Fi):
`settle` waits that out first. One thread: a reading thread of its own raced the device worker's pretend clock
(scripts/device-worker), and the host that never lost one (11-18, 7 installs) read in one thread as this does.
"""
import hashlib, json, re, sys, time
import serial


class Console:
    def __init__(self, port):
        self.port = port
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.2
        self.connect(time.time() + 15)

    def connect(self, deadline):
        while time.time() < deadline:
            try:
                self.s.open()
                # Release RTS, then DTR, so the chip is not reset or put into download mode.
                self.s.rts = False
                self.s.dtr = False
                return True
            except (serial.SerialException, OSError):
                time.sleep(0.2)
        return False

    def take_waiting(self):
        """What the device said and the port holds, taken now (not waiting for more): lines kept for next_line."""
        n = self.s.in_waiting
        if n:
            self._buf = getattr(self, '_buf', b'') + self.s.read(n)

    def next_line(self, seconds=None):
        """The next line: one already taken, else what comes within the port's timeout ('' when none)."""
        buf = getattr(self, '_buf', b'')
        if b'\n' not in buf:
            n = self.s.in_waiting
            buf += self.s.read(n) if n else self.s.readline()
        line, nl, rest = buf.partition(b'\n')
        self._buf = rest if nl else line  # no whole line yet: kept for the next
        return line.decode('utf-8', 'replace').strip() if nl else ''

    def wait_for(self, prefixes, seconds):
        """Returns the first line containing one of prefixes (from it on); reconnects across chip restarts."""
        end = time.time() + seconds
        while time.time() < end:
            try:
                line = self.next_line()
            except (serial.SerialException, OSError):
                try:
                    self.s.close()
                except Exception:
                    pass
                self.connect(end)
                continue
            for p in prefixes:
                if p in line:
                    return line[line.index(p):]
        return None

    def settle(self, least=0.0, quiet=3.0, most=20.0):
        """After the recovery started: at least `least` s, then until its Wi-Fi is up (`yk_wifi: connected joinMs`) or nothing
        but its every-3-s YAYKIT_LAUNCHER_READY came for `quiet` s; `most` s at most. Returns why it stopped."""
        start = time.time()
        quiet_from = start
        wifi = False
        while True:
            now = time.time()
            if now - start >= most:
                return 'timeout'
            if now - start >= least and (wifi or now - quiet_from >= quiet):
                return 'wifi' if wifi else 'quiet'
            line = self.next_line()
            if 'yk_wifi: connected joinMs' in line:  # got its address; not `connected but stuck`, a rejoin
                wifi = True
            elif line and 'YAYKIT_LAUNCHER_READY' not in line:
                quiet_from = time.time()

    def send(self, text):
        self.s.reset_input_buffer()
        self._buf = b''  # what came before the command is not its answer
        self.s.write(text.encode())

    def info(self):
        self.send('\r\nyk info\r\n')
        line = self.wait_for(['YK_INFO '], 4)
        return json.loads(line[line.index('{'):]) if line else None


def ack_of(line):
    """'YK_ACK 4096' or 'YK_ACK_AGAIN 4096' -> 4096; anything else -1."""
    m = re.match(r'YK_ACK(?:_AGAIN)? (\d+)(?:\s|$)', line or '')
    return int(m.group(1)) if m else -1


def install(con, slot, data, slug='', sha256=None, stop_after=None, settle=None, drop_ack=None):
    """Streams data into a slot. stop_after simulates a cable pulled after that many bytes. settle: (least, quiet,
    most) seconds to wait for the recovery to settle first (Console.settle), None not at all. drop_ack: the chunk
    (1 = the first) whose YK_ACK is read and thrown away, as if lost: a device that says YK_ACK_AGAIN goes on (about
    a second later), one that does not ends USB_TIMEOUT-like after 15 s (the bench check of YK_ACK_AGAIN)."""
    digest = sha256 or hashlib.sha256(data).hexdigest()
    t0 = time.time()
    settled = con.settle(*settle) if settle else None
    con.send(f'\r\ninstall {slot} {len(data)} {digest} {slug}\r\n')
    ready = con.wait_for(['YK_INSTALL_READY', 'YK_INSTALL '], 10)
    if not ready or not ready.startswith('YK_INSTALL_READY'):
        return {'result': ready, 'seconds': round(time.time() - t0, 2), 'settled': settled}
    chunk = int(ready.split('chunk=')[1]) if 'chunk=' in ready else 4096
    sent = 0
    result = None
    dropped = None
    n = 0
    while sent < len(data):
        if stop_after is not None and sent >= stop_after:
            break
        part = data[sent:sent + chunk]
        con.take_waiting()  # nothing the device said left in the port while the chunk goes
        con.s.write(part)
        sent += len(part)
        n += 1
        if drop_ack == n and sent < len(data):
            dropped = con.wait_for(['YK_ACK '], 15)  # as if it never came
        # The last chunk's ACK lost: the device checks the image and says YK_INSTALL OK only once its SHA-256
        # matched; that is the result (固件总策划 11-18). Its check may take a while: as long as the result.
        last = sent == len(data)
        wait = 60 if last else 15
        end = time.time() + wait
        ack = con.wait_for(['YK_ACK ', 'YK_ACK_AGAIN ', 'YK_INSTALL '], wait)
        while ack and 0 <= ack_of(ack) < sent:  # an earlier one said again
            ack = con.wait_for(['YK_ACK ', 'YK_ACK_AGAIN ', 'YK_INSTALL '], max(0, end - time.time()))
        if last and ack and ack.startswith('YK_INSTALL OK'):
            result = ack
        elif ack_of(ack) != sent:
            return {'result': ack, 'sent': sent, 'seconds': round(time.time() - t0, 2), 'settled': settled,
                    'dropped': dropped}
    if result is None:
        result = con.wait_for(['YK_INSTALL '], 60)
    seconds = round(time.time() - t0, 2)
    return {'result': result, 'sent': sent, 'seconds': seconds, 'kbps': round(sent / max(seconds, 0.01) / 1024),
            'settled': settled, 'dropped': dropped}


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) < 3:
        sys.exit(__doc__)
    con = Console(args[0])
    slot, data = int(args[1]), open(args[2], 'rb').read()
    # In the recovery already (this tool's way): a few quiet seconds first, in case it has only just started.
    drop = [int(a.split('=', 1)[1]) for a in sys.argv if a.startswith('--drop-ack=')]
    outcome = install(con, slot, data, args[3] if len(args) > 3 else '', settle=(0, 3, 20),
                      drop_ack=drop[0] if drop else None)
    print(json.dumps({'slot': slot, 'bytes': len(data), **outcome}))
    if not (outcome['result'] or '').startswith('YK_INSTALL OK'):
        sys.exit(1)
    if '--open' in sys.argv:
        con.send(f'yk cmd launcher.switch {{"slot":{slot}}}\r\n')
        print(con.wait_for(['LAUNCHER_SWITCH', 'YK_ERR'], 5))
