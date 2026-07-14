import { readFile } from "node:fs/promises";
import { transform } from "esbuild";

const sourcePath = process.argv[2];
if (!sourcePath) throw new Error("Usage: node scripts/build_web_app.mjs <source.ts>");

const source = await readFile(sourcePath, "utf8");
const result = await transform(source, {
  loader: "ts",
  target: "es2018",
  minify: false,
  legalComments: "none",
});

process.stdout.write(result.code);
