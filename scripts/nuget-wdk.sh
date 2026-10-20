# Sourced, not run: the kernel-driver toolchain our viogpudo (track M24)
# and the WDDM driver's Arm kernel driver (track M25) build with, on
# Windows (MSYS2). Visual Studio 2022 supplies the compilers and, through
# its "Windows Driver Kit" component, the kernel-driver toolset (the
# components it needs are named when one is missing); the WDK and SDK are
# Microsoft's NuGet packages at the versions below, restored into
# build/viogpudo/packages by a nuget.exe fetched next to them. WDK
# 10.0.26100 is the last for Visual Studio 2022. docs/build-windows.md
# "Our viogpudo".
#
# Needs ROOT (the checkout) and the working directory at it.

WDK_VER=10.0.26100.6584
# the SDK the WDK packages depend on, at their floor (what NuGet resolves)
SDK_VER=10.0.26100.1
NUGET_URL=https://dist.nuget.org/win-x86-commandline/v6.14.0/nuget.exe
WDK_PKGS=build/viogpudo/packages
VS_COMPONENTS=(Component.Microsoft.Windows.DriverKit
               Microsoft.VisualStudio.Component.VC.Tools.ARM64
               Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre
               Microsoft.VisualStudio.Component.VC.Runtimes.ARM64.Spectre)

# Visual Studio 2022 with every component in VS_COMPONENTS, or a sentence
# naming what to add.
find_vs() {
  local vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" vs
  [ -x "$vswhere" ] || { echo "nuget-wdk.sh: no Visual Studio installer (vswhere)" >&2; return 1; }
  vs=$("$vswhere" -version '[17.0,18.0)' -products '*' -requires "${VS_COMPONENTS[@]}" \
       -property installationPath | tr -d '\r' | head -1)
  if [ -z "$vs" ]; then
    echo "nuget-wdk.sh: needs Visual Studio 2022 with these components (Visual Studio Installer > Modify, or setup.exe modify --add <id>):" >&2
    printf '    %s\n' "${VS_COMPONENTS[@]}" >&2
    return 1
  fi
  cygpath -u "$vs"
}

# The WDK and SDK packages, at the pins, in $WDK_PKGS.
restore_wdk() {
  local nuget="build/viogpudo/tools/nuget.exe" p
  if ! [ -f "$nuget" ]; then
    mkdir -p "$(dirname "$nuget")"
    curl -fsSL -o "$nuget" "$NUGET_URL"
  fi
  for p in "Microsoft.Windows.SDK.CPP $SDK_VER" "Microsoft.Windows.SDK.CPP.x64 $SDK_VER" \
           "Microsoft.Windows.SDK.CPP.arm64 $SDK_VER" \
           "Microsoft.Windows.WDK.x64 $WDK_VER" "Microsoft.Windows.WDK.ARM64 $WDK_VER"; do
    set -- $p
    [ -d "$WDK_PKGS/$1.$2" ] && continue
    echo "==> NuGet: $1 $2"
    "$nuget" install "$1" -Version "$2" -OutputDirectory "$(cygpath -w "$WDK_PKGS")" \
      -NonInteractive -Verbosity quiet
  done
}

# What the WDK's NuGet packages ask of a project (Microsoft's
# Windows-driver-samples do the same), at $1, with the packages at $2 as
# MSBuild will see them. viogpudo's goes in its tree as
# Directory.Build.props, which MSBuild picks up from the root; the WDDM
# driver's project imports its own by name (D3DPT_WDK_PROPS).
wdk_props() {
  local pkgs=$2
  {
    echo '<Project>'
    echo "  <Import Project=\"$pkgs/Microsoft.Windows.WDK.x64.$WDK_VER/build/native/Microsoft.Windows.WDK.x64.props\" Condition=\"'\$(Platform)' == 'x64'\"/>"
    echo "  <Import Project=\"$pkgs/Microsoft.Windows.WDK.ARM64.$WDK_VER/build/native/Microsoft.Windows.WDK.ARM64.props\" Condition=\"'\$(Platform)' == 'ARM64'\"/>"
    echo "  <Import Project=\"$pkgs/Microsoft.Windows.SDK.CPP.x64.$SDK_VER/build/native/Microsoft.Windows.SDK.cpp.x64.props\" Condition=\"'\$(Platform)' == 'x64'\"/>"
    echo "  <Import Project=\"$pkgs/Microsoft.Windows.SDK.CPP.arm64.$SDK_VER/build/native/Microsoft.Windows.SDK.cpp.arm64.props\" Condition=\"'\$(Platform)' == 'ARM64'\"/>"
    echo "  <Import Project=\"$pkgs/Microsoft.Windows.SDK.CPP.$SDK_VER/build/native/Microsoft.Windows.SDK.cpp.props\"/>"
    echo "  <PropertyGroup><D3DPT_WDK_VER>${WDK_VER%.*}.0</D3DPT_WDK_VER></PropertyGroup>"
    echo '</Project>'
  } > "$1"
}
