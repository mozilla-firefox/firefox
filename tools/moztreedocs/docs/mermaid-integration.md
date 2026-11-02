# Mermaid Integration

Mermaid is a tool that lets you generate flow charts, sequence diagrams, gantt
charts, class diagrams and vcs graphs from a simple markup language. This
allows charts and diagrams to be embedded and edited directly in the
documentation source files rather than creating them as images using some
external tool and checking the images into the tree.

To add a diagram, simply put something like this into your page:

% These two examples come from the upstream website (<https://mermaid-js.github.io/mermaid/#/>)

```rst
.. mermaid::

    graph TD;
        A-->B;
        A-->C;
        B-->D;
        C-->D;
```

````md
```{mermaid}
graph TD;
    A-->B;
    A-->C;
    B-->D;
    C-->D;
```
````

The result will be:

```{mermaid}
graph TD;
    A-->B;
    A-->C;
    B-->D;
    C-->D;
```

Or

```{code-block} rst
:caption: .rst

.. mermaid::

     sequenceDiagram
         participant Alice
         participant Bob
         Alice->>John: Hello John, how are you?
         loop Healthcheck
             John->>John: Fight against hypochondria
         end
         Note right of John: Rational thoughts <br/>prevail!
         John-->>Alice: Great!
         John->>Bob: How about you?
         Bob-->>John: Jolly good!
```

````{code-block} markdown
:caption: .md

```{mermaid}
sequenceDiagram
    participant Alice
    participant Bob
    Alice->>John: Hello John, how are you?
    loop Healthcheck
        John->>John: Fight against hypochondria
    end
    Note right of John: Rational thoughts <br/>prevail!
    John-->>Alice: Great!
    John->>Bob: How about you?
    Bob-->>John: Jolly good!
```
````

will show:

```{mermaid}
sequenceDiagram
    participant Alice
    participant Bob
    Alice->>John: Hello John, how are you?
    loop Healthcheck
        John->>John: Fight against hypochondria
    end
    Note right of John: Rational thoughts <br/>prevail!
    John-->>Alice: Great!
    John->>Bob: How about you?
    Bob-->>John: Jolly good!
```

## ZenUML

[ZenUML](https://zenuml.com/) sequence diagrams are also supported. The
plugin that draws them is fetched only by a page that contains one. Start the
block with `zenuml`. The plugin ignores the diagram theme and draws in black on
white, so in the dark color scheme the diagram is inverted; avoid colors that a
light-to-dark inversion would misrepresent:

````md
```{mermaid}
zenuml
    title Order Service
    @Actor Client
    Client->OrderService.create(order) {
        OrderService->Inventory.reserve(order.items)
        return confirmation
    }
```
````

```{mermaid}
zenuml
    title Order Service
    @Actor Client
    Client->OrderService.create(order) {
        OrderService->Inventory.reserve(order.items)
        return confirmation
    }
```

## Options

The directive takes the Sphinx options `align`, `alt`, `caption`, `name` and
`zoom`, and mermaid's `title` and `config`. In the markdown form, each value has
to fit on one line. MyST stops reading options at the first line that does not
start with a colon, so a value that wraps onto a second line turns that line
into the start of the diagram. Mermaid then finds no diagram type, and the
published page shows the diagram source as plain text. The docs build fails on
such a block. The reStructuredText directive does not have this problem,
because docutils joins an indented continuation line onto the option's value.

To spread a long value over several lines, use the YAML options block:

````md
```{mermaid}
---
caption: A caption long enough that it does not fit on one line, which
  the YAML block form joins back together.
---
flowchart LR
    A --> B
```
````

MyST reads a `---` block at the top of the directive body as the directive's
options, so a key the directive does not know fails the build. To pass
mermaid's own front matter through, put a blank line before it. Do not combine
it with `:title:` or `:config:`: the directive turns those into a front matter
block of its own, and mermaid reads only the first one.

## Size and alignment

Diagrams render at their intrinsic size, centered horizontally within the page.
Their caption is centered automatically as well, so the `:align:` option isn't
needed.

A diagram wider than the page scrolls horizontally within its container, and
fades at the edges to indicate it overflows.

## Colors

A diagram follows the page's color scheme. A node you do not style takes the
theme's default fill, white in light mode and black in dark mode, so prose
cannot identify a node by that color.

A node you style with an explicit `fill:` keeps that color in both schemes.
Give it an explicit `color:` too, or its label takes the theme's own label
color and comes out grey on a light fill in dark mode:

```
classDef offstrip fill:#fef3c7,stroke:#92400e,color:#1a1a1a;
```

Do not let color be the only thing that identifies a node in prose. Pair it
with the shape or the label, since color alone does not reach a reader who
cannot tell the colors apart.

See [Mermaid's official](https://mermaid-js.github.io/mermaid/#/) docs for
more details on the syntax, and use the
[Mermaid Live Editor](https://mermaidjs.github.io/mermaid-live-editor/) to
experiment with creating your own diagrams.
