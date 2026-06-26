# Copyright 2026 dimunyx Authors
# Distributed under the terms of the GNU General Public License v3

EAPI=8

DESCRIPTION="A ls fork written in C++ with icons and colors"
HOMEPAGE="https://github.com/dimunyx/dimunyx-overlay"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64"

S="${WORKDIR}/dim-ls-0.1.3"

src_unpack() {
	cp -r "${FILESDIR}/dim-ls-0.1.3" "${WORKDIR}" || die
}

src_compile() {
    ${CXX} ${CXXFLAGS} main.cpp -o dim-ls || die
}

src_install() {
    dobin dim-ls
    insinto /usr/share/fish/vendor_completions.d
    doins "${FILESDIR}/dim-ls-0.1.3/dim-ls.fish"
}
