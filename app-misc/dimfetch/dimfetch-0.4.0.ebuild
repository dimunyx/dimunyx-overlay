# Copyright 2026 dimunyx Authors
# Distributed under the terms of the MIT license

EAPI=8

DESCRIPTION="Minimalistic fetch written in C++"
HOMEPAGE="https://github.com/dimunyx/dimfetch"
SRC_URI="https://raw.githubusercontent.com/dimunyx/dimfetch/main/archives/dimfetch-${PV}.tar.gz"

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64"
IUSE="x11 hyprland alsa brightness flatpak"

DEPEND="
	sys-devel/gcc
	dev-build/make
"

RDEPEND="
	x11-misc/read-edid
	sys-apps/pciutils
	media-video/wireplumber
	media-video/pipewire
	x11? (
		x11-misc/wmctrl
	)
	hyprland? (
		gui-wm/hyprland
	)
	alsa? (
		media-sound/alsa-utils
	)
	brightness? (
		app-misc/brightnessctl
	)
	flatpak? (
		sys-apps/flatpak
	)

"

S="${WORKDIR}"

src_prepare() {
    ln -s . src
    default
}

src_compile() {
    make build
}

src_install() {
    dobin dist/dimfetch
	insinto /usr/share/fish/vendor_completions.d
	doins "dimfetch.fish"
}
