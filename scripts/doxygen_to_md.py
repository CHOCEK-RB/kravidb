#!/usr/bin/env python3
"""Genera la documentacion Markdown a partir del XML de Doxygen.

Lee los compuestos XML que Doxygen deja en `docs/xml` (ver `Doxyfile`) y
escribe un fichero Markdown por modulo, namespace, clase y struct en
`docs/md`, ademas del indice `docs/md/index.md`.

Resuelve los `refid` de Doxygen para enlazar simbolos entre paginas, de modo
que no queden enlaces rotos.

Uso:
    python3 scripts/doxygen_to_md.py [--xml DIR] [--out DIR]
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Etiqueta legible por clase de compuesto.
COMPOUND_KIND_LABEL = {
    "class": "Clase",
    "struct": "Struct",
    "namespace": "Namespace",
    "module": "Modulo",
    "file": "Fichero",
}

# Titulo de seccion por `kind` de `sectiondef`.
SECTION_LABEL = {
    "public-func": "Funciones publicas",
    "protected-func": "Funciones protegidas",
    "private-func": "Funciones privadas",
    "public-static-func": "Funciones estaticas publicas",
    "protected-static-func": "Funciones estaticas protegidas",
    "private-static-func": "Funciones estaticas privadas",
    "public-attrib": "Atributos publicos",
    "protected-attrib": "Atributos protegidos",
    "private-attrib": "Atributos privados",
    "public-static-attrib": "Atributos estaticos publicos",
    "private-static-attrib": "Atributos estaticos privados",
    "public-type": "Tipos publicos",
    "private-type": "Tipos privados",
    "typedef": "Alias de tipo",
    "enum": "Enumeraciones",
    "func": "Funciones",
    "var": "Variables",
    "friend": "Amigos",
    "define": "Macros",
}

# Compuestos que generan una pagina propia.
PAGE_KINDS = ("module", "namespace", "class", "struct")

# Sufijo `_1<hex>` que Doxygen anade a los `refid` de miembros.
_MEMBER_SUFFIX = re.compile(r"_1[0-9a-f]{6,}$")


def flat(element: ET.Element | None) -> str:
    """Texto plano de un elemento, con los espacios colapsados."""
    if element is None:
        return ""
    return re.sub(r"\s+", " ", "".join(element.itertext())).strip()


def escape_text(text: str) -> str:
    """Escapa caracteres que Markdown interpretaria como formato."""
    text = text.replace("&", "&amp;")
    text = text.replace("<", "&lt;").replace(">", "&gt;")
    text = text.replace("\\", "\\\\")
    for char in ("`", "*", "_", "|", "[", "]"):
        text = text.replace(char, "\\" + char)
    return text


def code_span(text: str) -> str:
    """Envueltve texto en un `code span` de Markdown."""
    return "`" + text.replace("`", "'") + "`"


class Converter:
    def __init__(self, xml_dir: Path, out_dir: Path) -> None:
        self.xml_dir = xml_dir
        self.out_dir = out_dir
        # refid -> Element de `<compounddef>`.
        self.compounds: dict[str, ET.Element] = {}
        # refid -> nombre de fichero Markdown (solo para PAGE_KINDS).
        self.files: dict[str, str] = {}
        self.ordered: list[str] = []

    # -- Carga ------------------------------------------------------------
    def load(self) -> None:
        for path in sorted(self.xml_dir.glob("*.xml")):
            if path.name in ("index.xml", "Doxyfile.xml"):
                continue
            try:
                root = ET.parse(path).getroot()
            except ET.ParseError:
                continue
            compound = root.find("compounddef")
            if compound is None:
                continue
            refid = compound.get("id")
            if refid:
                self.compounds[refid] = compound
        self._assign_filenames()

    def _assign_filenames(self) -> None:
        counts: dict[str, int] = {}
        for refid, compound in self.compounds.items():
            kind = compound.get("kind")
            if kind not in PAGE_KINDS:
                continue
            name = flat(compound.find("compoundname"))
            slug = name.replace("::", ".").replace(":", "-")
            if kind == "module":
                slug = "module." + slug
            elif kind == "namespace":
                slug = "ns." + slug
            base = slug or refid
            # Desambigua colisiones de nombre.
            if base in counts:
                counts[base] += 1
                base = f"{base}.{counts[base]}"
            else:
                counts[base] = 1
            self.files[refid] = base + ".md"
            self.ordered.append(refid)

    # -- Enlaces ----------------------------------------------------------
    def link(self, refid: str) -> tuple[str, bool]:
        """Devuelve (destino, es_enlace) para un `refid` de Doxygen."""
        if refid in self.files:
            return self.files[refid], True
        stripped = _MEMBER_SUFFIX.sub("", refid)
        if stripped in self.files:
            return self.files[stripped], True
        return "", False

    # -- Render inline ----------------------------------------------------
    def inline(self, element: ET.Element | None) -> str:
        """Renderiza el contenido de un elemento como Markdown en linea."""
        if element is None:
            return ""
        parts: list[str] = []

        def walk(node: ET.Element) -> None:
            if node.text:
                parts.append(escape_text(node.text))
            for child in node:
                tag = child.tag
                if tag == "ref":
                    target, ok = self.link(child.get("refid", ""))
                    inner = self.inline(child)
                    if ok:
                        parts.append(f"[{inner}]({target})")
                    else:
                        parts.append(inner)
                elif tag in ("emphasis", "emph"):
                    parts.append("*" + self.inline(child).strip() + "*")
                elif tag == "bold":
                    parts.append("**" + self.inline(child).strip() + "**")
                elif tag in ("computeroutput", "code"):
                    parts.append(code_span(flat(child)))
                elif tag == "ulink":
                    parts.append(f"[{self.inline(child).strip()}]({child.get('url', '')})")
                elif tag in ("itemizedlist", "orderedlist"):
                    parts.append(self.render_list(child))
                elif tag in ("programlisting", "verbatim"):
                    parts.append(self.render_code(child))
                elif tag == "linebreak":
                    parts.append("  \n")
                elif tag in ("sp", "nbsp"):
                    parts.append(" ")
                elif tag in ("parameterlist", "simplesect", "parameteritem"):
                    parts.append(flat(child))
                elif tag in ("anchor",):
                    pass
                else:
                    parts.append(self.inline(child))
                if child.tail:
                    parts.append(escape_text(child.tail))

        walk(element)
        return re.sub(r"  +", " ", "".join(parts)).strip()

    def render_list(self, element: ET.Element) -> str:
        items = []
        for item in element.findall("listitem"):
            items.append("- " + self.inline(item))
        return "\n" + "\n".join(items) + "\n"

    def render_code(self, element: ET.Element) -> str:
        return "\n\n```cpp\n" + "\n".join(flat(line) for line in element.findall("codeline")) + "\n```\n\n"

    # -- Descripciones ----------------------------------------------------
    def paragraphs(self, container: ET.Element | None) -> list[str]:
        """Parrafos de prosa, saltando listas de parametros y simplesect."""
        if container is None:
            return []
        result = []
        for para in container.findall("para"):
            if para.find("parameterlist") is not None or para.find("simplesect") is not None:
                continue
            text = self.inline(para)
            if text:
                result.append(text)
        return result

    def describe(self, container: ET.Element | None, out: list[str]) -> None:
        """Anade brief + descripcion detallada + parametros/retorno/notas."""
        if container is None:
            return
        for para in self.paragraphs(container):
            out.append(para)
            out.append("")
        for plist in container.iter("parameterlist"):
            kind = plist.get("kind", "param")
            if kind not in ("param", "templateparam"):
                continue
            rows = []
            for item in plist.findall("parameteritem"):
                names = ", ".join(flat(n) for n in item.iter("parametername"))
                desc_el = item.find("parameterdescription")
                desc = self.inline(desc_el) if desc_el is not None else ""
                rows.append(f"- `{names}` - {desc}" if desc else f"- `{names}`")
            if rows:
                out.append("**Parametros**" if kind == "param" else "**Parametros de plantilla**")
                out.append("")
                out.extend(rows)
                out.append("")
        for section in container.iter("simplesect"):
            kind = section.get("kind", "")
            text = self.inline(section)
            if not text:
                continue
            if kind == "return":
                out.append(f"**Devuelve** - {text}")
            elif kind == "note":
                out.append(f"> **Nota:** {text}")
            elif kind in ("warning", "attention"):
                out.append(f"> **Aviso:** {text}")
            elif kind == "see":
                out.append(f"**Ver tambien** - {text}")
            elif kind == "pre":
                out.append(f"**Ejemplo** - {text}")
            else:
                out.append(text)
            out.append("")

    # -- Miembros ---------------------------------------------------------
    def signature(self, member: ET.Element) -> str:
        """Firma del miembro como texto plano (para bloque de codigo)."""
        kind = member.get("kind", "")
        name = flat(member.find("name"))
        if kind == "enum":
            underlying = flat(member.find("type"))
            prefix = "enum class" if member.get("strong") == "yes" else "enum"
            signature = f"{prefix} {name}"
            if underlying and underlying != "int":
                signature += f" : {underlying}"
            return signature
        if kind == "typedef":
            definition = flat(member.find("definition"))
            if definition.startswith("using ") and " = " in definition:
                lhs, rhs = definition.split(" = ", 1)
                return f"using {lhs.split('::')[-1]} = {rhs}"
            if definition.startswith("typedef "):
                head, _, qualified = definition[len("typedef "):].rpartition(" ")
                if qualified:
                    return f"typedef {head} {qualified.split('::')[-1]}"
            return definition or name
        return_el = member.find("type")
        args = flat(member.find("argsstring"))
        signature = name + args
        if flat(return_el):
            signature = flat(return_el) + " " + signature
        return signature.strip()

    def render_member(self, member: ET.Element, out: list[str]) -> None:
        name = flat(member.find("name"))
        out.append(f"### `{name}`")
        out.append("")
        out.append("```cpp")
        out.append(self.signature(member))
        out.append("```")
        out.append("")
        self.describe(member.find("briefdescription"), out)
        self.describe(member.find("detaileddescription"), out)

        for value in member.findall("enumvalue"):
            vname = flat(value.find("name"))
            initializer = flat(value.find("initializer"))
            brief = self.inline(value.find("briefdescription"))
            detail = self.inline(value.find("detaileddescription"))
            text = detail or brief
            line = f"- `{vname}`"
            if initializer:
                line += f" ({initializer})"
            if text:
                line += f" - {text}"
            out.append(line)
        if member.findall("enumvalue"):
            out.append("")

    def module_file(self, compound: ET.Element) -> ET.Element | None:
        """El compuesto de fichero asociado a un modulo (para el comentario \\file)."""
        inner = compound.find("innerfile")
        if inner is None:
            return None
        return self.compounds.get(inner.get("refid", ""))

    def render_compound(self, refid: str) -> None:
        compound = self.compounds[refid]
        kind = compound.get("kind", "")
        name = flat(compound.find("compoundname"))
        out: list[str] = ["[<- Indice](index.md)", "", f"# {name}", ""]

        label = COMPOUND_KIND_LABEL.get(kind, kind)
        location = compound.find("location")
        meta = f"**{label}**"
        if location is not None and location.get("file"):
            file_path = location.get("file", "")
            if "/src/" in file_path:
                file_path = "src/" + file_path.split("/src/", 1)[1]
            meta += f" - `{file_path}`"
            if location.get("line"):
                meta += f" (linea {location.get('line')})"
        out.append(meta)
        out.append("")

        source = compound
        if kind == "module":
            file_compound = self.module_file(compound)
            if file_compound is not None and flat(
                file_compound.find("briefdescription")
            ) + flat(file_compound.find("detaileddescription")):
                source = file_compound

        self.describe(source.find("briefdescription"), out)
        self.describe(source.find("detaileddescription"), out)

        exports = [flat(e) for e in compound.iter("export")]
        if exports:
            out.append("**Re-exporta:** " + ", ".join(code_span(e) for e in exports))
            out.append("")

        inner_classes = compound.findall("innerclass")
        if inner_classes:
            out.append("**Tipos declarados:**")
            out.append("")
            for inner in inner_classes:
                target, ok = self.link(inner.get("refid", ""))
                label_inner = flat(inner)
                if ok:
                    out.append(f"- [{label_inner}]({target})")
                else:
                    out.append(f"- {label_inner}")
            out.append("")

        for section in compound.findall("sectiondef"):
            members = section.findall("memberdef")
            if not members:
                continue
            title = SECTION_LABEL.get(section.get("kind", ""), section.get("kind", ""))
            out.append(f"## {title}")
            out.append("")
            for member in members:
                self.render_member(member, out)

        (self.out_dir / self.files[refid]).write_text(
            "\n".join(out).rstrip() + "\n", encoding="utf-8"
        )

    # -- Indice -----------------------------------------------------------
    def render_index(self) -> None:
        out: list[str] = ["# kravidb - Documentacion de la API", ""]
        prelude = self.compounds.get("module__kravidb")
        if prelude is not None:
            file_compound = self.module_file(prelude)
            src = file_compound if file_compound is not None else prelude
            for para in self.paragraphs(src.find("detaileddescription")):
                out.append(para)
                out.append("")

        groups = [
            ("Modulos", ("module",)),
            ("Clases y estructuras", ("class", "struct")),
            ("Namespaces", ("namespace",)),
        ]
        for title, kinds in groups:
            rows = []
            for refid in self.ordered:
                compound = self.compounds[refid]
                if compound.get("kind") not in kinds:
                    continue
                name = flat(compound.find("compoundname"))
                brief = self.inline(compound.find("briefdescription"))
                if not brief:
                    file_compound = self.module_file(compound)
                    if file_compound is not None:
                        brief = self.inline(file_compound.find("briefdescription"))
                rows.append((name, self.files[refid], brief))
            if not rows:
                continue
            out.append(f"## {title}")
            out.append("")
            for name, filename, brief in rows:
                line = f"- [{name}]({filename})"
                if brief:
                    line += f" - {brief}"
                out.append(line)
            out.append("")
        (self.out_dir / "index.md").write_text(
            "\n".join(out).rstrip() + "\n", encoding="utf-8"
        )

    # -- Orquestacion -----------------------------------------------------
    def run(self) -> None:
        self.load()
        if self.out_dir.exists():
            shutil.rmtree(self.out_dir)
        self.out_dir.mkdir(parents=True, exist_ok=True)
        for refid in self.ordered:
            self.render_compound(refid)
        self.render_index()
        print(f"[docs] {len(self.ordered)} paginas escritas en {self.out_dir}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xml", default="docs/xml", help="directorio con el XML de Doxygen")
    parser.add_argument("--out", default="docs/md", help="directorio Markdown de salida")
    args = parser.parse_args(argv)

    xml_dir = Path(args.xml)
    if not xml_dir.is_dir():
        print(f"error: no existe el directorio {xml_dir}; ejecuta antes 'doxygen Doxyfile'", file=sys.stderr)
        return 1
    Converter(xml_dir, Path(args.out)).run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
