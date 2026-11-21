import sharp from 'sharp'

export type SlideLayout = { mode: 'fit' | 'fill' | 'fill-frame'; rotation: 0 | 180; orientation?: 'portrait' | 'landscape' }

export class OrientationMismatch extends Error {}

const width = 1280
const height = 800
const background = '#000000'
const inputOptions = { failOn: 'warning', limitInputPixels: 4_000_000 } as const

async function orient(preview: Buffer) {
  const image = sharp(preview, inputOptions)
  const metadata = await image.metadata()
  if ((metadata.format !== 'jpeg' && metadata.format !== 'webp') || (metadata.pages ?? 1) > 1) {
    throw new Error('Expected a still JPEG or WebP preview')
  }
  return image.autoOrient().flatten({ background }).toColourspace('srgb').raw().toBuffer({ resolveWithObject: true })
}

async function tile(image: Awaited<ReturnType<typeof orient>>, left: number, maxWidth: number, fill: boolean,
                    align: 'left' | 'center' | 'right' = 'center') {
  const { width: w, height: h } = image.info
  const wide = w * height >= h * maxWidth
  let targetWidth = maxWidth
  let targetHeight = height
  let cropWidth = w
  let cropHeight = h
  if (fill) {
    cropWidth = wide ? Math.max(1, Math.floor(h * maxWidth / height)) : w
    cropHeight = wide ? h : Math.max(1, Math.floor(w * height / maxWidth))
  } else {
    targetWidth = wide ? maxWidth : Math.max(1, Math.floor(w * height / h))
    targetHeight = wide ? Math.max(1, Math.floor(h * maxWidth / w)) : height
  }
  const input = await sharp(image.data, { raw: image.info }).extract({
    left: Math.floor((w - cropWidth) / 2), top: Math.floor((h - cropHeight) / 2), width: cropWidth, height: cropHeight,
  }).resize(targetWidth, targetHeight, { fit: 'fill' }).raw().toBuffer({ resolveWithObject: true })
  const horizontalSpace = maxWidth - targetWidth
  let offset = 0
  if (align === 'right') offset = horizontalSpace
  else if (align === 'center') offset = Math.floor(horizontalSpace / 2)
  return {
    input: input.data,
    raw: input.info,
    left: left + offset,
    top: Math.floor((height - targetHeight) / 2),
  }
}

export async function prepareSlide(primary: Buffer, companion: Buffer | undefined, layout: SlideLayout) {
  const left = await orient(primary)
  const portrait = left.info.height > left.info.width
  if (layout.orientation && portrait !== (layout.orientation === 'portrait')) {
    throw new OrientationMismatch('Preview does not match the requested orientation')
  }
  const fill = layout.mode !== 'fit'
  const layers = []
  if (companion) {
    const right = await orient(companion)
    if (!portrait || right.info.height <= right.info.width) {
      throw new Error('A portrait pair requires two portrait previews')
    }
    const half = width / 2
    layers.push(await tile(left, 0, half, fill, 'right'), await tile(right, half, half, fill, 'left'))
  } else {
    layers.push(await tile(left, 0, width, layout.mode === 'fill-frame' || (fill && !portrait)))
  }
  const logical = await sharp({ create: { width, height, channels: 3, background } })
    .composite(layers).raw().toBuffer({ resolveWithObject: true })
  // Match panel_offset() in slide_geometry.h. Rotate after the layers are composed.
  return sharp(logical.data, { raw: logical.info }).rotate(layout.rotation === 180 ? 270 : 90)
    .jpeg({ quality: 82, chromaSubsampling: '4:2:0', progressive: false }).toBuffer()
}
