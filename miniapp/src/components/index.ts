/**
 * Charts, the calendar and the ring, drawn by the apps' own page renderer (render.js, page-runtime's, copied in), so
 * they look as they do on a service's page in the app. Colours follow the app: call applyEnv (or watchEnv) first.
 * Importing this brings its styles (components.css) into the page's app.css.
 *
 *   list.append(chart({ kind: "bar", title: "这周", points: [{ label: "一", value: 3 }, …] }));
 *   el.innerHTML = ringHtml({ items: [{ label: "专注", value: 40 }, …] });   // the same, as HTML text
 *
 * Every string is escaped by the renderer. A block it cannot draw comes back empty.
 */
import "./render.js";
import "./components.css";

/** The colours a block may use: the app's own, never blue. */
export type Tone = "coral" | "sage" | "sun" | "ink" | "wake" | "focus" | "sleep" | "mine";

/** A bar or line chart, 2–31 points. Stacked bars: name 2–6 `series` and give every point its `parts` in that order. */
export type ChartBlock = {
  kind: "bar" | "line";
  title?: string;
  series?: { label: string; tone?: Tone }[];
  points: { label: string; value: number; parts?: number[] }[];
};
/** Which days, deeper for more: one value a day from `start` (YYYY-MM-DD), oldest first, 7–371 of them. */
export type CalendarBlock = { title?: string; start: string; values: number[]; unit?: string; tone?: "coral" | "sage" | "sun" | "ink" };
/** Shares of a whole, 1–8 items; `center` is what the middle says. */
export type RingBlock = { title?: string; items: { label: string; value: number; tone?: Tone }[]; center?: { value: string; label: string } };
/** The calendar's legend, in the page's language (the renderer's own is Chinese: 少 … 多). */
export type CalendarWords = { less: string; more: string };

type Renderer = { render(spec: unknown): string };
// render.js puts itself on `window` (globalThis in a browser).
const renderer = () => ((typeof window !== "undefined" ? window : globalThis) as unknown as { YayPage: Renderer }).YayPage;

function draw(type: string, block: object): string {
  const html = renderer().render({ spec: "yay.page/v1", title: "", blocks: [{ ...block, type }] });
  const section = /<section class="blk b-[a-z]+">([\s\S]*)<\/section>/.exec(html);
  return section ? `<div class="tk-block">${section[1]}</div>` : "";
}

function element(html: string): HTMLElement {
  const wrap = document.createElement("div");
  wrap.innerHTML = html;
  return (wrap.firstElementChild as HTMLElement | null) ?? wrap;
}

const escape = (text: string) => text.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]!);

export const chartHtml = (block: ChartBlock) => draw("chart", block);
export const ringHtml = (block: RingBlock) => draw("ring", block);
export function calendarHtml(block: CalendarBlock, words?: CalendarWords): string {
  const html = draw("calendar", block);
  if (!words) return html;
  return html.replace(/<span>少<\/span>/, `<span>${escape(words.less)}</span>`).replace(/<span>多/, `<span>${escape(words.more)}`);
}

export const chart = (block: ChartBlock) => element(chartHtml(block));
export const ring = (block: RingBlock) => element(ringHtml(block));
export const calendar = (block: CalendarBlock, words?: CalendarWords) => element(calendarHtml(block, words));
