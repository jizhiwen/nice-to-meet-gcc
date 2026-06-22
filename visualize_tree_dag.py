#!/usr/bin/env python3
"""Visualize GCC GENERIC tree dump (e.g. test.c.005t.original) as a DAG.

Each node shows its full set of attributes. Outputs Graphviz DOT and an
interactive HTML graph (vis-network). Optionally renders SVG/PNG if ``dot``
is installed.
"""

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


NODE_HEADER_RE = re.compile(r"^@(\d+)\s+(\S+)(.*)$")
REF_RE = re.compile(r"@(\d+)")
SRCP_RE = re.compile(r":(\d+)\s*$")
Edge = Tuple[int, str, int, bool]


@dataclass
class TreeNode:
    node_id: int
    code: str
    attrs: Dict[str, str] = field(default_factory=dict)
    source_lines: List[str] = field(default_factory=list)

    def display_value(self, key: str, nodes: Dict[int, "TreeNode"]) -> str:
        raw = self.attrs[key]
        return resolve_value(raw, nodes)

    def all_attr_lines(self, nodes: Dict[int, "TreeNode"]) -> List[str]:
        lines = [f"{key}: {self.display_value(key, nodes)}" for key in self.attrs]
        return lines


def parse_attr_segment(text: str) -> Dict[str, str]:
    """Parse ``key: value`` pairs from one logical node line."""
    attrs: Dict[str, str] = {}
    text = text.strip()
    if not text:
        return attrs
    for part in re.split(r"\s{2,}(?=\S+\s*:)", text):
        match = re.match(r"(\S[\S ]*?)\s*:\s*(.*)", part.strip())
        if match:
            attrs[match.group(1).strip()] = match.group(2).strip()
    return attrs


def parse_dump(path: Path) -> Tuple[List[str], Dict[int, TreeNode], List[Edge]]:
    """Return header comments, nodes, and edges ``(src, label, dst, inferred)``."""
    headers: List[str] = []
    nodes: Dict[int, TreeNode] = {}
    edges: List[Edge] = []

    current: Optional[TreeNode] = None

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.rstrip()
        if not line.strip():
            continue
        if line.lstrip().startswith(";;"):
            headers.append(line.strip()[2:].strip())
            continue

        header = NODE_HEADER_RE.match(line)
        if header:
            node_id = int(header.group(1))
            code = header.group(2)
            tail = header.group(3)
            current = TreeNode(node_id=node_id, code=code)
            current.source_lines.append(line)
            current.attrs.update(parse_attr_segment(tail))
            nodes[node_id] = current
            continue

        if current is None:
            continue

        current.source_lines.append(line)
        continuation = line.strip()
        if continuation:
            current.attrs.update(parse_attr_segment(continuation))

    for node in nodes.values():
        for key, value in node.attrs.items():
            for ref in REF_RE.findall(value):
                dst = int(ref)
                if dst in nodes:
                    edges.append((node.node_id, key, dst, False))

    return headers, nodes, edges


def edge_key(src: int, label: str, dst: int) -> Tuple[int, str, int]:
    return (src, label, dst)


def has_edge(edges: Iterable[Edge], src: int, label: str, dst: int) -> bool:
    key = edge_key(src, label, dst)
    return any(edge_key(*item[:3]) == key for item in edges)


def add_edge(edges: List[Edge], src: int, label: str, dst: int, inferred: bool) -> None:
    if not has_edge(edges, src, label, dst):
        edges.append((src, label, dst, inferred))


def ref_target(value: str) -> Optional[int]:
    match = REF_RE.search(value.strip())
    return int(match.group(1)) if match else None


def srcp_line(node: TreeNode) -> Optional[int]:
    srcp = node.attrs.get("srcp")
    if not srcp:
        return None
    match = SRCP_RE.search(srcp.strip())
    return int(match.group(1)) if match else None


def is_user_var_decl(node: TreeNode) -> bool:
    if node.code != "var_decl":
        return False
    if node.attrs.get("note") == "artificial":
        return False
    return "name" in node.attrs


def var_decl_name(node: TreeNode, nodes: Dict[int, TreeNode]) -> Optional[str]:
    name_ref = node.attrs.get("name")
    if not name_ref:
        return None
    name_id = ref_target(name_ref)
    if name_id is None or name_id not in nodes:
        return None
    return nodes[name_id].attrs.get("strg")


def statement_list_children(node: TreeNode) -> List[Tuple[int, int]]:
    children: List[Tuple[int, int]] = []
    for key, value in node.attrs.items():
        if not key.isdigit():
            continue
        target = ref_target(value)
        if target is not None:
            children.append((int(key), target))
    return [target for _, target in sorted(children)]


def collect_scope_var_decls(
    nodes: Dict[int, TreeNode],
    scope_id: int,
) -> List[int]:
    decls = [
        node_id
        for node_id, node in nodes.items()
        if is_user_var_decl(node) and ref_target(node.attrs.get("scpe", "")) == scope_id
    ]
    return sorted(decls, key=lambda node_id: (srcp_line(nodes[node_id]) or 0, node_id))


def bind_expr_scope(nodes: Dict[int, TreeNode], bind_id: int) -> Optional[int]:
    bind = nodes.get(bind_id)
    if bind is None or bind.code != "bind_expr":
        return None
    vars_ref = bind.attrs.get("vars")
    if not vars_ref:
        return None
    head_id = ref_target(vars_ref)
    if head_id is None or head_id not in nodes:
        return None
    return ref_target(nodes[head_id].attrs.get("scpe", ""))


def infer_missing_edges(nodes: Dict[int, TreeNode], edges: List[Edge]) -> List[Edge]:
    """Reconstruct GCC slim-dump omissions such as DECL_CHAIN and DECL_EXPR operands."""
    inferred = list(edges)

    for node_id, node in nodes.items():
        if node.code != "bind_expr":
            continue
        vars_ref = node.attrs.get("vars")
        if not vars_ref:
            continue
        head_id = ref_target(vars_ref)
        scope_id = bind_expr_scope(nodes, node_id)
        if head_id is None or scope_id is None:
            continue

        scope_vars = collect_scope_var_decls(nodes, scope_id)
        if head_id in scope_vars:
            start = scope_vars.index(head_id)
            ordered = scope_vars[start:] + scope_vars[:start]
        else:
            ordered = scope_vars

        for left, right in zip(ordered, ordered[1:]):
            add_edge(inferred, left, "chain", right, True)

        body_ref = node.attrs.get("body")
        body_id = ref_target(body_ref) if body_ref else None
        if body_id is None or body_id not in nodes:
            continue
        body = nodes[body_id]
        if body.code != "statement_list":
            continue

        decl_expr_ids = [
            child_id
            for child_id in statement_list_children(body)
            if child_id in nodes and nodes[child_id].code == "decl_expr"
        ]
        if not decl_expr_ids or not ordered:
            continue

        for decl_expr_id, var_decl_id in zip(decl_expr_ids, ordered):
            add_edge(inferred, decl_expr_id, "decl", var_decl_id, True)

    return inferred


def resolve_value(raw: str, nodes: Dict[int, TreeNode]) -> str:
    """Expand ``@N`` references with node codes for readability."""

    def repl(match: re.Match[str]) -> str:
        ref_id = int(match.group(1))
        ref = nodes.get(ref_id)
        if ref is None:
            return match.group(0)
        return f"@{ref_id} ({ref.code})"

    return REF_RE.sub(repl, raw)


def dot_escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("\n", "<BR ALIGN='LEFT'/>")
    )


def node_color(code: str) -> str:
    if code.endswith("_type") or code.endswith("_decl"):
        return "#dbeafe"
    if code.endswith("_expr") or code.endswith("_stmt") or code == "statement_list":
        return "#dcfce7"
    if code.endswith("_cst"):
        return "#fef3c7"
    if code == "identifier_node":
        return "#fce7f3"
    return "#f3f4f6"


def build_type_levels(nodes: Dict[int, TreeNode]) -> Dict[str, int]:
    """Assign a stable level index per node type (first-seen order)."""
    levels: Dict[str, int] = {}
    for node_id in sorted(nodes):
        code = nodes[node_id].code
        if code not in levels:
            levels[code] = len(levels)
    return levels


def to_dot(
    headers: List[str],
    nodes: Dict[int, TreeNode],
    edges: List[Edge],
    group_by_type: bool = True,
) -> str:
    title = headers[0] if headers else "GCC GENERIC tree DAG"
    lines = [
        "digraph gcc_tree {",
        '  graph [rankdir=TB, bgcolor="white", fontsize=10, labeljust=l, labelloc=t];',
        '  node [shape=plaintext, fontname="Courier New", fontsize=9];',
        '  edge [fontname="Helvetica", fontsize=8, color="#64748b"];',
        f'  label="{dot_escape(title)}";',
        "",
    ]

    for node_id in sorted(nodes):
        node = nodes[node_id]
        rows = [
            f"<TR><TD COLSPAN='2' BGCOLOR='{node_color(node.code)}'><B>@{node_id} {dot_escape(node.code)}</B></TD></TR>"
        ]
        for key in node.attrs:
            value = dot_escape(node.display_value(key, nodes))
            rows.append(f"<TR><TD ALIGN='LEFT'>{dot_escape(key)}</TD><TD ALIGN='LEFT'>{value}</TD></TR>")
        label = f"<<TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' CELLPADDING='4'>{''.join(rows)}</TABLE>>"
        lines.append(f'  n{node_id} [label={label}];')

    lines.append("")
    for src, label, dst, inferred in edges:
        style = ' [label="{label}", color="#ea580c", style=dashed, fontcolor="#ea580c"]'.format(
            label=dot_escape(label)
        ) if inferred else ' [label="{label}"]'.format(label=dot_escape(label))
        lines.append(f"  n{src} -> n{dst}{style};")

    if group_by_type:
        by_type: Dict[str, List[int]] = {}
        for node_id in sorted(nodes):
            by_type.setdefault(nodes[node_id].code, []).append(node_id)
        lines.append("")
        lines.append("  // Keep same-type nodes on the same rank")
        for code, node_ids in by_type.items():
            members = "; ".join(f"n{nid}" for nid in node_ids)
            code_key = re.sub(r"[^A-Za-z0-9_]+", "_", code)
            lines.append(f"  subgraph cluster_rank_{code_key} {{ rank=same; {members}; }}")

    lines.append("}")
    return "\n".join(lines) + "\n"


def to_html(
    headers: List[str],
    nodes: Dict[int, TreeNode],
    edges: List[Edge],
    group_by_type: bool = True,
) -> str:
    title = headers[0] if headers else "GCC GENERIC tree DAG"
    type_levels = build_type_levels(nodes) if group_by_type else {}
    node_items = []
    for node_id in sorted(nodes):
        node = nodes[node_id]
        label_lines = [f"@{node_id} {node.code}"] + node.all_attr_lines(nodes)
        node_item = {
            "id": node_id,
            "label": "\n".join(label_lines),
            "title": html.escape("\n".join(label_lines)),
            "color": node_color(node.code),
            "font": {"face": "Courier New", "size": 11, "multi": "html", "align": "left"},
            "shape": "box",
            "margin": 12,
        }
        if group_by_type:
            node_item["level"] = type_levels[node.code]
        node_items.append(node_item)

    edge_items = []
    for src, label, dst, inferred in edges:
        item = {
            "from": src,
            "to": dst,
            "label": label,
            "arrows": "to",
            "font": {"size": 10, "align": "middle"},
        }
        if inferred:
            item.update(
                {
                    "color": {"color": "#ea580c"},
                    "dashes": True,
                    "title": "inferred (omitted by GCC slim dump)",
                }
            )
        edge_items.append(item)

    inferred_count = sum(1 for *_, inferred in edges if inferred)
    payload = {
        "title": title,
        "headers": headers,
        "nodes": node_items,
        "edges": edge_items,
        "inferred_count": inferred_count,
    }

    data_json = json.dumps(payload, ensure_ascii=False)
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>{html.escape(title)}</title>
  <style>
    html, body {{
      height: 100%;
      margin: 0;
      font-family: system-ui, sans-serif;
      background: #f8fafc;
    }}
    #header {{
      padding: 12px 16px;
      background: #0f172a;
      color: #f8fafc;
    }}
    #header h1 {{
      margin: 0 0 4px;
      font-size: 18px;
      font-weight: 600;
    }}
    #header p {{
      margin: 0;
      color: #cbd5e1;
      font-size: 13px;
    }}
    #graph {{
      width: 100%;
      height: calc(100% - 72px);
      border-top: 1px solid #cbd5e1;
      background: white;
    }}
  </style>
  <script src="https://unpkg.com/vis-network/standalone/umd/vis-network.min.js"></script>
</head>
<body>
  <div id="header">
    <h1>{html.escape(title)}</h1>
    <p>{len(nodes)} nodes, {len(edges)} edges ({payload["inferred_count"]} inferred) — orange dashed = reconstructed missing links</p>
  </div>
  <div id="graph"></div>
  <script>
    const payload = {data_json};
    const container = document.getElementById("graph");
    const network = new vis.Network(
      container,
      {{
        nodes: new vis.DataSet(payload.nodes),
        edges: new vis.DataSet(payload.edges),
      }},
      {{
        layout: {{
          hierarchical: {{
            enabled: true,
            direction: "UD",
            sortMethod: "directed",
            levelSeparation: 180,
            nodeSpacing: 220,
            treeSpacing: 260,
          }},
        }},
        physics: false,
        interaction: {{ hover: true, multiselect: true, navigationButtons: true }},
        edges: {{ smooth: {{ type: "cubicBezier" }} }},
      }}
    );
    network.fit({{ animation: true }});
  </script>
</body>
</html>
"""


def render_with_dot(dot_path: Path, fmt: str) -> Optional[Path]:
    dot_bin = shutil.which("dot")
    if not dot_bin:
        return None
    out_path = dot_path.with_suffix(f".{fmt}")
    subprocess.run([dot_bin, f"-T{fmt}", str(dot_path), "-o", str(out_path)], check=True)
    return out_path


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "input",
        nargs="?",
        default="test.c.005t.original",
        help="GCC tree dump file (default: test.c.005t.original)",
    )
    parser.add_argument(
        "-o",
        "--output-prefix",
        default=None,
        help="Output file prefix without extension (default: input stem + '.dag')",
    )
    parser.add_argument(
        "--render",
        choices=("svg", "png", "pdf"),
        default=None,
        help="Also render DOT with graphviz ``dot`` if available",
    )
    parser.add_argument(
        "--no-infer-missing",
        action="store_true",
        help="Do not reconstruct slim-dump omissions (chain, decl_expr->var_decl)",
    )
    parser.add_argument(
        "--no-group-by-type",
        action="store_true",
        help="Do not place same-type nodes on the same rank/level",
    )
    parser.add_argument(
        "--gcc-dump",
        action="store_true",
        help="Run ``gcc -fdump-tree-original-raw`` on INPUT source before visualizing",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    input_path = Path(args.input)
    if args.gcc_dump:
        if input_path.suffix not in {".c", ".cc", ".cpp", ".cxx"}:
            print("error: --gcc-dump expects a C/C++ source file", file=sys.stderr)
            return 1
        dump_path = input_path.with_suffix("")
        dump_glob = f"{input_path.name}.0*t.original"
        subprocess.run(
            ["gcc", "-fdump-tree-original-raw", "-c", str(input_path), "-o", "/dev/null"],
            check=True,
        )
        matches = sorted(input_path.parent.glob(dump_glob))
        if not matches:
            print(f"error: gcc dump not found: {dump_glob}", file=sys.stderr)
            return 1
        input_path = matches[-1]

    if not input_path.is_file():
        print(f"error: input file not found: {input_path}", file=sys.stderr)
        return 1

    prefix = Path(args.output_prefix) if args.output_prefix else input_path.with_suffix(".dag")

    headers, nodes, edges = parse_dump(input_path)
    if not nodes:
        print(f"error: no tree nodes parsed from {input_path}", file=sys.stderr)
        return 1

    explicit_count = len(edges)
    if not args.no_infer_missing:
        edges = infer_missing_edges(nodes, edges)
    inferred_count = len(edges) - explicit_count

    dot_path = prefix.with_suffix(".dot")
    html_path = prefix.with_suffix(".html")
    group_by_type = not args.no_group_by_type
    dot_path.write_text(to_dot(headers, nodes, edges, group_by_type=group_by_type), encoding="utf-8")
    html_path.write_text(to_html(headers, nodes, edges, group_by_type=group_by_type), encoding="utf-8")

    print(f"Parsed {len(nodes)} nodes, {len(edges)} edges from {input_path}")
    if inferred_count:
        print(f"  ({explicit_count} explicit + {inferred_count} inferred missing links)")
    print(f"Wrote {dot_path}")
    print(f"Wrote {html_path}")

    if args.render:
        rendered = render_with_dot(dot_path, args.render)
        if rendered:
            print(f"Wrote {rendered}")
        else:
            print("warning: graphviz ``dot`` not found; skipped rendering", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
