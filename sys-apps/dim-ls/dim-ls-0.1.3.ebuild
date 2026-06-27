# Copyright 2026 dimunyx Authors
# Distributed under the terms of the GNU General Public License v3

EAPI=8

DESCRIPTION="A ls fork written in C++ with icons and colors"
HOMEPAGE="https://github.com/dimunyx/dimunyx-overlay"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64"

S="${WORKDIR}"

src_unpack() {
	unpack "${FILESDIR}/dim-ls-0.1.3.tar.gz"
}

src_compile() {
	make build
}

src_install() {
    dobin dist/dim-ls
    insinto /usr/share/fish/vendor_completions.d
    doins "dim-ls.fish"
}
