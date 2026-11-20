import { createSlideService } from './service.ts'

const { IMMICH_URL, IMMICH_API_KEY, SLIDE_SERVICE_TOKEN, HOST = '127.0.0.1', PORT = '8080' } = process.env
if (!IMMICH_URL || !IMMICH_API_KEY || !SLIDE_SERVICE_TOKEN) {
  throw new Error('Set IMMICH_URL, IMMICH_API_KEY and SLIDE_SERVICE_TOKEN')
}
const port = Number(PORT)
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Set PORT to a valid TCP port')

createSlideService({ immichUrl: IMMICH_URL, immichKey: IMMICH_API_KEY, token: SLIDE_SERVICE_TOKEN })
  .listen(port, HOST, () => console.log(`Prepared slides listening on ${HOST}:${port}`))
