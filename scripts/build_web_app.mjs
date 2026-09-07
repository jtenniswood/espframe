import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { build } from "esbuild";

const sourcePath = process.argv[2];
if (!sourcePath) throw new Error("Usage: node scripts/build_web_app.mjs <source.ts>");

const source = await readFile(sourcePath, "utf8");
const result = await build({
  stdin: { contents: source, loader: "ts", sourcefile: "app.ts", resolveDir: resolve("docs/webserver/src") },
  bundle: true,
  write: false,
  format: "iife",
  target: "es2018",
  minify: false,
  legalComments: "none",
});
process.stdout.write(result.outputFiles[0].text);
