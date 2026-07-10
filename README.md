# zl

zl is a fast, lightweight, and modern `ls` replacement built in pure C. It was made as a hobby/practice project for low-level systems engineering. It is way lighter than coreutils `ls` and is heavily optimized for terminal minimalism.

## Features

- **Built-in Grid Layout:** Automatically stacks files into smart, columned grids that calculate width using `ioctl` and terminal dimensions.
- **Minimalist Data View:** Drops legacy visual noise (like permissions, ownership blocks, and link counts) to keep your terminal perfectly clean.
- **Custom Spacing:** Formatted to natively display information in a streamlined layout:
  
  `year/month/day              filename --size`

- **FOSS & Lightweight:** Zero bloated dependencies, extremely fast compilation, and small memory footprint.
- **Colorized Output:** Native terminal coloring to distinguish directories, images, audio, video, and source files at a glance.

## Options

- `-a` : Show hidden files (entries starting with `.`)
- `-b` : Force exact byte counts (displays raw bytes with a `b` suffix instead of human-readable units)

## Installation

### 1. Clone & Compile
Grab the source code and compile it with optimizations:
```bash
gcc -O2 zl.c -o zl
```
and to use it just like `ls`,move it to your `path`

```bash
mv zl ~/.local/bin/
```

License

This project is licensed under the GNU General Public License v3.0 (GPL-3.0). See the LICENSE file for details.
