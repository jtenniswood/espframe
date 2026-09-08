export type SettingValue = string | number | boolean;

interface PendingSetting {
  confirmed: SettingValue;
  latest: number;
  remaining: number;
}

// Serialize complete writes, including legacy fallbacks, and keep newer drafts
// intact when an older request fails. A failed latest draft restores the most
// recent successful value, rather than another unconfirmed draft.
export class SettingSaveCoordinator {
  private sequence = 0;
  private queue: Promise<unknown> = Promise.resolve();
  private confirmed = new Map<string, SettingValue>();
  private pending = new Map<string, PendingSetting>();

  constructor(
    private read: (key: string) => SettingValue,
    private write: (key: string, value: SettingValue) => void,
  ) {}

  receive(key: string, value: SettingValue): void {
    // Live events/snapshots can arrive after a user has started a newer draft.
    if (this.pending.has(key)) return;
    this.confirmed.set(key, value);
    this.write(key, value);
  }

  save<T>(values: Record<string, SettingValue>, send: () => Promise<T>): Promise<T> {
    const revision = ++this.sequence;
    const entries = Object.entries(values);
    for (const [key, value] of entries) {
      let pending = this.pending.get(key);
      if (!pending) {
        pending = { confirmed: this.confirmed.has(key) ? this.confirmed.get(key)! : this.read(key), latest: revision, remaining: 0 };
        this.pending.set(key, pending);
      }
      pending.latest = revision;
      pending.remaining++;
      this.write(key, value);
    }
    const request = this.queue.then(send).then(
      result => { this.finish(entries, revision, true); return result; },
      error => { this.finish(entries, revision, false); throw error; },
    );
    this.queue = request.catch(() => undefined);
    return request;
  }

  private finish(entries: [string, SettingValue][], revision: number, accepted: boolean): void {
    for (const [key, value] of entries) {
      const pending = this.pending.get(key)!;
      if (accepted) {
        pending.confirmed = value;
        this.confirmed.set(key, value);
      }
      if (pending.latest === revision) this.write(key, pending.confirmed);
      if (--pending.remaining === 0) this.pending.delete(key);
    }
  }
}
