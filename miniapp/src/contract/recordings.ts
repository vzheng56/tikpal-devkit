// 生成的，别手改：packages/contracts/src/recordings.ts 里开发包用得到的那些（scripts/sync.ts KEPT），别的不进公开包。改那份，再跑 pnpm miniapp:sync。
/**
 * 录音卡 in the apps (docs/delivery/2026-10-30-recording-card-0.2.md §3 给 App 的接口, §4): the recordings the
 * account's round screens made, the files of one recording.id shown as one, newest first. The round screen sends them
 * (device-api.ts FILE_STREAM); the service joins, transcribes and keeps them; the apps list, read, play and delete.
 *
 *   GET    /v1/recordings?cursor=        RecordingList: a page, and 今天的点数还够转约 N 分钟 (transcribeMinutes)
 *   GET    /v1/recordings/:id            RecordingText: the text, once a part is transcribed
 *   POST   /v1/recordings/:id/transcribe RecordingAnswer: 再转一次, only while `retry`
 *   GET    /v1/recordings/:id/audio      RecordingAudio: where to play it from, only while `audio`, the owner only
 *   DELETE /v1/recordings/:id            RecordingDeleted: the audio, the text and its Feed post go together
 *
 * Errors: 404 RECORDING_NOT_FOUND (none, or another account's); 409 AUDIO_GONE (no audio to play or transcribe again:
 * 30 days passed, or it was cleared for space); 409 NOT_FAILED (再转一次 on one that is not failed); 402
 * CREDITS_EXHAUSTED (再转一次 without the points: it stays failed, nothing is charged).
 *
 * The audio is kept 30 days (audioDays) in its own room, apart from the chat files: past spaceBytes the oldest audio
 * whose text is done goes first, without a word; audio still untranscribed is never cleared, and the round screen is
 * turned away (413 STORAGE_LIMIT) only past hardBytes. The text stays until the user deletes the recording.
 */
import { z } from "zod";

export const RECORDINGS = {
  /** Items in a page. */
  pageSize: 20,
  /** Characters of the opening lines the list shows. */
  previewMax: 120,
  /** How long the audio is kept, and its room per account (chat files have their own). */
  audioDays: 30,
  spaceBytes: 500_000_000,
  hardBytes: 1_000_000_000,
  /** How long a link to the audio works; ask again after. */
  audioUrlSeconds: 3600,
  /**
   * While uploading, a chunk this recent means it is coming in now (在传); older, the round screen has stopped for now
   * (「还没传完，在圆屏上打开录音卡就会接着传」: it goes on only in 录音卡, and after the battery ran out it wakes in Yay).
   */
  activeSeconds: 120,
} as const;

const at = z.iso.datetime({ offset: true });
/** As the round screen names it (device-api.ts recording.id). */
export const recordingId = z.string().regex(/^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$/);
const segment = z.number().int().min(1).max(1000);
const milliseconds = z.number().int().nonnegative();

/**
 * uploading: the round screen has not said 完了 for every file yet (the service finishes it itself after 7 days, from
 * what it has); transcribing; done; failed, `failure` saying why.
 */
export const recordingStates = ["uploading", "transcribing", "done", "failed"] as const;
/** 没转成: 点数不够 (credits), 太长 (too_long), 其他 (other). */
export const recordingFailures = ["credits", "too_long", "other"] as const;

/** One recording in the list. */
export const recording = z
  .object({
    id: recordingId,
    /** When it began, by the round screen's clock; null when that clock was not set (sort by createdAt then). */
    recordedAt: at.nullable(),
    createdAt: at,
    /** All its files together, so far while uploading. */
    durationMs: milliseconds,
    files: z.number().int().min(1),
    state: z.enum(recordingStates),
    /** While uploading: when the last chunk came (see RECORDINGS.activeSeconds); null before the first. */
    lastChunkAt: at.nullable(),
    failure: z.enum(recordingFailures).nullable(),
    /** Done, and nobody was heard in it: 「转好了，没听到说话」. */
    noSpeech: z.boolean(),
    /** Written from the text; null until then. */
    title: z.string().max(80).nullable(),
    /** The opening lines; null until there is text. */
    preview: z.string().max(RECORDINGS.previewMax).nullable(),
    /** The audio is still kept (joined, under 30 days, not cleared for space): it plays, and can be transcribed again. */
    audio: z.boolean(),
    audioExpiresAt: at.nullable(),
    /** 再转一次: failed, and the audio still kept. */
    retry: z.boolean(),
    /** The points its transcription cost, all files together; null until charged. */
    charged: z.number().int().nonnegative().nullable(),
  })
  .strict();
export type Recording = z.infer<typeof recording>;

/** GET /v1/recordings. */
export const recordingList = z
  .object({
    items: z.array(recording).max(RECORDINGS.pageSize),
    next: z.string().nullable(),
    /**
     * 今天的点数还够转约 N 分钟: the points the account may spend now (what is left today and what was given) over a
     * minute's transcription, rounded down; 0 is 「今天的点数不够转了」. The apps show it as it is and never price it
     * themselves; asked again on opening the page, after a recording is done and after 再转一次.
     */
    transcribeMinutes: z.number().int().nonnegative(),
  })
  .strict();
export type RecordingList = z.infer<typeof recordingList>;

/** GET /v1/recordings/:id: each file's text in order (null where it is not transcribed), and all of it joined. */
export const recordingText = z
  .object({
    id: recordingId,
    title: z.string().max(80).nullable(),
    /** The parts with text, joined by a blank line. */
    text: z.string(),
    parts: z
      .array(z.object({ segment, durationMs: milliseconds, text: z.string().nullable() }).strict())
      .min(1),
  })
  .strict();
export type RecordingText = z.infer<typeof recordingText>;

/** POST /v1/recordings/:id/transcribe: the recording as it is now (transcribing), and the minutes left. */
export const recordingAnswer = z.object({ recording, transcribeMinutes: z.number().int().nonnegative() }).strict();
export type RecordingAnswer = z.infer<typeof recordingAnswer>;

/** DELETE /v1/recordings/:id. */
export const recordingDeleted = z.object({ deleted: z.literal(true) }).strict();

export const recordingErrors = ["RECORDING_NOT_FOUND", "AUDIO_GONE", "NOT_FAILED", "CREDITS_EXHAUSTED"] as const;
