# Python Coding style

## Coding style

{doc}`/code-quality/lint/linters/black` is the tool used to reformat the Python code.

## Linting

The Python linting is done by {doc}`/code-quality/lint/linters/ruff` which includes rules from `flake8` and `pylint`.
It is executed by mozlint both at review phase and in the CI.

## Indentation

Four spaces in Python code.

### Makefile/moz.build practices

- Changes to makefile and moz.build variables do not require
  build-config peer review. Any other build system changes, such as
  adding new scripts or rules, require review from the build-config
  team.

- Suffix long `if`/`endif` conditionals with #{ & #}, so editors
  can display matched tokens enclosing a block of statements.

  ```
  ifdef CHECK_TYPE #{
    ifneq ($(flavor var_type),recursive) #{
      $(warning var should be expandable but detected var_type=$(flavor var_type))
    endif #}
  endif #}
  ```

- moz.build are python and follow normal Python style.

- List assignments should be written with one element per line. Align
  closing square brace with start of variable assignment. If ordering
  is not important, variables should be in alphabetical order.

  ```python
  var += [
      "foo",
      "bar",
  ]
  ```

- Use `CONFIG['TARGET_CPU'] {=arm}` to test for generic classes of
  architecture rather than `CONFIG['OS_TEST'] {=armv7}` (re: bug 886689).

## Other advice

- Run `./mach lint` on your changes and address every issue it reports.
- Follow [PEP 8](https://www.python.org/dev/peps/pep-0008/). Please run {doc}`/code-quality/lint/linters/black` for this.
- Do not place statements on the same line as `if/elif/else`
  conditionals to form a one-liner.
- Global vars, please avoid them at all cost.
- Exclude outer parenthesis from conditionals.Use
  `if x > 5:,`rather than `if (x > 5):`
- Use string formatters, rather than var + str(val).
  `var = 'Type %s value is %d'% ('int', 5).`
- Testing/Unit tests, please write them and make sure that they are executed in the CI.
