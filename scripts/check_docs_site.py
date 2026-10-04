#!/usr/bin/env python3
"""Validate rendered SEO metadata, sitemap, FAQ schema, and local links."""

from __future__ import annotations

import html.parser
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "docs/.vitepress/dist"
BASE_URL = "https://jtenniswood.github.io/espframe/"
BASE_PATH = "/espframe"
NON_SEARCHABLE = {
    "phase-1-product-metadata",
    "phase-3-reset-architecture",
    "phase-4-release-proven-architecture",
    "repository-governance",
    "reset-architecture-v2",
    "source-ownership",
    "tech-debt-cleanup-backlog",
}


class PageParser(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.title = ""
        self.in_title = False
        self.in_script = False
        self.script_type = ""
        self.script_data = ""
        self.description = ""
        self.canonical = ""
        self.robots = ""
        self.meta: dict[str, str] = {}
        self.ids: set[str] = set()
        self.links: list[str] = []
        self.visible_text = ""
        self.json_ld: list[dict] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attrs_dict = dict(attrs)
        if tag == "title":
            self.in_title = True
        if attrs_dict.get("id"):
            self.ids.add(attrs_dict["id"] or "")
        if tag == "a" and attrs_dict.get("href"):
            self.links.append(attrs_dict["href"] or "")
        if tag == "meta":
            key = attrs_dict.get("name") or attrs_dict.get("property")
            if key:
                self.meta[key] = attrs_dict.get("content") or ""
            if attrs_dict.get("name") == "description":
                self.description = attrs_dict.get("content") or ""
            if attrs_dict.get("name") == "robots":
                self.robots = attrs_dict.get("content") or ""
        if tag == "link" and attrs_dict.get("rel") == "canonical":
            self.canonical = attrs_dict.get("href") or ""
        if tag == "script":
            self.in_script = True
            self.script_type = attrs_dict.get("type") or ""
            self.script_data = ""

    def handle_endtag(self, tag: str) -> None:
        if tag == "title":
            self.in_title = False
        if tag == "script" and self.in_script:
            if self.script_type == "application/ld+json":
                try:
                    self.json_ld.append(json.loads(self.script_data))
                except json.JSONDecodeError:
                    pass
            self.in_script = False

    def handle_data(self, data: str) -> None:
        if self.in_title:
            self.title += data
        if self.in_script:
            self.script_data += data
        else:
            self.visible_text += data


def page_slug(path: Path) -> str:
    relative = path.relative_to(DIST).with_suffix("")
    if relative.as_posix() == "index":
        return ""
    return relative.parent.as_posix() if relative.name == "index" else relative.as_posix()


def route_for_page(path: Path) -> str:
    slug = page_slug(path)
    return BASE_URL if not slug else BASE_URL + slug


def main() -> int:
    if not DIST.is_dir():
        print(f"Docs build output not found: {DIST}", file=sys.stderr)
        return 1

    pages = {
        page_slug(path): path
        for path in DIST.rglob("*.html")
        if path.relative_to(DIST).as_posix() != "404.html"
    }
    errors: list[str] = []
    titles: dict[str, str] = {}
    descriptions: dict[str, str] = {}
    parsed: dict[str, PageParser] = {}

    for slug, path in pages.items():
        parser = PageParser()
        parser.feed(path.read_text(encoding="utf-8"))
        parsed[slug] = parser
        if not parser.title.strip():
            errors.append(f"{path.name}: missing title")
        elif parser.title in titles:
            errors.append(f"{path.name}: duplicate title also used by {titles[parser.title]}")
        else:
            titles[parser.title] = path.name
        if not parser.description.strip():
            errors.append(f"{path.name}: missing meta description")
        elif parser.description in descriptions:
            errors.append(f"{path.name}: duplicate description also used by {descriptions[parser.description]}")
        else:
            descriptions[parser.description] = path.name
        if parser.canonical != route_for_page(path):
            errors.append(f"{path.name}: unexpected canonical URL {parser.canonical!r}")
        for key in ("og:title", "og:description", "og:url", "twitter:title", "twitter:description"):
            if not parser.meta.get(key, "").strip():
                errors.append(f"{path.name}: missing {key} metadata")
        if slug in NON_SEARCHABLE and "noindex" not in parser.robots.lower():
            errors.append(f"{path.name}: engineering page is missing noindex metadata")

    sitemap_path = DIST / "sitemap.xml"
    try:
        sitemap_root = ET.parse(sitemap_path).getroot()
        sitemap_urls = {node.text or "" for node in sitemap_root.findall(".//{*}loc")}
    except (ET.ParseError, OSError) as exc:
        errors.append(f"sitemap.xml: could not parse sitemap ({exc})")
        sitemap_urls = set()

    expected_urls = {
        route_for_page(path)
        for slug, path in pages.items()
        if slug not in NON_SEARCHABLE
    }
    if expected_urls - sitemap_urls:
        errors.append("sitemap.xml: missing canonical pages: " + ", ".join(sorted(expected_urls - sitemap_urls)))
    if sitemap_urls - expected_urls:
        errors.append("sitemap.xml: unexpected pages: " + ", ".join(sorted(sitemap_urls - expected_urls)))

    for slug, parser in parsed.items():
        source = pages[slug].relative_to(DIST).as_posix()
        for href in parser.links:
            target = urlsplit(href)
            if target.scheme or target.netloc:
                continue
            path = unquote(target.path)
            if not path:
                target_slug = slug
            elif path == BASE_PATH or path == BASE_PATH + "/":
                target_slug = ""
            elif path.startswith(BASE_PATH + "/"):
                target_slug = path[len(BASE_PATH) + 1:].strip("/")
            else:
                continue
            if target_slug in pages:
                if target.fragment and unquote(target.fragment) not in parsed[target_slug].ids:
                    errors.append(f"{source}: internal link has no anchor {href}")
            elif target_slug:
                local_file = DIST / target_slug
                if not local_file.is_file():
                    errors.append(f"{source}: internal link has no rendered page or asset: {href}")

    faq = parsed.get("faq")
    if not faq:
        errors.append("faq.html: rendered FAQ page is missing")
    else:
        faq_schema = next((schema for schema in faq.json_ld if schema.get("@type") == "FAQPage"), None)
        if not faq_schema or not faq_schema.get("mainEntity"):
            errors.append("faq.html: missing FAQPage structured data")
        else:
            for question in faq_schema["mainEntity"]:
                name = question.get("name", "")
                answer = question.get("acceptedAnswer", {}).get("text", "")
                if name not in faq.visible_text or answer not in faq.visible_text:
                    errors.append(f"faq.html: structured answer is not visible: {name}")

    ai_txt = DIST / "ai.txt"
    if not ai_txt.is_file() or not ai_txt.read_text(encoding="utf-8").strip():
        errors.append("ai.txt: missing or empty AI discovery file")
    elif "https://jtenniswood.github.io/espframe/faq" not in ai_txt.read_text(encoding="utf-8"):
        errors.append("ai.txt: missing FAQ discovery link")
    if (DIST / "robots.txt").exists():
        errors.append("robots.txt: remove project-path policy; publish the host-root template separately")
    robots_template = ROOT / "docs/.vitepress/hosting/robots.txt"
    if not robots_template.is_file() or "Sitemap: https://jtenniswood.github.io/espframe/sitemap.xml" not in robots_template.read_text(encoding="utf-8"):
        errors.append("host-root robots template is missing or has no project sitemap")

    if errors:
        print("Docs discovery check failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(
        f"Docs discovery check passed: {len(pages)} canonical pages, {len(sitemap_urls)} sitemap entries, "
        "unique metadata, valid internal links, and matching visible FAQ structured data."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
