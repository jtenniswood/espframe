import { lstat, readFile } from 'node:fs/promises'
import { join } from 'node:path'
import sharp from 'sharp'

export interface ReplayAsset {
  id: string
  width: number
  height: number
  localDateTime: string
}

const uuid = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/
const maxPreviewBytes = 2 * 1024 * 1024
const millisecondsPerDay = 86_400_000

export async function* readCapturedPreviews(directory: string): AsyncGenerator<{ asset: ReplayAsset, bytes: Buffer }> {
  const manifest = join(directory, 'manifest.json')
  const info = await lstat(manifest)
  if (!info.isFile() || info.size > 64 * 1024) throw new Error('Expected a small capture manifest.json file')
  const records: unknown = JSON.parse(await readFile(manifest, 'utf8'))
  if (!Array.isArray(records) || records.length < 2 || records.length > 96 || records.length % 2) {
    throw new Error('The capture manifest must list 1–48 adjacent portrait pairs')
  }

  const seen = new Set<string>()
  let pairDay = -Infinity
  for (const [index, record] of records.entries()) {
    const id: unknown = record?.id
    const localDateTime: unknown = record?.localDateTime
    if (typeof id !== 'string' || !uuid.test(id) || seen.has(id)) {
      throw new Error(`Expected a unique lowercase UUID for capture at position ${index + 1}`)
    }
    const timestamp = typeof localDateTime === 'string' ? Date.parse(localDateTime) : NaN
    if (typeof localDateTime !== 'string' || !Number.isFinite(timestamp) ||
        new Date(timestamp).toISOString() !== localDateTime) {
      throw new Error(`Expected an ISO UTC timestamp for capture at position ${index + 1}`)
    }
    const day = Math.floor(timestamp / millisecondsPerDay)
    if (index % 2 ? day !== pairDay : day < pairDay + 5) {
      throw new Error('Each capture pair needs the same day, at least five days after the previous pair')
    }
    pairDay = day

    const path = join(directory, `${id}.preview`)
    const file = await lstat(path)
    if (!file.isFile() || file.size === 0 || file.size > maxPreviewBytes) {
      throw new Error(`Expected a preview file of at most 2 MiB at position ${index + 1}`)
    }
    const bytes = await readFile(path)
    const { width, height, format, orientation, pages } = await sharp(bytes).metadata()
    if ((format !== 'jpeg' && format !== 'webp') || !width || !height ||
        width >= height || height > 4096 || width * height > 4_000_000 ||
        (orientation && orientation !== 1) || (pages && pages !== 1)) {
      throw new Error(`Expected a supported, upright JPEG or WebP portrait at position ${index + 1}`)
    }
    seen.add(id)
    yield { asset: { id, width, height, localDateTime }, bytes }
  }
}
