<div align="center">
   <br>
   <img
       src="assets/gentoo.svg"   
       width="50"
       height="50"
       alt="Home"
   />
   <br>
   <br>

   **Personal package repository for Gentoo GNU/Linux**

   <br>

   [![Gentoo GNU/Linux](https://img.shields.io/badge/Gentoo%20Linux?logo=gentoo&logoColor=%23209fb5&link=https%3A%2F%2Fgentoo.org)](https://gentoo.org)
   [![Last Update](https://img.shields.io/github/last-commit/dimunyx/dimunyx-arch-repo?style=flat-square&color=blueviolet)](https://github.com/dimunyx/dimunyx-arch-repo/commits/main)

   <br>
   <br>
</div>

## About

This repository contains packages made by dimunyx, and **PKGBUILD** for Arch Linux. \
Packages can be installed directly via `pacman` by adding the repository to your pacman configuration. \
P.S: Soon will be added manual installation via `compiling`.

---

## Packages

| Package  | Category | Version | Description                                    |
|----------|----------|---------|------------------------------------------------|
| dim-ls   | sys-apps |  0.1.3  | A ls fork written on C++ with icons and colors |
| dimfetch | app-misc |  0.3.1  | Minimalistic fetch written in C++              |

---

## Installation

### 1. Add overlay url

```conf
[dimunyx-overlay]
location = /var/db/repos/dimunyx-overlay
sync-type = git
sync-uri = https://github.com/dimunyx/dimunyx-overlay.git
auto-sync = yes
```

### 2. Sync overlay

```bash
emaint sync -r dimunyx-overlay (with sudo/doas)
```

### 3. Install a package

```bash
emerge -(your flags) <package-name> (with sudo/doas)
```

## Support
If you have any problems/issues with repository don't be shy and tell me the error [here](https://mail.google.com/mail/u/0/#inbox?compose=DmwnWstvJsSTKsCdNHPWmQcjHvvqgsbwBgpvhmHmvdBjvfFPjhrwkBWJKsPWqkWsnKRscKWhXKKG)
