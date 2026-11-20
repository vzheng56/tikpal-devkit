/** A refusal the pretend bridge answers with: the bridge's error, as the app would send it. */
import type { FakeError } from "./types.js";

export class Failure {
  constructor(readonly error: FakeError) {}
}
export const fail = (code: FakeError["code"], extra: Omit<FakeError, "code"> = {}) => new Failure({ code, ...extra });
