# Shared by tests/vfs-e2e/run.sh and tests/smoke/modorganizer-smoke.sh (sourced).
#
# mo2_make_instance <fixture dir> <work dir> <game dir> <instance dir> <probe binary>
#                   [<steam root>]
#
# Creates, from tests/fixtures/vfs-e2e, the fake Skyrim Special Edition in <game dir>
# and the instance in <instance dir> (a global instance when that is under
# $XDG_DATA_HOME/ModOrganizer), with the placeholders of its ModOrganizer.ini filled in
# (@GAME_PATH@, @PROBE@ = the configured executable "probe", @PROBE_EXE@ = probe.exe
# of the game directory, @WORK@ = <work dir>, @INSTANCE@ = <instance dir>).
#
# With <steam root> (a fake Steam installation) <game dir> must be
# <steam root>/steamapps/common/Skyrim Special Edition; besides, it creates the
# appmanifest of Skyrim SE (489830), its Proton prefix (compatdata/489830, with the
# My Games and AppData/Local directories the game plugin uses) and the custom Proton
# GE-Proton11-7 (compatibilitytools.d).
mo2_make_instance() {
    local fixture="$1" work="$2" game="$3" instance="$4" probe="$5" steam="${6:-}"
    mkdir -p "$(dirname "$game")"
    cp -r "$fixture/game" "$game"
    mkdir -p "$(dirname "$instance")"
    cp -r "$fixture/instance" "$instance"
    mkdir -p "$instance/overwrite"
    sed -i -e "s|@GAME_PATH@|$game|g" -e "s|@PROBE@|$probe|g" \
        -e "s|@PROBE_EXE@|$game/probe.exe|g" -e "s|@WORK@|$work|g" \
        -e "s|@INSTANCE@|$instance|g" \
        "$instance/ModOrganizer.ini"
    [[ -n $steam ]] || return 0

    local pfx="$steam/steamapps/compatdata/489830"
    local users="$pfx/pfx/drive_c/users/steamuser"
    local tool="$steam/compatibilitytools.d/GE-Proton11-7"
    cat > "$steam/steamapps/appmanifest_489830.acf" <<'ACF'
"AppState"
{
	"appid"		"489830"
	"installdir"		"Skyrim Special Edition"
}
ACF
    mkdir -p "$users/Documents/My Games/Skyrim Special Edition" \
        "$users/AppData/Local/Skyrim Special Edition" "$tool"
    echo "11.0-100" > "$pfx/version"
    cat > "$tool/proton" <<'PROTON'
#!/usr/bin/env python3
CURRENT_PREFIX_VERSION="GE-Proton11-7"
PROTON
    chmod 755 "$tool/proton"
    cat > "$tool/compatibilitytool.vdf" <<'VDF'
"compatibilitytools"
{
	"compat_tools"
	{
		"GE-Proton11-7"
		{
			"install_path"		"."
			"display_name"		"GE-Proton11-7"
			"from_oslist"		"windows"
			"to_oslist"		"linux"
		}
	}
}
VDF
}
