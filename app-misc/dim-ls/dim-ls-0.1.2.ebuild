# Copyright 2026-2026 dimunyx Authors
# Distributed under the terms of the GNU General Public License v3

EAPI=8

DESCRIPTION="A ls fork written on C++ with icons and colors"
HOMEPAGE="https://github.com/dimunyx/dimunyx-overlay"
SRC_URI=""

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64"

S="${WORKDIR}"

src_unpack() {
	cp "${FILESDIR}/dim-ls.cpp" "${S}"/
}

src_compile() {
	g++ dim-ls.cpp -o dim-ls
}

src_install() {
	dobin dim-ls
}
