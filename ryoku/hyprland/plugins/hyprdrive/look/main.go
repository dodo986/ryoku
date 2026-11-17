// Command ryoku-hyprdrive-look hands the desktop's look to the Hyprdrive compositor
// plugin, which draws in C++ and cannot link the Lithic library. It reads the reader's
// choice once with palette.ReadDesktop, prints it as one block of key=value lines ended
// by a blank line, and with --watch prints a new block every time the look changes,
// until stdin or stdout closes. It also names the directory holding Lithic's two
// faces, which the plugin hands to fontconfig.
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"image/color"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"reflect"
	"strings"
	"syscall"

	"github.com/ryoku-dev/ryoku/docs/lithic/library/go/palette"
	"github.com/ryoku-dev/ryoku/docs/lithic/library/go/tokens"
)

func main() {
	watch := flag.Bool("watch", false, "print a new block whenever the look changes")
	flag.Parse()

	// The plugin is the parent: when the compositor goes, so does this.
	_, _, _ = syscall.RawSyscall(syscall.SYS_PRCTL, syscall.PR_SET_PDEATHSIG, uintptr(syscall.SIGTERM), 0)

	fontDir := facesDir()

	out := bufio.NewWriter(os.Stdout)
	if err := writeBlock(out, palette.ReadDesktop(), fontDir); err != nil || !*watch {
		return
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer stop()
	go func() {
		_, _ = io.Copy(io.Discard, os.Stdin)
		stop()
	}()
	_ = palette.WatchDesktop(ctx, func(d palette.Desktop) {
		if writeBlock(out, d, fontDir) != nil {
			stop()
		}
	})
}

func writeBlock(out *bufio.Writer, d palette.Desktop, fontDir string) error {
	profile := d.Design.Profile()
	line := func(key string, value any) { fmt.Fprintf(out, "%s=%v\n", key, value) }

	line("design", d.Design)
	line("dark", d.Roles.Dark)
	line("contrast", d.Look.Contrast == palette.High)
	line("reduceMotion", d.ReduceMotion)
	line("source", d.Roles.Source)
	line("corner.round", profile.Round)
	line("corner.scale", profile.Scale)
	line("corner.pill", profile.Pill)
	line("corner.max", profile.Max)
	line("cut.large", tokens.CutLarge)
	line("cut.mid", tokens.CutMid)
	line("cut.small", tokens.CutSmall)
	line("cut.menu", tokens.CutMenu)
	line("cut.overlay", tokens.CutOverlay)
	line("cut.tooltip", tokens.CutTooltip)
	line("cut.token", tokens.CutToken)
	line("font.sans", "Geologica")
	line("font.mono", "Martian Mono")
	line("font.dir", fontDir)
	writeColours(line, "role.", d.Roles)
	writeColours(line, "plane.", d.Roles.Planes(d.Design))
	fmt.Fprintln(out)
	return out.Flush()
}

// writeColours prints every exported colour field of v under prefix, its name in
// lowerCamel as the QML library spells the role.
func writeColours(line func(string, any), prefix string, v any) {
	value := reflect.ValueOf(v)
	for i := range value.NumField() {
		field := value.Type().Field(i)
		if !field.IsExported() {
			continue
		}
		c, ok := value.Field(i).Interface().(color.NRGBA)
		if !ok {
			continue
		}
		line(prefix+strings.ToLower(field.Name[:1])+field.Name[1:], fmt.Sprintf("#%02x%02x%02x%02x", c.R, c.G, c.B, c.A))
	}
}

// facesDir is where Lithic's faces are on this machine: where the package lays
// them, else the library's own directory in the checkout a dev box deploys from.
// "" when neither has them; the plugin then draws in the fontconfig default.
func facesDir() string {
	candidates := []string{os.Getenv("LITHIC_FONTS_DIR"), "/usr/share/ryoku/hyprdrive/fonts"}
	if repo := checkout(); repo != "" {
		candidates = append(candidates, filepath.Join(repo, "docs", "lithic", "library", "go", "fonts"))
	}
	for _, dir := range candidates {
		if dir == "" {
			continue
		}
		if _, err := os.Stat(filepath.Join(dir, "Geologica-Regular.ttf")); err == nil {
			return dir
		}
	}
	return ""
}

// checkout is the Ryoku checkout this box deploys from, as `ryoku deploy`
// records it.
func checkout() string {
	if r := os.Getenv("RYOKU_REPO"); r != "" {
		return r
	}
	state := os.Getenv("XDG_STATE_HOME")
	if state == "" {
		state = filepath.Join(os.Getenv("HOME"), ".local", "state")
	}
	b, err := os.ReadFile(filepath.Join(state, "ryoku", "repo"))
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(b))
}
