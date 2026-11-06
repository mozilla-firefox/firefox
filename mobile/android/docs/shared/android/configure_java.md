# Configure Java for Android

If you are building Android via the command line (which includes our recommended pre-push hooks), you will need to configure Java. If not, you can just use Android Studio and do not need to read further.

Notes:

* this guide is written using macOS and will need to be updated to additionally support Linux and Windows.
* if you find a better way to do configure Java, let us know!

## Background

Our Android builds run on Java 25 (while compiling to the Java 17 language level). By default `brew` and other package managers may install other versions that can throw errors during development.

## Methods

### Method #1: use the JDK installed by `./mach bootstrap`

Do the following:

* Run `./mach bootstrap`, which installs a Java 25 JDK under `~/.mozbuild/jdk/`
* Add the following line to your `~/.zshrc` or equivalent shell startup file: `export JAVA_HOME="$HOME/.mozbuild/jdk/jdk-25.0.5+8/Contents/Home"`
  * Update the version to match the directory under `~/.mozbuild/jdk/`

That's it! To verify correctness, open a new shell, navigate to a directory with Android source code, and type `./gradlew tasks`.

This works because macOS comes with a `/usr/bin/java` stub which will defer the location of the JDK to the value in the `JAVA_HOME` environment variable.

### Method #2: install Java 25 from Homebrew

You can install Java from [Homebrew](https://brew.sh/) using the command below.

```bash
brew install openjdk@25
```

### Method #3: install from website

See <https://docs.oracle.com/en/java/javase/25/install/installation-jdk-macos.html>

## Troubleshooting

### Get Java version

To see what version of Java you have installed, run:

```sh
java -version
```

If you have Java 25 configured, you'll see output like:

```
openjdk version "25.0.5" 2026-10-20 LTS
OpenJDK Runtime Environment Temurin-25.0.5+8 (build 25.0.5+8-LTS)
OpenJDK 64-Bit Server VM Temurin-25.0.5+8 (build 25.0.5+8-LTS, mixed mode, sharing)
```
