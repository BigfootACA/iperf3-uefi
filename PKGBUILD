# Maintainer: BigfootACA <bigfoot@classfun.cn>

pkgname=iperf3-uefi
pkgver=0.1
pkgrel=1
pkgdesc="iperf3 client for the UEFI Shell"
arch=(x86_64 aarch64)
url="https://github.com/BigfootACA/iperf3-uefi"
license=(GPL-2.0-only)
makedepends=(gcc git make nasm python)
options=(!debug !strip)
source=(build.sh)
md5sums=(SKIP)

pkgver() {
	cd "$(dirname "$(realpath "$srcdir/build.sh")")"
	bash scripts/version.sh
}

build() {
	cd "$(dirname "$(realpath "$srcdir/build.sh")")"
	find build/Build/IPerf3Pkg/ -name '*.efi' -delete 2>/dev/null || true
	find build/Build/IPerf3Pkg/ -name '*.debug' -delete 2>/dev/null || true
	bash build.sh
}

package() {
	cd "$(dirname "$(realpath "$srcdir/build.sh")")"
	install -Dm644 build/Build/IPerf3Pkg/*_*/*/IPerf3.efi \
		"$pkgdir/usr/share/iperf3-uefi/IPerf3.efi"
	install -Dm644 build/Build/IPerf3Pkg/*_*/*/IPerf3.debug \
		"$pkgdir/usr/share/iperf3-uefi/IPerf3.debug"
	install -Dm644 README.md \
		"$pkgdir/usr/share/doc/iperf3-uefi/README.md"
	install -Dm644 LICENSE \
		"$pkgdir/usr/share/licenses/iperf3-uefi/LICENSE"
}
