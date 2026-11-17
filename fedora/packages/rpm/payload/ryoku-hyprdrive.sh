# shellcheck shell=bash
: "${startdir:?}" "${srcdir:?}" "${pkgdir:?}"
source "$startdir/hyprland-plugin.sh"
_repo="$startdir/../../../.."
_src="$_repo/ryoku/hyprland/plugins/hyprdrive"

# The plugin, agent-run beside it, the look bridge, Lithic's faces and the
# rebuild recipe: the same payload as the Arch package.
build() {
  rm -rf "$srcdir/hyprdrive"
  cp -a "$_src" "$srcdir/hyprdrive"
  rm -rf "$srcdir/hyprdrive/build" "$srcdir/hyprdrive/hyprdrive.so"
  make -C "$srcdir/hyprdrive" hyprdrive.so
  (cd "$_src/look" && go build -o "$srcdir/ryoku-hyprdrive-look" .)
}

package() {
  install -Dm755 "$srcdir/hyprdrive/hyprdrive.so" "$pkgdir/usr/lib/hyprland/plugins/hyprdrive.so"
  install_plugin_abi hyprdrive
  install -Dm755 "$_src/bin/agent-run" "$pkgdir/usr/lib/hyprland/plugins/agent-run"
  install -Dm755 "$srcdir/ryoku-hyprdrive-look" "$pkgdir/usr/bin/ryoku-hyprdrive-look"
  install -Dm644 -t "$pkgdir/usr/share/ryoku/hyprdrive/fonts" "$_repo"/docs/lithic/library/go/fonts/*.ttf
  install -d "$pkgdir/usr/share/ryoku/hypr-plugins/hyprdrive"
  cp -a "$_src/src" "$_src/assets" "$_src/scripts" "$_src/tests" "$_src/Makefile" "$_src/hyprpm.toml" \
    "$pkgdir/usr/share/ryoku/hypr-plugins/hyprdrive/"
}
