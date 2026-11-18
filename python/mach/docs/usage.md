(mach-usage)=

# User Guide

Mach is the central entry point for most operations that can be performed in
the Firefox repository.

## Command Help

To see an overview of all the available commands, run:

```shell
./mach help
```

For more detailed information on a specific command, run:

```shell
./mach help <command>
```

If a command has subcommands listed, you can see more details on the subcommand
by running:

```shell
./mach help <command> <subcommand>
```

Alternatively, you can pass `-h/--help`. For example, all of the
following are valid:

```shell
./mach help try
./mach help try fuzzy
./mach try -h
./mach try fuzzy --help
```

## Tab Completion

There are commands built-in to `mach` that can generate a fast tab completion
script for various shells. Supported shells are currently `bash`, `zsh` and
`fish`. These generated scripts will slowly become out of date over time, so
you may want to create a cron task to periodically re-generate them.

See below for installation instructions:

### Bash

```shell
mach mach-completion bash -f _mach
sudo mv _mach /etc/bash_completion.d
```

### Bash (homebrew)

```shell
mach mach-completion bash -f $(brew --prefix)/etc/bash_completion.d/mach.bash-completion
```

### Zsh

```shell
mkdir ~/.zfunc
mach mach-completion zsh -f ~/.zfunc/_mach
```

then edit ~/.zshrc and add:

```shell
fpath+=~/.zfunc
autoload -U compinit && compinit
```

You can use any directory of your choosing.

### Zsh (oh-my-zsh)

```shell
mkdir $ZSH/plugins/mach
mach mach-completion zsh -f $ZSH/plugins/mach/_mach
```

then edit ~/.zshrc and add 'mach' to your enabled plugins:

```shell
plugins(mach ...)
```

### Zsh (prezto)

```shell
mach mach-completion zsh -f ~/.zprezto/modules/completion/external/src/_mach
```

### Fish

```shell
./mach mach-completion fish -f ~/.config/fish/completions/mach.fish
```

### Fish (homebrew)

```shell
./mach mach-completion fish -f (brew --prefix)/share/fish/vendor_completions.d/mach.fish
```

## User Settings

Some mach commands can read configuration from a `machrc` file. The default
location for this file is `~/.mozbuild/machrc` (you'll need to create it).
This can also be set to a different location by setting the `MACHRC`
environment variable.

For a list of all the available settings, run:

```shell
./mach settings
```

The settings file follows the `ini` format, e.g:

```ini
[alias]
eslint = lint -l eslint

[build]
telemetry = true

[try]
default = fuzzy
```

[bash completion]: https://searchfox.org/mozilla-central/source/python/mach/bash-completion.sh
