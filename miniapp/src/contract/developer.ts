// 生成的，别手改：packages/contracts/src/developer.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * The kit's sign-in to developer.tikpal.ai (tikpal-miniapp login, preview): the developer allows a code on the site,
 * the kit gets a token that only puts a 小程序 up as the developer's own dev version and lists its versions.
 */
import { z } from "zod";

const at = z.iso.datetime({ offset: true });

/**
 * An invite code as Yaykit makes it: three groups of four from an alphabet without 0/O, 1/I/L (TKP2-7QXD-M4RA). What is
 * typed is read leniently: lower case, spaces and dashes anywhere.
 */
export const INVITE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";

// ——— The kit on the developer's computer: signing in by code, a token for uploads ———

/**
 * `./kit miniapp login` (and `./kit miniapp preview` the first time) signs the kit in as the developer, the way a TV
 * does: the kit asks for a code (cli/start), shows it with developer.tikpal.ai/cli, and asks every `interval` seconds
 * (cli/token) until the developer, signed in on the site, allows it (cli/approve), or the code runs out. The token
 * (`Authorization: Bearer ykd_…`) does only what the kit does: put a 小程序 up as the developer's dev version and list
 * its versions (miniapps.ts MINIAPP_UPLOAD_PATH, POST and GET), and say whose it is (cli/me); every other route refuses
 * it, and those routes still ask Yaykit whose app it is. The platform keeps only its SHA-256; the developer sees each
 * computer signed in and can sign it out (cli/tokens), and deleting the account ends them all.
 */
export const DEVELOPER_CLI = {
  /** How long a code waits to be allowed. */
  codeMinutes: 10,
  /** How often the kit asks, in seconds. */
  interval: 5,
  /** How long a token works. */
  tokenDays: 30,
  /** Computers signed in at once, per developer; signing in another ends the oldest. */
  tokensMax: 10,
  /** What a token may do. */
  scopes: ["miniapp.dev"],
} as const;
/** The code the developer types or opens: eight of INVITE_ALPHABET as XXXX-XXXX; typed leniently. */
export const cliUserCode = z
  .string()
  .max(20)
  .transform((typed) => typed.toUpperCase().replace(/[\s-]/g, ""))
  .pipe(z.string().regex(new RegExp(`^[${INVITE_ALPHABET}]{8}$`)))
  .transform((code) => `${code.slice(0, 4)}-${code.slice(4)}`);
const deviceCode = z.string().regex(/^[A-Za-z0-9_-]{43}$/);
export const cliToken = z.string().regex(/^ykd_[A-Za-z0-9_-]{43}$/);
/** The computer, as the kit names it (its host name), shown on the page that allows it and in the list. */
export const cliStartInput = z.object({ name: z.string().trim().min(1).max(60) }).strict();
export const cliStart = z
  .object({ deviceCode, userCode: cliUserCode, expiresIn: z.number().int().positive(), interval: z.number().int().positive() })
  .strict();
export const cliTokenInput = z.object({ deviceCode }).strict();
export const cliIssued = z.object({ token: cliToken, expiresAt: at, developer: z.object({ name: z.string().max(60) }).strict() }).strict();
export const cliMe = z.object({ developer: z.object({ name: z.string().max(60) }).strict(), expiresAt: at }).strict();
