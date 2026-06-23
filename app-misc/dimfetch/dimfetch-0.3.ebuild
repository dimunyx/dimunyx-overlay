# Copyright 2026 dimunyx Authors
# Distributed under the terms of the GNU General Public License v3

EAPI=8

DESCRIPTION="Minimalistic fetch written in C++"
HOMEPAGE="https://github.com/dimunyx/dimunyx-overlay"
SRC_URI=""

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64"

S="${WORKDIR}"

src_unpack() {
	cp "${FILESDIR}/dimfetch.cpp" "${S}"/
}

src_compile() {
	${CXX} ${CXXFLAGS} dimfetch.cpp -o dimfetch
}

src_install() {
	dobin dimfetch
	insinto /usr/share/fish/vendor_completions.d
	doins "${FILESDIR}/dimfetch.fish"
}
