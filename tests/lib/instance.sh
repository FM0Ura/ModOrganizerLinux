# Shared by tests/vfs-e2e/run.sh and tests/smoke/modorganizer-smoke.sh (sourced).
#
# mo2_make_instance <fixture dir> <work dir> <game dir> <instance dir> <probe binary>
#
# Creates, from tests/fixtures/vfs-e2e, the fake Skyrim Special Edition in <game dir>
# and the instance in <instance dir> (a global instance when that is under
# $XDG_DATA_HOME/ModOrganizer), with the placeholders of its ModOrganizer.ini filled in
# (@GAME_PATH@, @PROBE@ = the configured executable "probe", @WORK@ = <work dir>).
mo2_make_instance() {
    local fixture="$1" work="$2" game="$3" instance="$4" probe="$5"
    cp -r "$fixture/game" "$game"
    mkdir -p "$(dirname "$instance")"
    cp -r "$fixture/instance" "$instance"
    mkdir -p "$instance/overwrite"
    sed -i -e "s|@GAME_PATH@|$game|g" -e "s|@PROBE@|$probe|g" \
        -e "s|@WORK@|$work|g" "$instance/ModOrganizer.ini"
}
