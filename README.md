# Jazz Shell

[![Build Status](https://img.shields.io/github/actions/workflow/status/yourusername/jazz-shell/ci.yml?branch=master)](https://github.com/yourusername/jazz-shell/actions)  
[![License](https://img.shields.io/github/license/yourusername/jazz-shell)](LICENSE)

A minimal UNIX-style shell featuring:

- **Persistent history** (`history -r/-w/-a`, `$HISTFILE`)  
- **Tab-completion** for builtins, files, and `$PATH` programs  
- **Builtins**: `cd`, `pwd`, `echo`, `type`, `history`, `exit`  
- **Pipelines & I/O**: `|`, `>`, `>>`, `2>`, `&>`

---

## Build & Install

```bash
git clone https://github.com/yourusername/jazz-shell.git
cd jazz-shell
make              # builds `jazz`
sudo make install # installs to /usr/local/bin
````

## Quick Start

```bash
export HISTFILE=~/.jazz_history
jazz
$ echo Hello
Hello
$ ls | grep .c > list.txt
$ history -w $HISTFILE
$ exit 0
```

## Configuration

* **`$HISTFILE`**: path to load/save history
* **Auto-save**: history is appended on `exit` if `$HISTFILE` is set

## License

MIT © [yourusername](https://github.com/yourusername)

```
```
