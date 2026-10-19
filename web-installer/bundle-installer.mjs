import { build } from "esbuild";
import { cp, mkdir, readdir, readFile, rm, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

process.chdir(fileURLToPath(new URL(".", import.meta.url)));
await rm("dist", { recursive: true, force: true });
await mkdir("dist/assets", { recursive: true });
await cp("static", "dist", { recursive: true });
await cp("serve-installer.mjs", "dist/serve-installer.mjs");
const result = await build({
  entryPoints: ["esp-web-tools/dist/web/install-button.js"],
  bundle: true, splitting: true, format: "esm", minify: true,
  outdir: "dist/assets", entryNames: "installer", chunkNames: "chunk-[hash]",
  metafile: true, legalComments: "linked", target: "es2022",
});
// Ship the licenses of every package included in the browser bundle.
const packages = new Set();
for (const input of Object.keys(result.metafile.inputs)) {
  const match = input.match(/(?:^|\/)node_modules\/((?:@[^/]+\/)?[^/]+)/);
  if (match) packages.add(match[1]);
}
await mkdir("dist/licenses", { recursive: true });
for (const name of packages) {
  const directory = path.join("node_modules", name);
  const info = JSON.parse(await readFile(path.join(directory, "package.json"), "utf8"));
  const notices = (await readdir(directory)).filter((file) => /^(licen[sc]e|copying|notice)(\.|$)/i.test(file));
  if (!notices.length) throw new Error(`No license file found for ${name}`);
  let text = `${name}@${info.version}\nLicense: ${info.license}\n`;
  for (const notice of notices) text += `\n${await readFile(path.join(directory, notice), "utf8")}\n`;
  await writeFile(`dist/licenses/${name.replaceAll("/", "-")}.txt`, text);
}
