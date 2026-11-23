import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { build, transform } from "esbuild";

const sourcePath = process.argv[2];
if (!sourcePath) throw new Error("Usage: node scripts/build_web_app.mjs <source> [--embedded-css]");

const source = await readFile(sourcePath, "utf8");
if (process.argv[3] === "--embedded-css") {
  const result = await transform(source, { loader: "css", minifyWhitespace: true, minifySyntax: true, target: "es2018" });
  process.stdout.write(result.code);
} else {
  const result = await build({
    stdin: { contents: source, loader: "ts", sourcefile: "app.ts", resolveDir: resolve("docs/webserver/src") },
    bundle: true,
    write: false,
    format: "iife",
    target: "es2018",
    // Keep the generated product contract declarations in the entry readable
    // by the compatibility checker while compacting the imported helpers.
    plugins: [{
      name: "compact-web-helpers",
      setup(builder) {
        builder.onLoad({ filter: /[\\/]docs[\\/]webserver[\\/]src[\\/].+\.ts$/ }, async ({ path }) => {
          const helper = await readFile(path, "utf8");
          const result = await transform(helper, { loader: "ts", target: "es2018", minifySyntax: true, minifyIdentifiers: true });
          return { contents: result.code, loader: "js" };
        });
      },
    }],
    legalComments: "none",
  });
  process.stdout.write(result.outputFiles[0].text);
}
