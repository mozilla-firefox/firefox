# MozLint

Linters are used in the Firefox repository to help enforce coding style and avoid bad practices.
Due to the wide variety of languages in use, this is not always an easy task.
In addition, linters should be runnable from editors, from the command line, from review tools
and from continuous integration. It's easy to see how the complexity of running all of these
different kinds of linters in all of these different places could quickly balloon out of control.

`Mozlint` is a library that accomplishes several goals:

1. It provides a standard method for adding new linters to the tree, which can be as easy as
   defining a config object in a `.yml` file. This helps keep lint related code localized, and
   prevents different teams from coming up with their own unique lint implementations.
2. It provides a streamlined interface for running all linters at once. Instead of running N
   different lint commands to test your patch, a single `mach lint` command will automatically run
   all applicable linters. This means there is a single API surface that other tools can use to
   invoke linters.
3. With a simple taskcluster configuration, Mozlint provides an easy way to execute all these jobs
   at review phase.

`Mozlint` isn't designed to be used directly by end users. Instead, it can be consumed by things
like mach, phabricator and taskcluster.
