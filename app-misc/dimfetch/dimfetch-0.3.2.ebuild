# Copyright 2026 dimunyx Authors
# Distributed under the terms of the MIT license

EAPI=8

DESCRIPTION="Minimalistic fetch written in C++"
HOMEPAGE="https://github.com/dimunyx/dimunyx-overlay"
SRC_URI=""

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64"

S="${WORKDIR}/dimfetch-0.3.2"

src_unpack() {
    cp -r "${FILESDIR}/dimfetch-0.3.2" "${WORKDIR}" || die
}

S="${WORKDIR}/dimfetch-0.3.2"

src_compile() {
    make build
}

src_install() {
	dobin dist/dimfetch
	insinto /usr/share/fish/vendor_completions.d
	doins "${FILESDIR}/dimfetch.fish"
}
