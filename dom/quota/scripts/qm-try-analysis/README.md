# QM_TRY Analysis Guide

Welcome to the QM_TRY Analysis Guide!
This document walks you through the process of setting up the environment for semi-automatic monitoring of QM_TRY failures within Mozilla.
Follow these step-by-step instructions to ensure you have the necessary requirements before initiating the analysis.

## Setup Instructions

### 1. Clone the Firefox repository

Ensure you have a local clone of the Firefox repository.
If you don't have it yet, refer to [this link](https://firefox-source-docs.mozilla.org/contributing/contribution_quickref.html#bootstrap-a-copy-of-the-firefox-source-code).

Building the project is not necessary for this setup.

### 2. Obtain Telemetry API Key

Obtain a Telemetry API Key by visiting [Telemetry API Key](https://sql.telemetry.mozilla.org/users/me).
Save this key for later use in the analysis scripts.

### 3. Obtain Bugzilla API Key (optional)

Obtain your Bugzilla API Key from [Bugzilla User Preferences](https://bugzilla.mozilla.org/userprefs.cgi?tab=apikey).

This is needed only if you plan to submit reports to Bugzilla.

### 4. Install rust-code-analysis

If you're already building Firefox, it means you have all required packages
installed and you can jump to [Generate output](#generate-output) section.

If not done already, set up Rust by visiting [rustup.rs](https://rustup.rs/).
Once Rust is installed, install rust-code-analysis using the following command:

```bash
cargo install --git https://github.com/mozilla/rust-code-analysis --rev 56f182ac570
```


### 5. Install Python

Install Python if not already set up.

### 6. Install Poetry and set up dependencies

If you haven't installed Poetry, use the following commands to install it and set up the project:

```bash
pip install poetry
cd mozilla-unified/dom/quota/scripts/qm-try-analysis
poetry install
```

### Containerized setup

To streamline the setup process, use [`Podman`](https://github.com/containers/podman?tab=readme-ov-file#podman-a-tool-for-managing-oci-containers-and-pods) with the provided `Containerfile`. Navigate to the relevant directory:

```bash
cd mozilla-unified/dom/quota/scripts/qm-try-analysis
```

Build the container image and run the container:

```bash
podman run -it $(podman build -q .) -v <path on your system>:/home/scripts/qm-try-analysis/output
```

## Effort

- Each run takes approximately 5–15 minutes, with scripts running in less than 5 minutes.
- Analysis is performed once a week (as of November 2023) on Monday.

## Generate Output

Navigate to the analysis directory:

```bash
cd mozilla-unified/dom/quota/scripts/qm-try-analysis
```

The process involves fetching data, analyzing, and reporting. Here's a quick overview:

If you installed packages yourself (instructions 2 -> 6 above):
```bash
# Jump into a poetry shell session
poetry shell
````

Alternatively if you're building Firefox, prefix all your commands with
```
PYTHONPATH=dom/quota/scripts/qm-try-analysis ./mach python -m
```
For example:
```
PYTHONPATH=dom/quota/scripts/qm-try-analysis ./mach python -m qm_try_analysis.fetch -k "$REDASH_API_KEY" -d 2 -w output
```

```
# Fetch data
qm-try-analysis fetch [OPTIONS]

# Analyze data
qm-try-analysis analyze [OPTIONS]

# Optionally render the analysis as an HTML page
qm-try-analysis html [OPTIONS]

# Report failures to Bugzilla
qm-try-analysis report [OPTIONS]

# To exit the shell session
exit
```

Refer to the detailed usage instructions provided by adding the `--help` option to one of the commands above.

## Analysis

- Look for noticeable problems such as new errors, unusual stacks, or issues not encountered for a long time.

## Additional Hints

- Treat QM_TRY bugs as meta bugs; do not attach patches there; create a new bug for that and cross-link using blocks/depends on.
- Interesting bugs to cross-link:
  - [Bug 1705304](https://bugzilla.mozilla.org/show_bug.cgi?id=1705304) (FATAL errors): "error conditions we cannot meaningfully recover from."
  - [Bug 1712582](https://bugzilla.mozilla.org/show_bug.cgi?id=1712582) (Replace generic NS_ERROR_FAILURE errors with more specific codes).
