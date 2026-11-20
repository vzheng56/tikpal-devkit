// 生成的，别手改：packages/contracts/src/device-link.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * 设备通道 (level 3, `device.link`): a page talks to its own app on the user's own Tikpal Voice, over Bluetooth,
 * through the phone app, which owns the connection, the key and the encryption. The page sends and receives its app's
 * messages (device.link.*: at most LINK.frame.appMaxBytes each, at most LINK.frame.appPerSecond a second) and sees no
 * other device. The app on the device uses yaykit_link's unpaired mode (`bluetooth_unpaired` in app.json).
 */

/** 128-bit UUIDs differ in one byte: e18d52XX-0266-4765-8f37-25449812d4a9 (the draft's, kept). */
const uuid = (x: string) => `e18d52${x}-0266-4765-8f37-25449812d4a9`;

export const LINK = {
  ble: {
    service: uuid("50"),
    /** write: LinkOpen with response, then envelopes without. */
    frames: uuid("51"),
    /** notify: the device's envelopes. */
    notify: uuid("52"),
    /** read: the device's 8 random bytes, its kid, the app's number (linkSession). */
    session: uuid("53"),
    nonceBytes: 8,
    /** The scan response's short name. */
    name: "Tikpal",
    /** The device asks for this; what a write may hold is the phone's MTU − 3. */
    preferredMtu: 517,
  },
  mark: { bits: 20, label: "yay-mark-v1", serviceDataBytes: 5 },
  key: { bytes: 32, kidBytes: 4 },
  crypto: {
    alg: "AES-128-GCM",
    sessionKeyBytes: 16,
    counterBytes: 4,
    tagBytes: 16,
    /** The nonce's first byte: which way the envelope goes (the two never share a nonce). */
    toDevice: 1,
    toPhone: 2,
    sessionInfo: "tikpal-link-session-v1",
  },
  frame: {
    version: 1,
    /** One write, envelope included: under the 182 bytes an iPhone writes at the usual 185-byte MTU. */
    maxBytes: 180,
    /** A phone that may write less than this is not supported. */
    minWriteBytes: 108,
    /** An app's message: what still fits minWriteBytes with the envelope and the frame's two bytes. */
    appMaxBytes: 86,
    appPerSecond: 10,
  },
  kind: { open: 2, app: 4 },
  /** ATT application errors on LinkOpen. */
  openErrors: { keyMismatch: 0x80, busy: 0x81 },
  /** Asking the platform again after a mismatch, at most once a minute for a device. */
  refetchSeconds: 60,
  /** Envelopes in a row the device drops (before LinkOpen, or not opening) before it disconnects. */
  badEnvelopes: 5,
} as const;
