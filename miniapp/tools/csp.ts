/**
 * The CSP every 小程序 runs under is the contract's MINIAPP_CSP (the container doc §3.1): the containers send it as a
 * header and the entry page writes it again. The template's index.html writes it, the preview sends it, and the check
 * holds the entry page to it here.
 */
import { MINIAPP_CSP } from "../src/contract/miniapps.js";

export { MINIAPP_CSP };

/** A CSP as directive → its values, names lowercased. */
export function parseCsp(policy: string): Map<string, string[]> {
  const out = new Map<string, string[]>();
  for (const part of policy.split(";")) {
    const [name, ...values] = part.trim().split(/\s+/);
    if (name) out.set(name.toLowerCase(), values);
  }
  return out;
}

/**
 * Where a page's own CSP is looser than MINIAPP_CSP: each of its directives must be there with the same values, or
 * 'none'; a directive it adds must be 'none' (base-uri 'none' is fine). Nothing when it is as tight.
 */
export function looserThan(policy: string): string[] {
  const page = parseCsp(policy);
  const out: string[] = [];
  for (const [name, values] of parseCsp(MINIAPP_CSP)) {
    const own = page.get(name);
    const same = own && [...own].sort().join(" ") === [...values].sort().join(" ");
    if (!same && own?.join(" ") !== "'none'") out.push(`${name} 要写 ${values.join(" ")}${own ? `（现在是 ${own.join(" ") || "空的"}）` : "（没写）"}`);
  }
  for (const [name, values] of page) if (!parseCsp(MINIAPP_CSP).has(name) && values.join(" ") !== "'none'") out.push(`${name} ${values.join(" ")} 多放开了：去掉，或者写 'none'`);
  return out;
}

/**
 * The entry page with the contract's CSP in its <meta>, as the build writes it (and the preview serves it): a page's
 * own policy is replaced, a page without one gets it at the top of <head>. So no page copies the string by hand.
 */
export function withCsp(html: string): string {
  const meta = `<meta http-equiv="Content-Security-Policy" content="${MINIAPP_CSP}" />`;
  const own = /<meta\b[^>]*http-equiv\s*=\s*["']?content-security-policy["']?[^>]*>/i;
  if (own.test(html)) return html.replace(own, meta);
  const charset = /<meta\s+charset[^>]*>/i.exec(html);
  if (charset) return html.slice(0, charset.index + charset[0].length) + `\n    ${meta}` + html.slice(charset.index + charset[0].length);
  return html.replace(/<head[^>]*>/i, (head) => `${head}\n    ${meta}`);
}
