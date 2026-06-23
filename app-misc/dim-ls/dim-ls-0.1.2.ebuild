# Copyright 2026-2026 dimunyx Authors
# Distributed under the terms of the GNU General Public License v3

EAPI=8

DESCRIPTION="A ls fork written on C++ with icons and colors"
HOMEPAGE="https://github.com/dimunyx/dimunyx-overlay"
SRC_URI=""

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64"

IUSE="icons catppuccin-mocha catppuccin-macchiato catppuccin-frappe catppuccin-latte"

S="${WORKDIR}"

src_unpack() {
	local src
	if use icons; then
		src="dim-ls.cpp"
		use catppuccin-macchiato && src="dim-ls-macchiato.cpp"
		use catppuccin-frappe && src="dim-ls-frappe.cpp"
		use catppuccin-latte && src="dim-ls-latte.cpp"
	else
		src="dim-ls-noicons.cpp"
	fi
	cp "${FILESDIR}/${src}" "${S}"/dim-ls.cpp
}

src_compile() {
	${CXX} ${CXXFLAGS} dim-ls.cpp -o dim-ls
}

src_install() {
	dobin dim-ls
	insinto /usr/share/fish/vendor_completions.d
	doins "${FILESDIR}/dim-ls.fish"
}
